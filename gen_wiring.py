# -*- coding: utf-8 -*-
"""生成 F407VET6 新核心板系统接线总图（内联 SVG，2400 宽）"""
import sys
sys.stdout.reconfigure(encoding='utf-8')

W, H = 2400, 1750

def rect(x, y, w, h, fill, stroke, rx=10, sw=2):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>'

def text(x, y, s, size=22, fill="#1A1B1C", anchor="start", bold=False, family="Microsoft YaHei"):
    w = "font-weight:bold;" if bold else ""
    return f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" style="font-family:\'{family}\',sans-serif;{w}">{s}</text>'

def line(x1, y1, x2, y2, color, w=3, dash=""):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{w}"{d}/>'

def module(x, y, w, h, title, lines, color="#E8F1FB", title_fill="#1A5F9E"):
    s = rect(x, y, w, h, color, title_fill, sw=2)
    s += text(x+w/2, y+34, title, 24, title_fill, "middle", True)
    s += line(x, y+46, x+w, y+46, title_fill, 2)
    yy = y + 78
    for ln in lines:
        s += text(x+22, yy, ln, 20, "#222")
        yy += 42
    return s

svg = []
svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
svg.append(rect(0, 0, W, H, "#FAFAF7", "#D8D5CC", rx=0, sw=1))

# 标题
svg.append(text(W/2, 64, "SMART-M · 基于 STM32F407VET6 的工业电机运行状态监测与远程预警系统 — 系统接线总图", 34, "#12233D", "middle", True))
svg.append(text(W/2, 100, "F407VET6 新核心板版（LQFP100）｜软I2C总线1=PB8/PB9｜软I2C总线2=PD6/PD7｜USART3=PB10/PB11 外接ESP8266", 20, "#5A6472", "middle"))

# ============ 核心板 ============
cx, cy, cw, ch = 950, 430, 500, 900
svg.append(rect(cx, cy, cw, ch, "#FFFFFF", "#12233D", rx=16, sw=3))
svg.append(text(cx+cw/2, cy+46, "STM32F407VET6 核心板", 28, "#12233D", "middle", True))
svg.append(text(cx+cw/2, cy+78, "LQFP100 · 168MHz · 512KB/192KB", 18, "#5A6472", "middle"))

# 核心板内部引脚组（左/右两侧标签）
left_pins = [
    ("总线1 SCL", "PB8"), ("总线1 SDA", "PB9"),
    ("USART1_TX(调试)", "PA9"), ("USART1_RX(调试)", "PA10"),
    ("SWDIO", "PA13"), ("SWCLK", "PA14"),
]
right_pins = [
    ("总线2 SCL(OLED)", "PD6"), ("总线2 SDA(OLED)", "PD7"),
    ("USART3_TX(ESP8266)", "PB10"), ("USART3_RX(ESP8266)", "PB11"),
    ("继电器 IN", "PA4"), ("蜂鸣器 I/O", "PA5"),
    ("LED2/LED3", "PA6/PA7"),
]
lp_y = cy + 140
for name, pin in left_pins:
    svg.append(rect(cx-260, lp_y-24, 250, 40, "#EFF6EC", "#2E7D32", rx=8, sw=1))
    svg.append(text(cx-252, lp_y+2, name, 17, "#333"))
    svg.append(text(cx-30, lp_y+2, pin, 19, "#2E7D32", "end", True))
    lp_y += 78
rp_y = cy + 140
for name, pin in right_pins:
    svg.append(rect(cx+cw+10, rp_y-24, 250, 40, "#FFF3E0", "#B26A00", rx=8, sw=1))
    svg.append(text(cx+cw+20, rp_y+2, name, 17, "#333"))
    svg.append(text(cx+cw+248, rp_y+2, pin, 19, "#B26A00", "end", True))
    rp_y += 78

# 核心板内框：矩阵键盘引脚（右下）
svg.append(text(cx+cw/2, cy+ch-220, "矩阵键盘（4×4）", 20, "#12233D", "middle", True))
svg.append(text(cx+cw/2, cy+ch-185, "行 R1-R4: PE2 / PE6 / PE7 / PE10", 19, "#444", "middle"))
svg.append(text(cx+cw/2, cy+ch-148, "列 C1-C4: PE8 / PE9 / PE11 / PE12", 19, "#444", "middle"))
svg.append(text(cx+cw/2, cy+ch-105, "调试: USART1 PA9/PA10 ｜ SWD: PA13/PA14", 17, "#888", "middle"))
svg.append(text(cx+cw/2, cy+ch-70, "预留: HC-SR04 Trig=PC11 / Echo=PE5（8外设版未启用）", 16, "#AAA", "middle"))

