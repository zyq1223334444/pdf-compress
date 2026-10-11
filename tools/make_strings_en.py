# -*- coding: utf-8 -*-

"""把 Python 版的英文文案表转成 C++ 的 strings_en.cpp。

中文是这个项目的源语言，英文表以中文原文为键。C++ 版和 Python 版用的是同一套
键，所以这份表直接由 lang_en.py 生成，而不是手抄一遍——抄一遍必然会漂移。

用法（在 C++ 工程根目录）：
    python tools/make_strings_en.py [path/to/lang_en.py]

默认路径指向同仓库里的 Python 实现：../../python/pdf-compress/pdfcomp/lang_en.py
生成的文件是提交进仓库的普通源码，这个脚本只是方便以后同步。
"""

import ast
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_SOURCE = os.path.normpath(
    os.path.join(ROOT, "..", "..", "python", "pdf-compress", "pdfcomp", "lang_en.py"))
OUTPUT = os.path.join(ROOT, "src", "strings_en.cpp")


def load_table(path):
    with io.open(path, encoding="utf-8") as handle:
        tree = ast.parse(handle.read(), path)
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "ENGLISH":
            return ast.literal_eval(node.value)
    raise SystemExit("没有在 %s 里找到 ENGLISH 字典" % path)


def cpp_literal(text):
    """把 Python 字符串写成 C++ 字符串字面量（源文件是 UTF-8，直接放原文）。"""
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SOURCE
    if not os.path.isfile(source):
        raise SystemExit("找不到 %s" % source)
    table = load_table(source)

    lines = [
        "// -*- coding: utf-8 -*-",
        "//",
        "// English translations.",
        "//",
        "// 这个文件由 tools/make_strings_en.py 从 Python 版的 pdfcomp/lang_en.py 生成，",
        "// 不要手改：改文案请改那份表，然后重跑生成脚本。中文是源语言，键就是中文原文。",
        "// 占位符（{name}、{size:.2f} …）必须与中文原文一一对应。",
        "",
        '#include "i18n.hpp"',
        "",
        "namespace pdfc {",
        "namespace {",
        "",
        "const std::pair<const char*, const char*> kEnglishTable[] = {",
    ]
    for key, value in table.items():
        lines.append("    {%s, %s}," % (cpp_literal(key), cpp_literal(value)))
    lines += [
        "};",
        "",
        "}  // namespace",
        "",
        "const std::vector<std::pair<const char*, const char*>>& english_table() {",
        "    static const std::vector<std::pair<const char*, const char*>> table(",
        "        kEnglishTable, kEnglishTable + sizeof(kEnglishTable) / sizeof(kEnglishTable[0]));",
        "    return table;",
        "}",
        "",
        "}  // namespace pdfc",
        "",
    ]
    with io.open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("wrote %s (%d entries) from %s" % (OUTPUT, len(table), source))


if __name__ == "__main__":
    main()
