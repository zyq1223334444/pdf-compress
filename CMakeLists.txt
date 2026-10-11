cmake_minimum_required(VERSION 3.16)
project(pdf-compress VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ── 版本与元信息 ──────────────────────────────────────────────
# 版本号同时写进 CMake 工程、C++ 常量（app.hpp）和 Windows 资源，改这里就够。
add_compile_definitions(PDFC_VERSION="${PROJECT_VERSION}")

# ── 第三方依赖 ────────────────────────────────────────────────
# MuPDF 提供 PDF 解析、渲染和写出；libjpeg-turbo 由 MuPDF 内部使用，
# 我们自己只用 MuPDF 的 fz_* JPEG 编码接口，不再单独链接它。
#
# 头文件与库的查找顺序：
#   1. -DMUPDF_ROOT=<前缀>          （自己编译的 MuPDF 或解压的二进制包）
#   2. 系统路径 / pkg-config        （Linux: libmupdf-dev, macOS: brew install mupdf）
#   3. third_party/mupdf/           （仓库内放置的预编译产物）
set(MUPDF_ROOT "" CACHE PATH "MuPDF 安装前缀（包含 include/ 与 lib/）")

find_path(MUPDF_INCLUDE_DIR
    NAMES mupdf/fitz.h
    HINTS ${MUPDF_ROOT} ENV MUPDF_ROOT
    PATH_SUFFIXES include
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mupdf)

find_library(MUPDF_LIBRARY
    NAMES mupdf libmupdf mupdfcpp
    HINTS ${MUPDF_ROOT} ENV MUPDF_ROOT
    PATH_SUFFIXES lib lib64
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mupdf/lib
          ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mupdf)

if(NOT MUPDF_INCLUDE_DIR OR NOT MUPDF_LIBRARY)
    message(FATAL_ERROR
        "找不到 MuPDF。请用 -DMUPDF_ROOT=<前缀> 指定，或安装发行版包：\n"
        "  Linux   sudo apt install libmupdf-dev\n"
        "  macOS   brew install mupdf\n"
        "  Windows 用 build\\windows\\build_mupdf.bat 编译到 third_party/mupdf\n"
        "  Linux/macOS 用 build/unix/build_mupdf_unix.sh 编译到 build/out/mupdf-unix-prefix")
endif()
message(STATUS "MuPDF include: ${MUPDF_INCLUDE_DIR}")
message(STATUS "MuPDF library: ${MUPDF_LIBRARY}")

# ── 源文件 ────────────────────────────────────────────────────
set(PDFC_SOURCES
    src/main.cpp
    src/common.cpp
    src/i18n.cpp
    src/strings_en.cpp
    src/strings_en_extra.cpp
    src/config.cpp
    src/options.cpp
    src/cli.cpp
    src/pdf.cpp
    src/search.cpp
    src/engine.cpp
    src/process.cpp
    src/worker.cpp
    src/filedialog.cpp
    src/shellmenu.cpp
)

# 图形界面只用 Win32 + Direct2D 手绘，不引入 Qt/GTK 之类的框架。
# 非 Windows 平台编译出的就是纯命令行版本，见 README 的说明。
if(WIN32)
    list(APPEND PDFC_SOURCES
        src/gui/theme.cpp
        src/gui/widgets.cpp
        src/gui/window.cpp
        src/gui/gui.cpp
        src/platform_win.cpp
        src/app.rc              # 图标与版本资源，MSVC 的 CMake 会自动调 rc.exe
    )
else()
    list(APPEND PDFC_SOURCES
        src/platform_posix.cpp
        src/gui/gui_stub.cpp
    )
endif()

# 构建时生成的右键菜单载荷（build/windows/build_shell_menu.bat 写出）。
# 没生成过时用仓库自带的占位实现 src/shell_payload_default.cpp，于是"没有右键
# 菜单组件的构建"照样能编出来 —— 关键是源文件列表里永远有且只有一份实现，
# 否则干净 clone 出来的树会在链接阶段报找不到 kShellMsixHex。
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/shell_payload.cpp)
    list(APPEND PDFC_SOURCES src/shell_payload.cpp)
    message(STATUS "右键菜单载荷：使用构建生成的 src/shell_payload.cpp")
else()
    list(APPEND PDFC_SOURCES src/shell_payload_default.cpp)
    message(STATUS "右键菜单载荷：使用内置占位实现（--install-menu 会提示本构建不含右键菜单）")
endif()

add_executable(pdf-compress ${PDFC_SOURCES})

