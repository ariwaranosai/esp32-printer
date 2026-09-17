"""Generate Noto Sans SC glyphs; 18px includes basic CJK for IP-located cities."""
import sys
from PIL import Image, ImageDraw, ImageFont
ui = ('°·…温度湿更新于室内环境等待校时请配置网络间未同步放入照片'
      '户外天气采样每分钟小时无数据离线暂无获取失败未知定位气象风力'
      '晴多云阴少雨雪雷阵伴有冰雹小中大暴特强极冻夹沙尘扬浮雾霾飑龙卷'
      '轻浓重毛细转到局部零星东南西北微旋向静不持续级杭州北京上海')
base = set(chr(c) for c in range(32, 127)) | set(ui)
data=bytearray();glyphs=[]
for size in [16,18,25,32,46]:
    font=ImageFont.truetype(sys.argv[1],size)
    font.set_variation_by_axes([700 if size in (25,32) else 500])
    chars = base | ({chr(c) for c in range(0x4e00, 0xa000)} if size == 18 else set())
    if size == 46:
        chars = set('0123456789.-°C?')
    for ch in sorted(chars):
        advance=round(font.getlength(ch));w=max(advance,1);h=size+3
        im=Image.new('L',(w,h));ImageDraw.Draw(im).text((0,round(size*.9)),ch,font=font,fill=255,anchor='ls')
        # Shared baseline keeps decimal points and punctuation at their proper height.
        bits=[v>127 for v in im.getdata()];offset=len(data)
        for i in range(0,len(bits),8):data.append(sum(int(v)<<(7-j) for j,v in enumerate(bits[i:i+8])))
        glyphs.append((ord(ch),size,w,h,advance,offset))
with open(sys.argv[2],'w') as f:
    f.write('// Generated from Noto Sans SC (SIL OFL 1.1); see third_party/NotoSansSC-OFL.txt\n#pragma once\n#include <cstdint>\nstruct Glyph {uint32_t cp;int size,w,h,advance;unsigned offset;};\ninline constexpr uint8_t font_bits[]={\n')
    for i in range(0,len(data),24):f.write(','.join(str(x) for x in data[i:i+24])+',\n')
    f.write('};\ninline constexpr Glyph glyphs[]={\n')
    for g in glyphs:f.write('{'+','.join(map(str,g))+'},\n')
    f.write('};\n')
print(f'{len(glyphs)} glyphs, {len(data)} bitmap bytes')
