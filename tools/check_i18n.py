# -*- coding: utf-8 -*-

"""检查 C++ 源码里的 tr("...") 文案与英文表是否一致。

英文表（src/strings_en.cpp）是从 Python 版的 pdfcomp/lang_en.py 生成的，所以
真正要防的是另一件事：**C++ 代码里新写的 tr("中文") 有没有对应的英文**。
漏了不会崩，只会在英文界面下悄悄显示中文——正是那种"上线几个月后才被发现"
的问题，所以用脚本扫一遍。

    python tools/check_i18n.py

四条检查：
  1. C++ 里用到、英文表里没有的文案      -> 漏翻，必须补
  2. 英文表里有、C++ 里没用到的文案      -> 只是提示（可能这条只在 Python 版用）
  3. 占位符名字两边对不上的              -> 英文版会缺参数
  4. 含中文、却没包 tr(...) 的字符串字面量 -> 英文界面下必然原样显示中文

第 4 条是后加的，起因很具体：错误路径上顺手写 `error = "渲染结果为空文件";`
是最自然的写法，而这类字符串只在出问题时出现，测不到，于是英文用户看到的
就是半句英文加半句中文。它和第 1 条不是一回事——第 1 条管"翻了但没翻全"，
这条管"根本就没走翻译函数"。

退出码非 0 表示第 1、3、4 条里有不通过的，可以直接接进 CI。
"""

import ast
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src")
PY_TABLE = os.path.normpath(
    os.path.join(ROOT, "..", "..", "python", "pdf-compress", "pdfcomp", "lang_en.py"))

# 这两份是"表"本身，里面当然全是中文字面量，不参与第 4 条检查
TABLE_FILES = ("strings_en.cpp", "strings_en_extra.cpp")

# tr("中文") 或 tr(R"HELP(...)HELP") —— 只关心以 " 或 R"( 开头的字面量。
# C++ 源码是 UTF-8，中文直接写在里面，所以用字符匹配就够。
TR_PATTERN = re.compile(r'\btr\(\s*(?:u8)?(R?"(?:[^"\\]|\\.)*")', re.S)
# 相邻字面量会被 C++ 自动拼接（"很长的前半段" "后半段"），扫描时要跟着吃下去
NEXT_LITERAL = re.compile(r'\s*(?:u8)?(R?"(?:[^"\\]|\\.)*")')
# 任何位置的字面量，第 4 条用它做全量扫描
ANY_LITERAL = re.compile(r'(?:u8)?(R?"(?:[^"\\]|\\.)*")', re.S)
EXTRA_TABLE = os.path.join(ROOT, "src", "strings_en_extra.cpp")
# 键和值都允许由多个相邻字面量拼成（C++ 会自己接起来），所以两边都用
# "一个或多个字面量" 来匹配，解析时再逐个还原、拼接。
PAIR_PATTERN = re.compile(
    r'\{\s*("(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*)\s*,\s*'
    r'("(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*)\s*\}', re.S)

# 汉字（含扩展 A 区与兼容区）。用 \p{Han} 会连日文汉字一起抓进来，这里要的就是
# "这串字面量是中文写的"这个判断，所以按码位范围取。
CJK_PATTERN = re.compile(u"[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]")

# 第 4 条的例外。每一条都得说得出理由，否则这个检查会慢慢变成摆设。
ALLOWED_LITERALS = {
    # 交互回答的比较值：那是用户敲进来的输入，不是显示给用户看的文案
    "是": "manual_flow 拿它比较用户输入",
    "否": "manual_flow 拿它比较用户输入",
    # 语言名按各自的语言书写，翻译了反而看不懂
    "简体中文": "i18n.cpp 的语言显示名，故意不翻译",
    "中文": "语言切换按钮上的语言名，故意不翻译",
    # 右键菜单标题在代码里已经是"按语言二选一"
    "用 PDF Compressor 压缩": "shellmenu.cpp 里已按 language() 二选一",
}


def cpp_string_literal(token):
    """把 C++ 字面量还原成 Python 字符串。"""
    if token.startswith('R"'):
        # 原始字符串 R"HELP(...)HELP"：分隔符可以是任意短的串，这里取默认的
        body = token[2:]
        open_paren = body.find("(")
        delimiter = body[:open_paren]
        closing = ")" + delimiter + '"'
        if body.endswith(closing):
            return body[open_paren + 1:-len(closing)]
        return body[open_paren + 1:-1]
    body = token[1:-1]
    return (body.replace('\\"', '"').replace("\\n", "\n")
                .replace("\\\\", "\\").replace("\\t", "\t"))


def mask_comments(text):
    """把注释替换成等长空格。

    长度必须保持不变，因为后面要拿偏移量去和字面量的区间比对。注释里的中文
    很多（这个项目注释写得很细），不屏蔽掉的话第 4 条会被淹掉。
    扫描时自己认得字符串边界，所以注释符出现在字面量里（比如 URL）不会误判。
    """
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            i = j
            continue
        if ch == "'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "'":
                    j += 1
                    break
                j += 1
            i = j
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
            continue
        i += 1
    return "".join(out)


def source_files():
    for folder, _dirs, files in os.walk(SRC):
        for name in sorted(files):
            if name.endswith((".cpp", ".hpp")):
                yield os.path.join(folder, name)


