"""Generate the checked-in 1bpp Noto Sans SC subset. Usage: script FONT.ttf OUTPUT.h"""
import sys
from PIL import Image, ImageDraw, ImageFont
chars=sorted(set('0123456789: /%.°-温度湿星期日一二三四五六更新于室内环境等待校时请配置网络间未同步放入照片SDphotos'))
data=bytearray();glyphs=[]
for size in [14,18,22,23,65]:
    font=ImageFont.truetype(sys.argv[1],size)
    font.set_variation_by_axes([700 if size in (22,65) else 500])
    for ch in (chars if size!=65 else list('0123456789:-')):
        advance=round(font.getlength(ch));w=max(advance,1);h=size+3
        im=Image.new('L',(w,h));ImageDraw.Draw(im).text((0,0),ch,font=font,fill=255,anchor='lt')
        # Keep consistent top alignment; shared bitmap height also includes descenders.
        bits=[v>127 for v in im.getdata()];offset=len(data)
        for i in range(0,len(bits),8):data.append(sum(int(v)<<(7-j) for j,v in enumerate(bits[i:i+8])))
        glyphs.append((ord(ch),size,w,h,advance,offset))
with open(sys.argv[2],'w') as f:
    f.write('// Generated from Noto Sans SC (SIL OFL 1.1); see third_party/NotoSansSC-OFL.txt\n#pragma once\n#include <cstdint>\nstruct Glyph {uint32_t cp;int size,w,h,advance;unsigned offset;};\ninline constexpr uint8_t font_bits[]={\n')
    for i in range(0,len(data),24):f.write(','.join(str(x) for x in data[i:i+24])+',\n')
    f.write('};\ninline constexpr Glyph glyphs[]={\n')
    for g in glyphs:f.write('{'+','.join(map(str,g))+'},\n')
    f.write('};\n')