# ============ 外设模块 ============
# 左列：总线1 三传感器
m_sht = module(110, 330, 300, 240, "SHT30 温湿度", ["VCC→3.3V   GND→GND", "SCL→PB8 (总线1)", "SDA→PB9 (总线1)", "ADDR→GND (0x44)"], "#E8F1FB", "#1A5F9E")
m_qmi = module(110, 640, 300, 250, "QMI8658 六轴", ["VCC→3.3V   GND→GND", "SCL→PB8 (总线1)", "SDA→PB9 (总线1)", "AD0→板上固定GND(0x6B)", "INT1/INT2 悬空"], "#E8F1FB", "#1A5F9E")
m_ina = module(110, 950, 300, 280, "INA226 电流", ["VCC→3.3V   GND→GND", "SCL→PB8 (总线1)", "SDA→PB9 (总线1)", "A0/A1→GND (0x40)", "IN+/IN-→0.1Ω采样电阻", "VBUS→电机母线"], "#E8F1FB", "#1A5F9E")
svg.append(m_sht + m_qmi + m_ina)

# 右列：OLED / 继电器 / 蜂鸣器
m_oled = module(1990, 330, 300, 210, "SSD1306 OLED 0.96\"", ["VCC→3.3V   GND→GND", "SCL→PD6 (总线2)", "SDA→PD7 (总线2)", "地址 0x3C"], "#F3E8FB", "#7B1FA2")
m_relay = module(1990, 600, 300, 240, "继电器（断电保护）", ["VCC→5V   GND→GND", "IN→PA4 (高电平吸合)", "COM/NO→串电机回路", "光耦隔离"], "#FDECEA", "#B71C1C")
m_beep = module(1990, 900, 300, 210, "有源蜂鸣器（外接）", ["+→PA5（新板无板载）", "-→GND", "VCC按模块额定 3.3/5V", "高电平响"], "#FFF8E1", "#E65100")
svg.append(m_oled + m_relay + m_beep)

# 上方：ESP8266
m_esp = module(900, 150, 560, 210, "ESP8266-01S WiFi（外接，USART3）", ["VCC→独立3.3V（AMS1117单独一路，≥300mA）", "GND→GND 共地", "TXD→PB11 (USART3_RX)", "RXD→PB10 (USART3_TX)", "CH_PD/EN→3.3V(10k上拉)  GPIO0→10k上拉"], "#E0F2F1", "#00695C")
svg.append(m_esp)

# 下方：矩阵键盘 + 调试口
m_key = module(900, 1430, 560, 180, "4×4 矩阵键盘", ["行 R1-R4 → PE2 / PE6 / PE7 / PE10（推挽输出）", "列 C1-C4 → PE8 / PE9 / PE11 / PE12（上拉输入）", "避开板载 KEY0=PE4 / KEY1=PE3 / Echo=PE5"], "#E8F5E9", "#33691E")
svg.append(m_key)

# ============ 连线 ============
BLUE1, BLUE2, GREEN, YELLOW, RED, BLACK = "#1E88E5", "#8E24AA", "#00897B", "#F9A825", "#D32F2F", "#424242"

# 总线1：PB8/PB9 核心板左侧 → 三传感器（串联）
svg.append(line(cx-10, cy+170, cx-360, cy+170, BLUE1, 4))          # 主干横线
svg.append(line(cx-360, cy+170, cx-360, 450, BLUE1, 4))
svg.append(line(cx-360, cy+170, cx-360, 760, BLUE1, 4))
svg.append(line(cx-360, cy+170, cx-360, 1070, BLUE1, 4))
# 接入各模块
svg.append(line(410, 450, 410, 450, BLUE1, 1))
svg.append(line(410, 450, 300, 450, BLUE1, 3))   # SHT30 左侧→SCL
svg.append(line(410, 480, 300, 480, BLUE1, 3))   # SDA
svg.append(line(410, 760, 300, 760, BLUE1, 3))
svg.append(line(410, 790, 300, 790, BLUE1, 3))
svg.append(line(410, 1070, 300, 1070, BLUE1, 3))
svg.append(line(410, 1100, 300, 1100, BLUE1, 3))
# 总线1 标签
svg.append(text(cx-365, cy+150, "总线1: PB8=SCL / PB9=SDA（4.7kΩ上拉×2）", 20, BLUE1, "start", True))
svg.append(text(420, 445, "SCL", 16, BLUE1))
svg.append(text(420, 485, "SDA", 16, BLUE1))
svg.append(text(420, 755, "SCL", 16, BLUE1))
svg.append(text(420, 795, "SDA", 16, BLUE1))
svg.append(text(420, 1065, "SCL", 16, BLUE1))
svg.append(text(420, 1105, "SDA", 16, BLUE1))

# 总线2：PD6/PD7 → OLED（右侧）
svg.append(line(cx+cw+10, cy+170, cx+cw+430, cy+170, BLUE2, 4))
svg.append(line(cx+cw+430, cy+170, cx+cw+430, 450, BLUE2, 4))
svg.append(line(cx+cw+430, 450, 1990, 450, BLUE2, 3))
svg.append(line(cx+cw+430, 480, 1990, 480, BLUE2, 3))
svg.append(text(cx+cw+435, cy+150, "总线2(OLED): PD6=SCL / PD7=SDA", 20, BLUE2, "start", True))
svg.append(text(cx+cw+440, 445, "SCL", 16, BLUE2))
svg.append(text(cx+cw+440, 485, "SDA", 16, BLUE2))