def collect_from_cpp():
    """(文案 -> [文件:行号], 已被 tr(...) 吃掉的字面量区间 {文件: [(起点, 终点)]})"""
    found = {}
    consumed = {}
    for path in source_files():
        with io.open(path, encoding="utf-8") as handle:
            text = handle.read()
        spans = consumed.setdefault(path, [])
        for match in TR_PATTERN.finditer(text):
            literal = cpp_string_literal(match.group(1))
            spans.append((match.start(1), match.end(1)))
            # 接着吃相邻的字面量，C++ 会把它们拼成一个
            cursor = match.end()
            while True:
                following = NEXT_LITERAL.match(text, cursor)
                if not following:
                    break
                literal += cpp_string_literal(following.group(1))
                spans.append((following.start(1), following.end(1)))
                cursor = following.end()
            if not literal:
                continue
            line = text.count("\n", 0, match.start()) + 1
            found.setdefault(literal, []).append(
                "%s:%d" % (os.path.relpath(path, ROOT).replace("\\", "/"), line))
    return found, consumed


def find_untranslated(consumed, used, known):
    """含中文、既没走 tr(...)、也不在任何一张英文表里的字面量。

    判据是"值"而不是"位置"，因为界面的写法是先把文案存进成员变量或数组、画的
    时候再 `tr(text)`：
        btn_add_.text = tr("添加 PDF 文件");           // 这一处是 tr() 的参数
        const char* texts[] = {"目标大小 (MB)", ...};  // 存起来
        draw_text(ctx_, tr(texts[i]), ...);           // 用的时候才翻译
    只看"有没有直接当 tr() 参数"会把第二种写法整片误报，所以只要这个值在项目里
    当过 tr() 的参数、或者本身就收在英文表里（说明有人为它准备过英文），就认为
    它是翻译过的。

    真正要抓的是第三种情况：**值既没当过 tr() 的参数，英文表里也没有它**。
    那串中文在任何语言设置下都只能是中文，只能改代码。

    原始字符串（R"HELP(...)HELP"）直接跳过：cli.cpp 里那两段是整篇中英文帮助
    文本，本来就按语言各写一份，不走 tr()。
    """
    problems = []
    for path in source_files():
        if os.path.basename(path) in TABLE_FILES:
            continue
        with io.open(path, encoding="utf-8") as handle:
            raw = handle.read()
        text = mask_comments(raw)
        spans = consumed.get(path, [])
        for match in ANY_LITERAL.finditer(text):
            token = match.group(1)
            if token.startswith('R"'):
                continue
            start, end = match.start(1), match.end(1)
            if any(start >= s and end <= e for s, e in spans):
                continue
            literal = cpp_string_literal(token)
            if not CJK_PATTERN.search(literal):
                continue
            if literal in used or literal in known or literal in ALLOWED_LITERALS:
                continue
            line = text.count("\n", 0, start) + 1
            problems.append(("%s:%d" % (os.path.relpath(path, ROOT).replace("\\", "/"), line),
                             literal.replace("\n", "\\n")))
    return problems


def load_extra_table(path):
    """从 strings_en_extra.cpp 里抠出 {中文, 英文} 对。"""
    if not os.path.isfile(path):
        return {}
    with io.open(path, encoding="utf-8") as handle:
        text = handle.read()
    table = {}
    for match in PAIR_PATTERN.finditer(text):
        key = "".join(cpp_string_literal(part)
                      for part in ANY_LITERAL.findall(match.group(1)))
        value = "".join(cpp_string_literal(part)
                        for part in ANY_LITERAL.findall(match.group(2)))
        if key:
            table[key] = value
    return table


def load_python_table(path):
    with io.open(path, encoding="utf-8") as handle:
        tree = ast.parse(handle.read(), path)
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "ENGLISH":
            return ast.literal_eval(node.value)
    raise SystemExit("没有在 %s 里找到 ENGLISH 字典" % path)


def placeholders(text):
    names = set()
    for match in re.finditer(r"\{([A-Za-z_][A-Za-z0-9_]*)[^}]*\}", text):
        names.add(match.group(1))
    return names


def main():
    if not os.path.isfile(PY_TABLE):
        raise SystemExit("找不到英文表：%s" % PY_TABLE)
    used, consumed = collect_from_cpp()
    table = load_python_table(PY_TABLE)
    extra = load_extra_table(EXTRA_TABLE)
    known = dict(table)
    known.update(extra)
    problems = 0

    missing = {key: where for key, where in used.items() if key not in known}
    if missing:
        problems += 1
        print("以下文案在 C++ 里用了，但两张英文表里都没有（英文界面下会显示中文）：")
        for key in sorted(missing):
            print("  %-70s <- %s" % (key.replace("\n", "\\n")[:70], ", ".join(missing[key])))
        print("  两边共用的文案补到 Python 版的 lang_en.py 并重跑 tools/make_strings_en.py；")
        print("  只有 C++ 用的文案补到 src/strings_en_extra.cpp。")

    mismatched = []
    for key in used:
        if key not in known:
            continue
        if placeholders(key) != placeholders(known[key]):
            mismatched.append((key, placeholders(key), placeholders(known[key])))
    if mismatched:
        problems += 1
        print("以下文案的占位符两边对不上（英文版会缺参数）：")
        for key, zh, en in mismatched:
            print("  %s\n    zh=%s\n    en=%s" % (key, sorted(zh), sorted(en)))

    untranslated = find_untranslated(consumed, used, known)
    if untranslated:
        problems += 1
        print("以下字面量含中文，既没包在 tr(...) 里，也不在任何英文表里：")
        for where, literal in untranslated:
            print("  %-24s %s" % (where, literal[:90]))
        print("  要么包上 tr(\"…\") 并在英文表里补一条，要么本来就不该给用户看。")

    unused = [key for key in table if key not in used]
    if unused:
        print("（提示）从 Python 版生成的表里有 %d 条 C++ 侧暂时用不到，"
              "可能只在 Python 版使用。" % len(unused))

    print("C++ 侧用了 %d 条文案；生成表 %d 条 + C++ 补充表 %d 条。"
          % (len(used), len(table), len(extra)))
    if problems:
        print("检查未通过。")
        return 1
    print("检查通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
