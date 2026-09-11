# -*- coding: utf-8 -*-
"""
生成 4x4 矩阵键盘按键功能图（STM32F407 SMART-M）
输出: 矩阵键盘4x4按键功能.png
"""
from PIL import Image, ImageDraw, ImageFont

W, H = 1600, 1760
FONT_PATH = r"C:\Windows\Fonts\msyh.ttc"

# ---------- 配色 ----------
BG        = (253, 252, 248)   # 暖白
TEXT      = (26, 27, 28)
GRAY      = (110, 115, 122)
ACT_FILL  = (223, 233, 251)   # 浅蓝
ACT_BRD   = (155, 187, 244)
INA_FILL  = (243, 242, 238)   # 浅灰
INA_BRD   = (224, 222, 218)
CARD_FILL = (238, 244, 252)
CARD_BRD  = (155, 187, 244)

def font(size, bold=False):
    return ImageFont.truetype(FONT_PATH, size)

F_TITLE   = font(46)
F_SUB     = font(26)
F_KEY     = font(76)
F_LINE    = font(26)
F_CARD_T  = font(32)
F_CARD_L  = font(27)
F_NOTE    = font(24)

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)

# ---------- 标题 ----------
title = "4×4 矩阵键盘按键功能图"
tw = d.textlength(title, font=F_TITLE)
d.text(((W - tw) / 2, 46), title, font=F_TITLE, fill=TEXT)

sub = "STM32F407 SMART-M · 行 R1–R4：PE2 / PE6 / PE7 / PE10（输出拉低扫描） · 列 C1–C4：PE8 / PE9 / PE11 / PE12（输入上拉，低电平=按下）"
sw = d.textlength(sub, font=F_SUB)
d.text(((W - sw) / 2, 118), sub, font=F_SUB, fill=GRAY)

# ---------- 按键网格 ----------
MARGIN, GAP = 70, 20
CW = (W - 2 * MARGIN - 3 * GAP) // 4   # 350
CH = 240
keys = [
    # (键名, 功能行, 是否启用)
    ("1", ["退出设置"], True),
    ("2", ["设置模式：上移"], True),
    ("3", ["设置模式：确认修改"], True),
    ("A", ["切换OLED页面", "确认进入调整"], True),

    ("4", ["调整时数值减少"], True),
    ("5", ["无功能"], False),
    ("6", ["调整时数值增大"], True),
    ("B", ["继电器开/关切换", "调整时取消返回"], True),

    ("7", ["无功能"], False),
    ("8", ["设置模式：下移"], True),
    ("9", ["无功能"], False),
    ("C", ["蜂鸣器测试", "短鸣 100ms"], True),

    ("*", ["进入/退出设置", "调整时数值减"], True),
    ("0", ["无功能"], False),
    ("#", ["调整时数值加"], True),
    ("D", ["进入设置模式"], True),
]

grid_top = 190
for i, (k, lines, act) in enumerate(keys):
    r, c = divmod(i, 4)
    x = MARGIN + c * (CW + GAP)
    y = grid_top + r * (CH + GAP)
    fill, brd = (ACT_FILL, ACT_BRD) if act else (INA_FILL, INA_BRD)
    d.rounded_rectangle([x, y, x + CW, y + CH], radius=18, fill=fill, outline=brd, width=3)

    # 键名（居中偏上）
    kc = TEXT if act else GRAY
    kw = d.textlength(k, font=F_KEY)
    d.text((x + (CW - kw) / 2, y + 18), k, font=F_KEY, fill=kc)

    # 功能行（键名下方居中）
    line_h = 34
    total_h = len(lines) * line_h
    ly = y + 122 + (CH - 122 - 20 - total_h) / 2
    lc = TEXT if act else (168, 172, 178)
    for j, ln in enumerate(lines):
        lw = d.textlength(ln, font=F_LINE)
        d.text((x + (CW - lw) / 2, ly + j * line_h), ln, font=F_LINE, fill=lc)

# ---------- 三种模式卡片 ----------
cards = [
    ("正常模式", [
        "D    进入设置模式",
        "A    切换 OLED 页面（实时/阈值/状态）",
        "B    继电器开 / 关切换",
        "C    蜂鸣器短鸣 100ms",
    ]),
    ("选择模式（SET_MODE_SELECT）", [
        "2    上移阈值项",
        "8    下移阈值项",
        "3    确认修改",
        "1    退出设置",
    ]),
    ("调整模式（SET_MODE_ADJUST）", [
        "6    数值增大",
        "4    数值减少",
        "3    确认修改（保存）",
        "1    退出设置",
    ]),
]
card_top = grid_top + 4 * CH + 3 * GAP + 56   # 1286
card_h = 320
card_w = (W - 2 * MARGIN - 2 * GAP) // 3       # 473
for idx, (ct, clines) in enumerate(cards):
    x = MARGIN + idx * (card_w + GAP)
    d.rounded_rectangle([x, card_top, x + card_w, card_top + card_h],
                        radius=16, fill=CARD_FILL, outline=CARD_BRD, width=3)
    d.text((x + 22, card_top + 18), ct, font=F_CARD_T, fill=(59, 111, 214))
    for j, ln in enumerate(clines):
        d.text((x + 22, card_top + 78 + j * 52), ln, font=F_CARD_L, fill=TEXT)

# ---------- 底部说明 ----------
notes = [
    "阈值三项：温度 temp_high（每次 ±1℃，下限 10℃）｜振动 vib_high（±0.1，下限 0.2）｜电流 curr_high（±0.1，下限 0.2）",
    "消抖：50ms 周期 × 连续 3 次同键 ≈ 150ms 生效一次 ｜ 按键映射：1 退出设置，2/8 上/下移，3 确认修改，4/6 减/增，D 进入设置模式",
]
ny = card_top + card_h + 40
for n in notes:
    d.text((MARGIN, ny), n, font=F_NOTE, fill=GRAY)
    ny += 40

out = r"D:\STM32kdl\SMART-M\矩阵键盘4x4按键功能_新版.png"
img.save(out, "PNG")
print("saved:", out, img.size)
