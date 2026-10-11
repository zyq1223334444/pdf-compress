# -*- coding: utf-8 -*-

"""只下载 MuPDF 的头文件（不编译）。

用途有两个：

1. **对着真实头文件检查代码**。`pdf.cpp` 里那些 MuPDF 调用（`fz_new_pixmap_from_page`
   的参数个数、`pdf_add_image` 的签名之类）会随版本变，用真头文件做一次
   `cl /c` 或 `g++ -fsyntax-only` 就能立刻发现对不上的地方，不必先把整个 MuPDF
   编译出来。
2. **给只想要头文件的场景兜底**：真正的构建仍然需要 `libmupdf`，走
   `build/windows/build_mupdf.bat`（Windows）、`build/unix/build_mupdf_unix.sh`
   （Linux/macOS）或发行版包。

走 `gh api`（GitHub CLI 已经带好了认证与代理），所以不受直连 GitHub 被重置的影响。

    python tools/fetch_mupdf.py [版本，默认 1.26.0]
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REPO = "ArtifexSoftware/mupdf"
TARGET = os.path.join(ROOT, "third_party", "mupdf", "include")


def gh(args, binary=False):
    result = subprocess.run(["gh"] + args, capture_output=True)
    if result.returncode != 0:
        raise SystemExit("gh %s 失败：%s" % (" ".join(args),
                                            result.stderr.decode("utf-8", "replace")))
    return result.stdout if binary else result.stdout.decode("utf-8", "replace")


def main():
    revision = sys.argv[1] if len(sys.argv) > 1 else "1.26.0"
    print("列出 %s@%s 的文件树..." % (REPO, revision))
    tree = gh(["api", "repos/%s/git/trees/%s?recursive=1" % (REPO, revision)])
    import json

    entries = json.loads(tree).get("tree", [])
    headers = [e["path"] for e in entries
               if e["type"] == "blob" and e["path"].startswith("include/mupdf/")
               and e["path"].endswith(".h")]
    if not headers:
        raise SystemExit("没有找到任何头文件，检查版本号是否正确")
    print("找到 %d 个头文件，开始下载到 %s" % (len(headers), TARGET))

    for index, path in enumerate(sorted(headers), start=1):
        relative = path[len("include/"):]
        destination = os.path.join(TARGET, relative.replace("/", os.sep))
        os.makedirs(os.path.dirname(destination), exist_ok=True)
        if os.path.isfile(destination) and os.path.getsize(destination) > 0:
            continue
        data = gh(["api", "repos/%s/contents/%s?ref=%s" % (REPO, path, revision),
                   "-H", "Accept: application/vnd.github.raw"], binary=True)
        with open(destination, "wb") as handle:
            handle.write(data)
        if index % 20 == 0:
            print("  ... %d/%d" % (index, len(headers)))

    print("完成。用法：")
    print("  cl /c /I third_party\\mupdf\\include src\\pdf.cpp     (Windows 语法检查)")
    print("  g++ -fsyntax-only -I third_party/mupdf/include src/pdf.cpp")


if __name__ == "__main__":
    main()
