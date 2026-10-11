// ExplorerCommand.cpp
//
// Single-file x64 in-process COM server for the Windows 11 *top-level* File
// Explorer context menu ("Compress with PDF Compressor").
//
// Why this shape:
//   The Windows 11 modern context menu cannot be extended from the registry
//   alone. File Explorer only surfaces commands that come from an application
//   with *package identity*, so an unpackaged Win32 app has to ship a "sparse"
//   MSIX package (an identity package registered with -ExternalLocation) whose
//   manifest (a) registers this DLL as a COM *surrogate* server
//   (windows.comServer) and (b) binds the CLSID to the ".pdf" shell item type
//   (windows.fileExplorerContextMenus). See shell/AppxManifest.xml.
//
//   The COM class is *not* activated through the classic InprocServer32
//   registry key: the package registration is only visible to packaged callers,
//   so File Explorer activates it through the packaged surrogate (dllhost.exe)
//   declared by com:SurrogateServer. Registering the CLSID under
//   HKCU\Software\Classes\CLSID is only used by the build-time unit test.

#include <initguid.h>  // Must be the first include. This is the single translation
                       // unit of the module, so it is the one place where the
                       // GUIDs declared with DEFINE_GUID/MIDL_DEFINE_GUID in the
                       // SDK headers (and the CLSID below) become definitions.
#include <windows.h>
#include <shobjidl_core.h>  // IExplorerCommand, IShellItemArray, IEnumShellItems
#include <shlwapi.h>        // SHStrDupW
#include <new>              // std::nothrow
#include <string>

namespace {

// Configuration key written by the installer. HKCU wins over HKLM so a single
// user can override a machine-wide installation.
const wchar_t* const kConfigSubKeyUser = L"Software\\pdf-compress";
const wchar_t* const kConfigSubKeyMachine = L"SOFTWARE\\pdf-compress";

LONG g_objectCount = 0;  // live COM objects created by this DLL
LONG g_lockCount = 0;    // outstanding IClassFactory::LockServer locks

// ---------------------------------------------------------------------------
// Configuration / path resolution (registry only - never any file I/O here).
// ---------------------------------------------------------------------------

bool ReadRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, std::wstring& value)
{
    DWORD type = 0;
    DWORD cb = 0;
    if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                     &type, nullptr, &cb) != ERROR_SUCCESS) {
        return false;
    }
    if (cb < sizeof(wchar_t)) {
        return false;  // empty value
    }

    std::wstring buffer(cb / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                     &type, &buffer[0], &cb) != ERROR_SUCCESS) {
        return false;
    }
    buffer.resize(wcslen(buffer.c_str()));
    if (buffer.empty()) {
        return false;
    }
    value = buffer;
    return true;
}

bool ReadConfigString(const wchar_t* valueName, std::wstring& value)
{
    return ReadRegString(HKEY_CURRENT_USER, kConfigSubKeyUser, valueName, value)
        || ReadRegString(HKEY_LOCAL_MACHINE, kConfigSubKeyMachine, valueName, value);
}

