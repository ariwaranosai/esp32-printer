"""CLI conversion tests; every successful output is decoded by production firmware code."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from PIL import Image

SCRIPT = Path('tools/prepare_photo.py').resolve()
DECODER = Path('host-build/test_frame').resolve()


class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / 'input'
        self.source.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def image(self, name='a.png', mode='RGB', color='red', size=(800, 480)):
        path = self.source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        Image.new(mode, size, color).save(path)
        return path

    def cli(self, *args, status=0):
        result = subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode, status, result.stdout + result.stderr)
        return result

    def check_output(self, path, fmt=None):
        with Image.open(path) as im:
            self.assertEqual(im.size, (456, 656))
            self.assertEqual(im.mode, 'RGB')
            self.assertNotIn(274, im.getexif())
            if fmt:
                self.assertEqual(im.format, fmt)
        result = subprocess.run([str(DECODER), str(path), str(self.root / 'preview.ppm'), 'cover', 'none'], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_single_all_formats_and_original_preserved(self):
        source = self.image()
        original = source.read_bytes()
        for extension, fmt in [('png', 'PNG'), ('jpg', 'JPEG'), ('bmp', 'BMP')]:
            output = self.root / f'photo.{extension}'
            self.cli(source, output)
            self.check_output(output, fmt)
        self.assertEqual(source.read_bytes(), original)

    def test_directory_recursive_flatten_and_collisions(self):
        self.image('a.jpg')
        self.image('a.png')
        self.image('nested/a.png')
        self.image('other/A.png')
        (self.source / 'notes.txt').write_text('ignored')
        output = self.root / 'converted'
        result = self.cli(self.source, output, '--recursive', '--format', 'bmp')
        self.assertIn('converted=4', result.stdout)
        paths = list(output.glob('*.bmp'))
        self.assertEqual(len(paths), 4)
        self.assertEqual(len({p.name.casefold() for p in paths}), 4)
        for p in paths:
            self.check_output(p, 'BMP')

    def test_default_shallow_and_default_destination(self):
        self.image()
        self.image('nested/b.png')
        self.cli(self.source)
        paths = list((self.root / 'input_prepared').glob('*.png'))
        self.assertEqual(len(paths), 1)
        self.check_output(paths[0])

    def test_nested_output_excluded_and_existing_skipped(self):
        self.image()
        output = self.source / 'converted'
        self.cli(self.source, output, '--recursive')
        old = (output / 'a.png').read_bytes()
        result = self.cli(self.source, output, '--recursive')
        self.assertIn('converted=0 skipped=1', result.stdout)
        self.assertEqual((output / 'a.png').read_bytes(), old)
        self.image(color='blue')
        self.cli(self.source, output, '--recursive', '--overwrite')
        self.assertNotEqual((output / 'a.png').read_bytes(), old)
        self.check_output(output / 'a.png')

    def test_corrupt_image_does_not_stop_batch(self):
        self.image()
        (self.source / 'broken.jpg').write_bytes(b'not an image')
        output = self.root / 'converted'
        result = self.cli(self.source, output, status=1)
        self.assertIn('converted=1 skipped=0 failed=1', result.stdout)
        self.assertFalse((output / 'broken.png').exists())
        self.assertFalse(list(output.glob('.photopainter-*')))
        self.check_output(output / 'a.png')

    def test_alpha_contain_and_exif(self):
        source = self.image(mode='RGBA', color=(255, 0, 0, 0))
        output = self.root / 'alpha.png'
        self.cli(source, output, '--fit', 'contain')
        with Image.open(output) as im:
            self.assertEqual(im.getpixel((200, 300)), (255, 255, 255))
        self.check_output(output)
        source = self.image('rotated.jpg', size=(100, 50))
        exif = Image.Exif()
        exif[274] = 6
        Image.new('RGB', (100, 50), 'red').save(source, exif=exif)
        output = self.root / 'rotated.png'
        self.cli(source, output, '--fit', 'contain')
        with Image.open(output) as im:
            self.assertEqual(im.getpixel((24, 288)), (255, 255, 255))
            self.assertEqual(im.getpixel((216, 288)), (254, 0, 0))
        self.check_output(output)

    def test_reject_inplace_and_conflicting_format(self):
        source = self.image()
        original = source.read_bytes()
        self.cli(source, source, '--overwrite', status=1)
        self.assertEqual(source.read_bytes(), original)
        self.cli(self.source, self.source, status=2)
        self.cli(source, self.root / 'out.bmp', '--format', 'png', status=2)
        self.cli(source, self.root / 'out.gif', status=1)
        self.cli(self.root / 'missing', status=2)

    def test_single_directory_output_and_parent_output(self):
        source = self.image()
        output = self.root / 'out'
        self.cli(source, output, '--format', 'bmp')
        self.check_output(output / 'a.bmp')
        self.cli(self.source, self.root, '--format', 'png')
        self.check_output(self.root / 'a.png')


if __name__ == '__main__':
    unittest.main()