# 可执行文件必须叫 pdf_compress.exe：右键菜单的 MSIX 清单里写死了这个名字
# （<Application Executable="pdf_compress.exe">），DLL 的 GetIcon 也按它找。
set_target_properties(pdf-compress PROPERTIES OUTPUT_NAME pdf_compress)

if(WIN32)
    # rc.exe 解析 src/app.rc 里的相对路径时用的是它自己的 include 路径，
    # 不是 target_include_directories：靠这一行找到 assets\pdf_compress.ico。
    include_directories(${CMAKE_CURRENT_SOURCE_DIR})
endif()

target_include_directories(pdf-compress PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${MUPDF_INCLUDE_DIR}
)

# MuPDF 从源码编译（build/unix/build_mupdf_unix.sh）时，第三方库是单独一个归档：
# libmupdf.a 只有 MuPDF 自己的目标文件，zlib/freetype/harfbuzz 那些在
# libmupdf-third.a 里，少链一个就是满屏 undefined reference。发行版包通常把两者
# 合成一份，那就不会有这个文件，下面这段自然什么都不加。
get_filename_component(MUPDF_LIBRARY_DIR "${MUPDF_LIBRARY}" DIRECTORY)
set(MUPDF_EXTRA_LIBRARIES "")
foreach(_mupdf_extra_name libmupdf-third.a libmupdf-third.so libmupdf-third.dylib)
    if(EXISTS "${MUPDF_LIBRARY_DIR}/${_mupdf_extra_name}")
        list(APPEND MUPDF_EXTRA_LIBRARIES "${MUPDF_LIBRARY_DIR}/${_mupdf_extra_name}")
        message(STATUS "MuPDF third-party library: ${MUPDF_LIBRARY_DIR}/${_mupdf_extra_name}")
    endif()
endforeach()

target_link_libraries(pdf-compress PRIVATE ${MUPDF_LIBRARY} ${MUPDF_EXTRA_LIBRARIES})

if(WIN32)
    target_link_libraries(pdf-compress PRIVATE
        d2d1 dwrite dwmapi windowscodecs shlwapi ole32 oleaut32 shell32
        comctl32          # SetWindowSubclass / DefSubclassProc（输入框的子类化）
        advapi32 user32 gdi32 comdlg32)
    # GUI 子系统：双击不弹控制台黑框，程序自己在需要时附加父控制台
    # （见 src/platform_win.cpp 的 attach_parent_console）。
    set_target_properties(pdf-compress PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_compile_options(pdf-compress PRIVATE /utf-8 /W4 /permissive-)
        target_compile_definitions(pdf-compress PRIVATE
            _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(pdf-compress PRIVATE -Wall -Wextra -municode)
    endif()
else()
    target_compile_options(pdf-compress PRIVATE -Wall -Wextra)
    target_link_libraries(pdf-compress PRIVATE pthread)
endif()

install(TARGETS pdf-compress RUNTIME DESTINATION bin)

# ── 单元测试 ──────────────────────────────────────────────────
# 两个可执行文件而不是一个：搜索测试和文案表测试各自有 main()，分开更省事，
# 而且它们依赖的东西本来就不同（搜索测试连 MuPDF 都不需要）。
# 漏翻的静态检查在 tools/check_i18n.py 里，那个是跨语言比对，编译期做不了。
enable_testing()

add_executable(pdfc_search_tests
    tests/test_search.cpp
    src/search.cpp src/common.cpp src/i18n.cpp
    src/strings_en.cpp src/strings_en_extra.cpp)

add_executable(pdfc_i18n_tests
    tests/test_i18n.cpp
    src/common.cpp src/i18n.cpp src/strings_en.cpp src/strings_en_extra.cpp)

foreach(test_target pdfc_search_tests pdfc_i18n_tests)
    target_include_directories(${test_target} PRIVATE src)
    if(WIN32)
        # user32 是必须的：platform_win.cpp 里的 SetProcessDPIAware 在那里
        target_link_libraries(${test_target} PRIVATE user32 shlwapi advapi32)
        target_sources(${test_target} PRIVATE src/platform_win.cpp)
        if(MSVC)
            # 与主程序同样的编译环境：NOMINMAX 是必须的，否则 windows.h 的
            # min/max 宏会把 std::max 拆坏
            target_compile_options(${test_target} PRIVATE /utf-8 /W4 /permissive-)
            target_compile_definitions(${test_target} PRIVATE
                _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
    else()
        target_sources(${test_target} PRIVATE src/platform_posix.cpp)
    endif()
endforeach()

add_test(NAME search COMMAND pdfc_search_tests)
add_test(NAME i18n COMMAND pdfc_i18n_tests)
