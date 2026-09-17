"""Render a UI proposal with illustrative data, without changing firmware."""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs' / 'weather-ui-preview.png'
FONT = 'C:/Windows/Fonts/msyh.ttc'
BOLD = 'C:/Windows/Fonts/msyhbd.ttc'


def render():
    canvas = Image.new('RGB', (480, 800), 'white')
    draw = ImageDraw.Draw(canvas)

    def text(x, y, value, size, bold=False, right=False):
        font = ImageFont.truetype(BOLD if bold else FONT, size)
        bounds = font.getbbox(value)
        width = bounds[2] - bounds[0]
        mask = Image.new('L', (width + 4, bounds[3] - bounds[1] + 4))
        ImageDraw.Draw(mask).text((2 - bounds[0], 2 - bounds[1]), value, font=font, fill=255)
        mask = mask.point(lambda p: 255 if p >= 128 else 0)
        canvas.paste('black', ((x - width if right else x) - 2, y - 2), mask)

    text(24, 22, '杭州 · 户外天气', 19)
    # Small battery and Wi-Fi status, secondary to the weather.
    draw.rounded_rectangle((422, 24, 451, 36), radius=2, outline='black', width=2)
    draw.rectangle((451, 28, 454, 32), fill='black')
    draw.rectangle((426, 28, 443, 32), fill='black')
    for r in (5, 10, 15):
        draw.arc((387-r, 39-r, 387+r, 39+r), 220, 320, fill='black', width=2)
    draw.ellipse((385, 36, 388, 39), fill='black')

    # Partly cloudy icon in the panel's native colors.
    cx, cy = 54, 70
    for angle in range(0, 360, 45):
        a = math.radians(angle)
        draw.line((cx + 21*math.cos(a), cy + 21*math.sin(a),
                   cx + 27*math.cos(a), cy + 27*math.sin(a)), fill='black', width=2)
    draw.ellipse((cx-15, cy-15, cx+15, cy+15), fill=(255,255,0), outline='black', width=2)
    cloud = [(27, 83), (30, 76), (36, 73), (42, 74), (45, 67), (52, 64),
             (60, 67), (65, 74), (72, 74), (79, 80), (79, 87), (74, 92), (33, 92)]
    draw.polygon(cloud, fill='white')
    draw.line(cloud + [cloud[0]], fill='black', width=2, joint='curve')
    text(94, 58, '多云', 32, bold=True)
    text(456, 51, '26°C', 46, right=True)
    text(24, 107, '东北风 2级', 16)
    text(456, 107, '气象 08:20 更新', 16, right=True)

    with Image.open(ROOT / 'sdcard/photos/00-landscape.png') as photo:
        assert photo.size == (432, 576)
        canvas.paste(photo.convert('RGB'), (24, 140))

    text(24, 738, '室内', 18)
    text(77, 735, '30.6°C', 25, bold=True)
    text(204, 738, '湿度', 18)
    text(250, 735, '40%', 25, bold=True)
    text(24, 775, '采样于 08:30', 16)
    text(456, 775, '每10分钟更新', 16, right=True)
    canvas.save(OUT)

    palette = Image.new('P', (1, 1))
    colors = [0,0,0, 255,255,255, 255,255,0, 255,0,0, 0,0,255, 0,160,0]
    palette.putpalette(colors + [0,0,0] * (256-6))
    canvas.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG).convert('RGB').save(
        OUT.with_name('weather-ui-six-color.png'))
    print(OUT)


if __name__ == '__main__':
    render()
