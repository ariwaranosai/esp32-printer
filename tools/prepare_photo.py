#!/usr/bin/env python3
"""Convert one image or a directory to PhotoPainter's 432x576 photo area."""
import argparse
import os
from pathlib import Path
import sys
import shutil
import tempfile

from PIL import Image, ImageOps

SIZE = (432, 576)
FORMATS = {'.png': 'PNG', '.jpg': 'JPEG', '.jpeg': 'JPEG', '.bmp': 'BMP'}


def prepare(source, output, fit='cover', *, overwrite=False):
    """Write one RGB photo safely; never replace the original source."""
    source, output = Path(source), Path(output)
    if fit not in ('cover', 'contain'):
        raise ValueError('fit must be cover or contain')
    file_format = FORMATS.get(output.suffix.lower())
    if file_format is None:
        raise ValueError('Output must be PNG, JPEG or BMP')
    if source.resolve() == output.resolve() or (output.exists() and source.samefile(output)):
        raise ValueError('Output must not replace the source image')
    if output.exists() and not overwrite:
        raise FileExistsError(f'Output already exists: {output}')
    with Image.open(source) as raw:
        # Animated inputs intentionally contribute only their first frame.
        im = ImageOps.exif_transpose(raw).convert('RGBA')
        bg = Image.new('RGBA', im.size, 'white')
        bg.alpha_composite(im)
        im = bg.convert('RGB')
        if fit == 'cover':
            im = ImageOps.fit(im, SIZE, method=Image.Resampling.LANCZOS)
        else:
            fitted = ImageOps.contain(im, SIZE, method=Image.Resampling.LANCZOS)
            im = Image.new('RGB', SIZE, 'white')
            im.paste(fitted, ((SIZE[0] - fitted.width) // 2, (SIZE[1] - fitted.height) // 2))
        # Strip orientation/metadata so the device cannot rotate a normalized image again.
        im.info.clear()
        output.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix='.photopainter-', dir=output.parent)
        os.close(fd)
        try:
            options = {'quality': 95, 'subsampling': 0, 'progressive': False} if file_format == 'JPEG' else {}
            im.save(temporary, format=file_format, **options)
            if overwrite:
                os.replace(temporary, output)
            else:
                # Exclusive creation also works on FAT32 SD cards (which lack hard links).
                dst = output.open('xb')
                try:
                    with open(temporary, 'rb') as src, dst:
                        shutil.copyfileobj(src, dst)
                except BaseException:
                    output.unlink(missing_ok=True)
                    raise
        finally:
            Path(temporary).unlink(missing_ok=True)
    return output


def directory_jobs(source, output, extension, recursive):
    """Snapshot input first; flatten subdirectories for firmware's nonrecursive scanner."""
    if source.resolve() == output.resolve():
        raise ValueError('Use a separate output directory')
    if output.exists() and not output.is_dir():
        raise ValueError('Directory input requires an output directory')
    supported = {suffix.lower() for suffix, fmt in Image.registered_extensions().items() if fmt in Image.OPEN}
    nested_output = output.resolve().is_relative_to(source.resolve())
    iterator = source.rglob('*') if recursive else source.iterdir()
    files = sorted((p for p in iterator if p.is_file() and not p.is_symlink()
                    and p.suffix.lower() in supported
                    and not (nested_output and p.resolve().is_relative_to(output.resolve()))),
                   key=lambda p: (str(p.relative_to(source)).casefold(), str(p)))
    jobs, used = [], set()
    for path in files:
        # Case-folded names also prevent collisions on FAT32 and default macOS volumes.
        stem = path.stem
        name, index = stem + extension, 2
        while name.casefold() in used:
            name = f'{stem}-{index}{extension}'
            index += 1
        used.add(name.casefold())
        jobs.append((path, output / name))
    return jobs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Image file or input directory')
    parser.add_argument('output', type=Path, nargs='?', help='Output file/directory (default: sibling *_prepared directory)')
    parser.add_argument('--format', choices=['png', 'jpg', 'jpeg', 'bmp'], help='Output format; default PNG, or inferred from an explicit output filename')
    parser.add_argument('--fit', choices=['cover', 'contain'], default='cover', help='cover: crop to fill; contain: preserve whole photo with white margins')
    parser.add_argument('--recursive', action='store_true', help='Include subdirectories; flatten output for the device')
    parser.add_argument('--overwrite', action='store_true', help='Replace existing outputs, never originals')
    args = parser.parse_args(argv)
    source = args.source
    if not source.exists():
        parser.error(f'Input does not exist: {source}')
    extension = '.' + (args.format or 'png')
    output = args.output or source.with_name(source.stem + '_prepared')
    try:
        if source.is_dir():
            jobs = directory_jobs(source, output, extension, args.recursive)
        elif source.is_file():
            if args.recursive:
                parser.error('--recursive requires a directory input')
            if args.output is None or output.is_dir() or not output.suffix:
                output = output / (source.stem + extension)
            elif args.format and FORMATS.get(output.suffix.lower()) != FORMATS[extension]:
                parser.error('--format conflicts with the output filename extension')
            jobs = [(source, output)]
        else:
            parser.error('Input must be a regular image file or directory')
    except ValueError as error:
        parser.error(str(error))
    if not jobs:
        print('No supported images found.', file=sys.stderr)
        return 1
    converted = skipped = failed = 0
    for source, output in jobs:
        try:
            prepare(source, output, args.fit, overwrite=args.overwrite)
        except FileExistsError:
            skipped += 1
            print(f'SKIP {output} (exists; use --overwrite)')
        except (OSError, ValueError, Image.DecompressionBombError) as error:
            failed += 1
            print(f'ERROR {source}: {error}', file=sys.stderr)
        else:
            converted += 1
            print(f'OK {source} -> {output}')
    print(f'432x576 RGB | converted={converted} skipped={skipped} failed={failed}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
