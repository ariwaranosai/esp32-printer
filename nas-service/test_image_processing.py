import io
import unittest

from PIL import Image, ImageFilter, ImageOps
from app import enhance_photo, prepare_image, processing_settings


class ImageProcessingTests(unittest.TestCase):
    def test_invalid_settings_rejected(self):
        for value in ([], {"enabled": 1}, {"gamma": float("nan")},
                      {"contrast": float("inf")}, {"sharpness": True},
                      {"gamma": 0}, {"sharpness": 151}, {"unknown": 1}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                processing_settings(value)

    def test_disabled_matches_previous_pipeline_exactly(self):
        image = Image.linear_gradient("L").resize((900, 700)).convert("RGB")
        raw = io.BytesIO(); image.save(raw, "PNG")
        previous = io.BytesIO()
        ImageOps.fit(image, (432, 576), method=Image.Resampling.LANCZOS).save(
            previous, "JPEG", quality=95, subsampling=0, progressive=False)
        self.assertEqual(prepare_image(raw.getvalue(), processing={"enabled": False}),
                         previous.getvalue())

    def test_luma_edges_sharpen_without_color_cast(self):
        image = Image.new("L", (64, 64), 60)
        image.paste(190, (32, 0, 64, 64))
        image = image.filter(ImageFilter.GaussianBlur(0.7)).convert("RGB")
        result = enhance_photo(image, processing_settings({"gamma": 1, "contrast": 1}))
        old_edge = image.getpixel((32, 32))[0] - image.getpixel((31, 32))[0]
        new_edge = result.getpixel((32, 32))[0] - result.getpixel((31, 32))[0]
        self.assertGreater(new_edge, old_edge)
        self.assertTrue(all(max(pixel)-min(pixel) <= 1 for pixel in result.getdata()))

    def test_flat_noise_threshold_and_endpoints(self):
        for gray in (0, 80, 255):
            image = Image.new("RGB", (32, 32), (gray,)*3)
            result = enhance_photo(image, processing_settings())
            self.assertEqual(len(set(result.getdata())), 1)
            if gray in (0, 255): self.assertEqual(result.getpixel((0, 0)), (gray,)*3)

    def test_contain_border_stays_white_and_jpeg_compatible(self):
        raw = io.BytesIO(); Image.new("RGB", (1000, 600), (60, 80, 100)).save(raw, "PNG")
        image = Image.open(io.BytesIO(prepare_image(raw.getvalue(), "contain")))
        self.assertEqual(image.size, (432, 576))
        self.assertEqual(image.getpixel((0, 0)), (255,)*3)
        self.assertFalse(image.info.get("progressive"))
        self.assertEqual(dict(image.getexif()), {})


if __name__ == "__main__":
    unittest.main()
