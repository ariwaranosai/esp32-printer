"""Exercise production C++ decoder/renderer, including changed photo rectangle."""
from pathlib import Path
import subprocess,sys
from PIL import Image,ImageOps
sys.path.insert(0,str(Path('tools').resolve()))
from prepare_photo import prepare
D=Path('host-build/fixtures');D.mkdir(exist_ok=True)
exe=str(Path('host-build/test_frame').resolve())
def render(p,fit='cover',ui='none',ok=True):
    out=D/(p.stem+'-'+fit+'.ppm')
    r=subprocess.run([exe,str(p),str(out),fit,ui],capture_output=True)
    assert (r.returncode==0)==ok,(p,r.stderr.decode())
    assert b'AddressSanitizer' not in r.stderr and b'runtime error:' not in r.stderr,r.stderr
    return Image.open(out).copy() if ok else None
for size in [(800,480),(480,800),(432,576),(431,575),(1,1),(736,1325),(1350,1350)]:
    for ext in ['png','jpg','bmp']:
        p=D/f'{size[0]}x{size[1]}.{ext}';Image.new('RGB',size,(255,0,0)).save(p)
        for fit in ['cover','contain']:
            out=render(p,fit);assert out.size==(480,800)
            assert out.getpixel((240,480))==(255,0,0)
            for xy in [(23,140),(456,140),(24,139),(24,716)]:assert out.getpixel(xy)==(255,255,255)
            if fit=='cover':assert out.getpixel((24,140))==(255,0,0) and out.getpixel((455,715))==(255,0,0)
# Large input rejected before decoding; malformed/truncated input must not crash.
for size in [(4097,1),(2000,2000)]:
    p=D/f'oversize-{size[0]}.png';Image.new('RGB',size).save(p);render(p,ok=False)
for name,data in [('empty.png',b''),('invalid.bmp',b'BM'+b'\xff'*90),('truncated.jpg',(D/'480x800.jpg').read_bytes()[:100]),('bad-exif.jpg',b'\xff\xd8\xff\xe1\x00\x16Exif\x00\x00II\x2a\x00\xff\xff\xff\xff'+b'\x00'*8)]:
    p=D/name;p.write_bytes(data);render(p,ok=False)
p=D/'alpha.png';Image.new('RGBA',(432,576),(255,0,0,0)).save(p);assert render(p).getpixel((100,300))==(255,255,255)
# Additional channel layouts and progressive JPEG exercise decoder output-channel handling.
for mode, color in [('L', 200), ('LA', (0, 0)), ('RGBA', (0, 0, 255, 128))]:
    p=D/f'channels-{mode}.png';Image.new(mode,(431,575),color).save(p);render(p)
p=D/'palette-alpha.png';pal=Image.new('P',(43,57));pal.putpalette([255,0,0]+[0]*765);pal.save(p,transparency=0)
assert render(p).getpixel((240,480))==(255,255,255)
p=D/'progressive.jpg';Image.new('RGB',(800,480),'blue').save(p,progressive=True);render(p)
p=D/'file-limit.png';p.write_bytes((D/'432x576.png').read_bytes()+bytes(8*1024*1024));render(p,ok=False)
# EXIF: compare device path with Pillow's physically oriented version, all 8 orientations.
base=Image.new('RGB',(80,60),'red');base.paste('blue',(40,0,80,30));base.paste('green',(0,30,40,60));base.paste('yellow',(40,30,80,60))
for orientation in range(1,9):
    exif=Image.Exif();exif[274]=orientation;p=D/f'exif-{orientation}.jpg';base.save(p,exif=exif,quality=100,subsampling=0)
    with Image.open(p) as source:
        ref=D/f'reference-{orientation}.png';ImageOps.exif_transpose(source).save(ref)
    # ref PNG inherits EXIF but pixel orientation is already normalized by Pillow.
    assert render(p).tobytes()==render(ref).tobytes()
    prepared=D/f'prepared-{orientation}.bmp';prepare(p,prepared,overwrite=True);assert Image.open(prepared).size==(432,576)
# Native photo asset must never add another header or mutate the UI area.
render(Path('sdcard/photos/sample.png'),ui='ui').save('host-build/firmware-preview.png')
print('Image compatibility passed: old/new sizes, JPG/PNG/BMP, alpha, EXIF 1-8, malformed/oversize input, cover/contain.')