# USART3：PB10/PB11 → ESP8266
svg.append(line(cx+cw+10, cy+290, cx+cw+330, cy+290, GREEN, 4))
svg.append(line(cx+cw+330, cy+290, cx+cw+330, cy+70, GREEN, 4))
svg.append(line(cx+cw+330, cy+70, 1120, cy+70, GREEN, 4))
svg.append(line(1120, cy+70, 1120, 150, GREEN, 4))
svg.append(text(cx+cw+340, cy+270, "USART3 115200+DMA（绿）", 19, GREEN, "start", True))
svg.append(text(1135, cy+40, "TXD→PB11  /  RXD←PB10", 18, GREEN, "start"))

# 继电器 PA4（黄）
svg.append(line(cx+cw+10, cy+470, cx+cw+230, cy+470, YELLOW, 4))
svg.append(line(cx+cw+230, cy+470, cx+cw+230, 720, YELLOW, 4))
svg.append(line(cx+cw+230, 720, 1990, 720, YELLOW, 3))
svg.append(text(cx+cw+240, cy+450, "PA4 高电平吸合（黄）", 19, YELLOW, "start", True))

# 蜂鸣器 PA5（黄）
svg.append(line(cx+cw+10, cy+545, cx+cw+300, cy+545, YELLOW, 4))
svg.append(line(cx+cw+300, cy+545, cx+cw+300, 1010, YELLOW, 4))
svg.append(line(cx+cw+300, 1010, 1990, 1010, YELLOW, 3))
svg.append(text(cx+cw+310, cy+525, "PA5 高电平响（黄）", 19, YELLOW, "start", True))

# 矩阵键盘连线（黄，核心板下方）
svg.append(line(cx+120, cy+ch-10, cx+120, 1430, YELLOW, 3))
svg.append(line(cx+240, cy+ch-10, cx+240, 1430, YELLOW, 3))
svg.append(text(cx+140, cy+ch+30, "行/列 GPIO（黄）", 18, YELLOW, "start", True))

# 调试串口（绿，左下角标注）
svg.append(line(cx-10, cy+230, cx-330, cy+230, GREEN, 3))
svg.append(text(cx-345, cy+215, "USART1 PA9/PA10 → 板载USB转串口（调试 printf）", 18, GREEN, "start", True))

# ============ 电机电源回路（底部中央） ============
py = 1330
svg.append(rect(110, 1180, 760, 180, "#FFF8F8", "#C62828", rx=12, sw=2))
svg.append(text(200, 1222, "电机驱动电源回路", 24, "#C62828", "start", True))
svg.append(text(140, 1268, "12V 适配器 ──▶ 继电器 COM/NO ──▶ 电机（供电回路）", 20, "#333"))
svg.append(text(140, 1308, "INA226 采样电阻(0.1Ω) 串入该回路；告警时继电器断开 = 电机断电保护", 18, "#666"))
svg.append(rect(1990, 1180, 300, 180, "#FFF8F8", "#C62828", rx=12, sw=2))
svg.append(text(2060, 1222, "电源架构", 24, "#C62828", "start", True))
svg.append(text(2020, 1268, "12V → 5V(MP1584/", 18, "#333"))
svg.append(text(2020, 1300, "AMS1117-5.0)", 18, "#333"))
svg.append(text(2020, 1332, "5V → 核心板 / 继电器 / 独立AMS1117-3.3 → ESP8266", 17, "#333"))

# ============ 图例 ============
lx, ly = 110, 1610
svg.append(rect(lx, ly, 2180, 110, "#FFFFFF", "#B0BEC5", rx=12, sw=1))
svg.append(text(lx+30, ly+42, "图例：", 22, "#12233D", "start", True))
leg = [("红线=VCC 电源", RED), ("黑线=GND 地", BLACK), ("蓝实线=I2C 总线1（PB8/PB9）", BLUE1), ("紫虚线=I2C 总线2（PD6/PD7）", BLUE2), ("绿线=UART 串口", GREEN), ("黄线=GPIO 控制", YELLOW)]
lx2 = lx + 120
for t, c in leg:
    svg.append(line(lx2, ly+40, lx2+46, ly+40, c, 5))
    svg.append(text(lx2+56, ly+46, t, 19, "#333"))
    lx2 += 350

# 底部说明
svg.append(text(W/2, H-30, "SMART-M · 引脚权威依据：《器件引脚与功能配置总表.md》｜2026-09-08", 17, "#8A93A0", "middle"))

svg.append('</svg>')
html = f"""<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">
<title>SMART-M 系统接线总图（F407VET6 新核心板版）</title>
<style>body{{margin:0;background:#FAFAF7;}}svg{{display:block;}}</style></head>
<body>{''.join(svg)}</body></html>"""

open(r'D:\STM32kdl\SMART-M\文档\系统接线总图.html', 'w', encoding='utf-8').write(html)
print('html OK')
