# -*- coding: utf-8 -*-

"""生成应用图标（assets/pdf_compress.ico）。

图标是构建产物也是源资产：exe 的图标（src/app.rc 里 IDI_APPICON 指向的就是本文件
的产物）和右键菜单 DLL 的图标资源（build/windows/build_shell_menu.bat 里的 .rc）
都读这一个文件，所以它进仓库，脚本留在这里保证以后能重新生成。

画的是一个圆角方块底 + 白纸 + 三条越来越短的线（"被压扁的文档"）。
小尺寸下细节会糊，所以线条要少、要粗。

    python assets/make_icon.py
"""

import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(HERE, "pdf_compress.ico")

# 背景渐变：上浅下深，和界面里的强调色是同一个色系
TOP = (0x2B, 0x8A, 0xEF)
BOTTOM = (0x00, 0x5A, 0xAE)
PAPER = (0xFF, 0xFF, 0xFF)
INk = (0x00, 0x67, 0xC0)

SUPERSAMPLE = 8
BASE = 256


def render(size: int = BASE) -> Image.Image:
    scale = size * SUPERSAMPLE
    image = Image.new("RGBA", (scale, scale), (0, 0, 0, 0))

    # 圆角方形背景（垂直渐变 + 圆角遮罩）
    gradient = Image.new("RGBA", (scale, scale))
    draw = ImageDraw.Draw(gradient)
    for y in range(scale):
        ratio = y / max(1, scale - 1)
        colour = tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * ratio) for i in range(3))
        draw.line([(0, y), (scale, y)], fill=colour + (255,))
    mask = Image.new("L", (scale, scale), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, scale - 1, scale - 1],
                                           radius=int(scale * 0.23), fill=255)
    image.paste(gradient, (0, 0), mask)

    draw = ImageDraw.Draw(image)
    # 白纸
    left, top = int(scale * 0.26), int(scale * 0.16)
    right, bottom = int(scale * 0.74), int(scale * 0.84)
    draw.rounded_rectangle([left, top, right, bottom],
                           radius=int(scale * 0.055), fill=PAPER + (255,))

    # 三条越来越短的行：压扁的文档
    inner = right - left
    rows = ((0.34, 1.00), (0.48, 0.72), (0.62, 0.46))
    thickness = max(1, int(scale * 0.055))
    for ratio_y, ratio_w in rows:
        y = top + int((bottom - top) * ratio_y)
        draw.rounded_rectangle(
            [left + int(inner * 0.16), y,
             left + int(inner * (0.16 + 0.68 * ratio_w)), y + thickness],
            radius=thickness // 2, fill=INk + (255,))

    return image.resize((size, size), Image.LANCZOS)


def main() -> None:
    icon = render(BASE)
    icon.save(OUTPUT, format="ICO",
              sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                     (128, 128), (256, 256)])
    preview = os.path.join(HERE, "pdf_compress_256.png")
    icon.save(preview, format="PNG")
    print("wrote", OUTPUT, os.path.getsize(OUTPUT), "bytes")
    print("wrote", preview)


if __name__ == "__main__":
    main()
