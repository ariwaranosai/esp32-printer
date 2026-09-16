#!/usr/bin/env python3
"""Normalize large/phone photos without stretching. pip install Pillow"""
import argparse
from pathlib import Path
from PIL import Image, ImageOps

def prepare(source, output, fit='cover'):
    with Image.open(source) as raw:
        im=ImageOps.exif_transpose(raw).convert('RGBA')
        bg=Image.new('RGBA',im.size,'white');bg.alpha_composite(im);im=bg.convert('RGB')
        if fit=='cover':
            im=ImageOps.fit(im,(432,576),method=Image.Resampling.LANCZOS)
        else:
            fitted=ImageOps.contain(im,(432,576),method=Image.Resampling.LANCZOS)
            im=Image.new('RGB',(432,576),'white')
            im.paste(fitted,((432-fitted.width)//2,(576-fitted.height)//2))
        output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
        if output.suffix.lower() not in ('.bmp','.png','.jpg','.jpeg'):
            raise ValueError('Output must be BMP, PNG or JPEG')
        im.save(output)
    return output

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source');p.add_argument('output');p.add_argument('--fit',choices=['cover','contain'],default='cover')
    a=p.parse_args();print(prepare(a.source,a.output,a.fit))