// The UI language is a plain registry read: GetTitle must never be slow.
bool IsEnglishUi()
{
    std::wstring language;
    return ReadConfigString(L"Language", language) && _wcsicmp(language.c_str(), L"en") == 0;
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Resolve pdf_compress.exe: InstallDir from the registry first, then PATH.
// Unlike GetTitle this may touch the file system, but it only runs while the
// context menu is being built or when the command is invoked.
bool ResolveExecutable(std::wstring& exePath)
{
    std::wstring installDir;
    if (ReadConfigString(L"InstallDir", installDir)) {
        while (!installDir.empty() && (installDir[installDir.size() - 1] == L'\\' ||
                                       installDir[installDir.size() - 1] == L'/')) {
            installDir.erase(installDir.size() - 1);
        }
        const std::wstring candidate = installDir + L"\\pdf_compress.exe";
        if (FileExists(candidate)) {
            exePath = candidate;
            return true;
        }
    }

    wchar_t found[MAX_PATH * 4] = {};
    if (SearchPathW(nullptr, L"pdf_compress.exe", nullptr, ARRAYSIZE(found), found, nullptr) > 0) {
        exePath = found;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The command itself.
// ---------------------------------------------------------------------------

class __declspec(uuid("9E7B2C41-5A64-4E1B-9C3D-2F7A8B1D4E60")) ExplorerCommand final
    : public IExplorerCommand
{
public:
    ExplorerCommand() { InterlockedIncrement(&g_objectCount); }
    virtual ~ExplorerCommand() { InterlockedDecrement(&g_objectCount); }

    // IUnknown -------------------------------------------------------------
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        *ppv = nullptr;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IExplorerCommand))) {
            *ppv = static_cast<IExplorerCommand*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_refs));
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        const LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) {
            delete this;
        }
        return static_cast<ULONG>(refs);
    }

    // IExplorerCommand -----------------------------------------------------
    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* ppszName) override
    {
        if (ppszName == nullptr) {
            return E_POINTER;
        }
        *ppszName = nullptr;
        // "\u7528 PDF Compressor \u538b\u7f29" == "用 PDF Compressor 压缩".
        // Universal character names keep this source file encoding independent.
        return SHStrDupW(IsEnglishUi() ? L"Compress with PDF Compressor"
                                       : L"\u7528 PDF Compressor \u538b\u7f29",
                         ppszName);
    }

    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* ppszIcon) override
    {
        if (ppszIcon == nullptr) {
            return E_POINTER;
        }
        *ppszIcon = nullptr;
        std::wstring exePath;
        if (!ResolveExecutable(exePath)) {
            return E_NOTIMPL;  // no icon: the command still works
        }
        exePath += L",0";  // icon index 0 of the executable
        return SHStrDupW(exePath.c_str(), ppszIcon);
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray*, PWSTR* ppszInfotip) override
    {
        if (ppszInfotip != nullptr) {
            *ppszInfotip = nullptr;
        }
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID* pguidCommandName) override
    {
        if (pguidCommandName == nullptr) {
            return E_POINTER;
        }
        // GUID_NULL spelled out: the SDK only declares it in some headers, and
        // it is not worth pulling in another one for a constant.
        const GUID nullGuid = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
        *pguidCommandName = nullGuid;
        return S_OK;
    }

    IFACEMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pCmdState) override
    {
        if (pCmdState == nullptr) {
            return E_POINTER;
        }
        // Never hide or disable: an HKCU override must not change the menu
        // layout, and the manifest already scopes the verb to ".pdf" items.
        *pCmdState = ECS_ENABLED;
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx*) override
    {
        std::wstring exePath;
        if (!ResolveExecutable(exePath)) {
            MessageBoxW(nullptr,
                        IsEnglishUi()
                            ? L"pdf_compress.exe could not be found.\n\n"
                              L"Reinstall PDF Compressor, or set the InstallDir value under "
                              L"HKCU\\Software\\pdf-compress."
                            : L"\u627e\u4e0d\u5230 pdf_compress.exe\u3002\n\n"
                              L"\u8bf7\u91cd\u65b0\u5b89\u88c5 PDF Compressor\uff0c"
                              L"\u6216\u8bbe\u7f6e HKCU\\Software\\pdf-compress \u7684 "
                              L"InstallDir\u3002",
                        L"PDF Compressor", MB_OK | MB_ICONWARNING);
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        if (psiItemArray == nullptr) {
            return E_INVALIDARG;
        }

        // One single launch with every selected file as an argument. Explorer
        // calls Invoke on its UI thread, so keep this short: ShellExecuteW only
        // creates the process and returns.
        IEnumShellItems* enumerator = nullptr;
        HRESULT hr = psiItemArray->EnumItems(&enumerator);
        if (FAILED(hr) || enumerator == nullptr) {
            return FAILED(hr) ? hr : E_FAIL;
        }

        std::wstring arguments;
        unsigned int fileCount = 0;
        IShellItem* item = nullptr;
        while (enumerator->Next(1, &item, nullptr) == S_OK) {
            PWSTR filePath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath)) && filePath != nullptr) {
                if (fileCount > 0) {
                    arguments += L' ';
                }
                arguments += L'"';
                for (const wchar_t* p = filePath; *p != L'\0'; ++p) {
                    if (*p == L'"') {
                        arguments += L'"';  // "" inside a quoted argument == literal quote
                    }
                    arguments += *p;
                }
                arguments += L'"';
                ++fileCount;
                CoTaskMemFree(filePath);
            }
            item->Release();
            item = nullptr;
        }
        enumerator->Release();

        if (fileCount == 0) {
            return E_INVALIDARG;
        }

        const HINSTANCE result = ShellExecuteW(nullptr, L"open", exePath.c_str(),
                                               arguments.c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            return HRESULT_FROM_WIN32(static_cast<DWORD>(reinterpret_cast<INT_PTR>(result)));
        }
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override
    {
        if (pFlags == nullptr) {
            return E_POINTER;
        }
        *pFlags = ECF_DEFAULT;  // no separator, no flyout
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override
    {
        if (ppEnum != nullptr) {
            *ppEnum = nullptr;
        }
        return E_NOTIMPL;
    }

private:
    LONG m_refs = 1;
};

// ---------------------------------------------------------------------------
// Class factory and DLL exports.
// ---------------------------------------------------------------------------

class ClassFactory final : public IClassFactory
{
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        *ppv = nullptr;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IClassFactory))) {
            *ppv = static_cast<IClassFactory*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_refs));
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        const LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) {
            delete this;
        }
        return static_cast<ULONG>(refs);
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        *ppv = nullptr;
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }
        ExplorerCommand* command = new (std::nothrow) ExplorerCommand();
        if (command == nullptr) {
            return E_OUTOFMEMORY;
        }
        const HRESULT hr = command->QueryInterface(riid, ppv);
        command->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) {
            InterlockedIncrement(&g_lockCount);
        } else {
            InterlockedDecrement(&g_lockCount);
        }
        return S_OK;
    }

private:
    LONG m_refs = 1;
};

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

// Exported through ExplorerCommand.def; checked with dumpbin /exports by
// build\windows\build_shell_menu.bat.
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (ppv == nullptr) {
        return E_POINTER;
    }
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, __uuidof(ExplorerCommand))) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    ClassFactory* factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    const bool idle = InterlockedCompareExchange(&g_objectCount, 0, 0) == 0
                   && InterlockedCompareExchange(&g_lockCount, 0, 0) == 0;
    return idle ? S_OK : S_FALSE;
}
