#!/usr/bin/env python3
"""GalEngineKit 图标生成器 — 程序化生成，原创设计"""

from PIL import Image, ImageDraw, ImageFont
import os
import math

def lerp_color(c1, c2, t):
    return tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3))

def generate_icon(size):
    """生成指定尺寸的图标"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # 颜色定义
    bg_top = (15, 12, 41)       # 深蓝黑
    bg_bottom = (48, 43, 99)     # 深蓝紫
    accent = (0, 212, 255)       # 青色强调
    accent2 = (139, 92, 246)     # 紫色强调
    white = (255, 255, 255)
    white_dim = (200, 200, 220)

    # 1. 圆角方形背景（垂直渐变）
    margin = max(1, size // 32)
    radius = size // 4
    for y in range(margin, size - margin):
        t = (y - margin) / (size - 2 * margin)
        color = lerp_color(bg_top, bg_bottom, t)
        # 绘制该行（考虑圆角）
        for x in range(margin, size - margin):
            # 圆角检测
            dx = min(x - margin, (size - margin - 1) - x)
            dy = min(y - margin, (size - margin - 1) - y)
            if dx < radius and dy < radius:
                if (radius - dx)**2 + (radius - dy)**2 > radius**2:
                    continue
            img.putpixel((x, y), color + (255,))

    # 2. 内部光泽（顶部高光）
    highlight = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    hl_draw = ImageDraw.Draw(highlight)
    hl_height = size // 3
    for y in range(margin + radius//2, margin + hl_height):
        t = (y - margin - radius//2) / hl_height
        alpha = int(40 * (1 - t))
        hl_draw.line([(margin + radius, y), (size - margin - radius, y)],
                     fill=(255, 255, 255, alpha))
    img = Image.alpha_composite(img, highlight)
    draw = ImageDraw.Draw(img)

    # 3. 字母 "G" — 粗体白色，居中
    try:
        font_size = int(size * 0.52)
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", font_size)
    except:
        font = ImageFont.load_default()

    text = "G"
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = (size - tw) // 2 - bbox[0]
    ty = (size - th) // 2 - bbox[1] - size // 20

    # G 的阴影
    draw.text((tx + size//64, ty + size//64), text, font=font, fill=(0, 0, 0, 100))
    # G 的主体
    draw.text((tx, ty), text, font=font, fill=white)

    # 4. 右下角工具标记 — 小方块阵列（表示工具箱/多引擎）
    grid_size = max(2, size // 32)
    grid_count = 3
    grid_total = grid_count * grid_size + (grid_count - 1) * max(1, size // 64)
    gx = size - margin - grid_total - size // 12
    gy = size - margin - grid_total - size // 12

    colors = [accent, accent2, white_dim,
              white_dim, accent, accent2,
              accent2, white_dim, accent]

    for i in range(grid_count):
        for j in range(grid_count):
            x = gx + j * (grid_size + max(1, size // 64))
            y = gy + i * (grid_size + max(1, size // 64))
            c = colors[i * grid_count + j]
            draw.rectangle([x, y, x + grid_size - 1, y + grid_size - 1], fill=c)

    # 5. 底部装饰线（渐变，青色→紫色）
    line_y = size - margin - size // 16
    line_x1 = margin + size // 6
    line_x2 = size - margin - size // 6
    line_width = max(1, size // 64)
    for x in range(line_x1, line_x2):
        t = (x - line_x1) / (line_x2 - line_x1)
        c = lerp_color(accent, accent2, t)
        draw.line([(x, line_y), (x, line_y + line_width)], fill=c)

    return img

def main():
    output_dir = os.path.dirname(os.path.abspath(__file__))
    ico_path = os.path.join(output_dir, "icon.ico")
    png_path = os.path.join(output_dir, "icon-256.png")

    # 生成多个尺寸
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = []
    for s in sizes:
        img = generate_icon(s)
        images.append(img)
        print(f"  生成 {s}x{s}")

    # 保存为 .ico — 直接构造标准 ICO 文件（PNG 压缩格式）
    # ICO = ICONDIR(6B) + ICONDIRENTRY[N](16B each) + PNG data[N]
    import struct
    import io

    png_datas = []
    for img in images:
        buf = io.BytesIO()
        img.save(buf, format='PNG')
        png_datas.append(buf.getvalue())

    count = len(images)
    header_size = 6 + 16 * count
    offsets = []
    offset = header_size
    for data in png_datas:
        offsets.append(offset)
        offset += len(data)

    with open(ico_path, 'wb') as f:
        # ICONDIR
        f.write(struct.pack('<HHH', 0, 1, count))  # reserved, type=icon, count
        # ICONDIRENTRY[]
        for i, img in enumerate(images):
            w = img.size[0] if img.size[0] < 256 else 0  # 0 = 256
            h = img.size[1] if img.size[1] < 256 else 0
            f.write(struct.pack('<BBBBHHII',
                w, h, 0, 0, 1, 32, len(png_datas[i]), offsets[i]))
        # PNG data[]
        for data in png_datas:
            f.write(data)

    print(f"\n图标已保存: {ico_path} ({os.path.getsize(ico_path)} bytes, {count} 个尺寸)")

    # 保存 256x256 PNG 预览
    images[-1].save(png_path, format='PNG')
    print(f"预览图已保存: {png_path}")

if __name__ == "__main__":
    main()
