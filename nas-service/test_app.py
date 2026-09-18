import io
import json
import tempfile
import threading
import unittest
from wsgiref.simple_server import make_server, WSGIRequestHandler
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from PIL import Image
from app import create_app, prepare_image, ServiceError
from smoke_test import check


def jpeg(color="red", size=(1000, 800), orientation=None):
    out = io.BytesIO()
    image = Image.new("RGB", size, color)
    exif = Image.Exif()
    if orientation:
        exif[274] = orientation
    image.save(out, "JPEG", exif=exif)
    return out.getvalue()


class FakeNAS(BaseHTTPRequestHandler):
    calls = []
    mode = "ok"

    def log_message(self, *_):
        pass

    def do_POST(self):
        form = parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode())
        FakeNAS.calls.append((self.path, form, self.headers.get("Cookie")))
        start = int(form['start'][0])
        items = [{"path": "/a.jpg", "ftype": 101},
                 {"path": "/video.mp4", "ftype": 102},
                 {"path": "/b.jpg", "ftype": "101"}]
        if self.mode == "expired":
            payload = {"code": "N001401"}
        elif self.mode == "empty":
            payload = {"code": "200", "data": {"total": 0, "list": []}}
        else:
            # Server caps page size; client must not skip records.
            payload = {"code": "200", "data": {"total": 3, "list": items[start:start+2]}}
        self.send_response(200)
        self.end_headers()
        self.wfile.write(json.dumps(payload).encode())

    def do_GET(self):
        query = parse_qs(urlsplit(self.path).query)
        FakeNAS.calls.append((self.path, query, self.headers.get("Cookie")))
        if self.mode == "redirect":
            self.send_response(302)
            self.send_header("Location", "http://127.0.0.1:1/unwanted")
            self.end_headers()
            return
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'<html>login</html>' if self.mode == "html" else
                         jpeg("red" if query["file_path"] == ["/a.jpg"] else "blue"))


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), FakeNAS)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        FakeNAS.mode, FakeNAS.calls = "ok", []
        self.tmp = tempfile.TemporaryDirectory()
        self.config = Path(self.tmp.name) / "config.json"
        self.cfg = {"nas_url": f"http://127.0.0.1:{self.server.server_port}",
                    "cookie": "token=test; device_id=sample; device=web", "api_token": "a"*32}
        self.config.write_text(json.dumps(self.cfg))
        self.client = create_app(self.config).test_client()
        self.headers = {"Authorization": "Bearer " + "a"*32}

    def tearDown(self):
        self.tmp.cleanup()

    def test_pipeline_pagination_filter_large_and_no_repeat(self):
        first = self.client.get('/photo.jpg', headers=self.headers)
        second = self.client.get('/photo.jpg', headers=self.headers)
        self.assertEqual(first.status_code, 200)
        self.assertEqual(second.status_code, 200)
        self.assertNotEqual(first.data, second.data)
        image = Image.open(io.BytesIO(first.data))
        self.assertEqual((image.size, image.mode), ((432, 576), 'RGB'))
        self.assertFalse(image.info.get('progressive'))
        self.assertEqual(dict(image.getexif()), {})
        self.assertEqual(first.headers['Cache-Control'], 'no-store')
        calls = FakeNAS.calls
        self.assertTrue(any(form.get('start') == ['2'] for _, form, _ in calls))
        downloads = [form for url, form, _ in calls if url.startswith('/transcode/thumb')]
        self.assertEqual(len(downloads), 2)
        self.assertTrue(all(form['dest_fmt'] == ['large'] for form in downloads))
        self.assertTrue(all(cookie == self.cfg['cookie'] for _, _, cookie in calls))

    def test_auth_blocks_nas_access(self):
        self.assertEqual(self.client.get('/photo.jpg').status_code, 401)
        self.assertEqual(FakeNAS.calls, [])

    def test_deployment_checker_over_http(self):
        class QuietHandler(WSGIRequestHandler):
            def log_message(self, *_):
                pass
        server = make_server('127.0.0.1', 0, create_app(self.config), handler_class=QuietHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            output = Path(self.tmp.name) / 'smoke-output'
            check(f'http://127.0.0.1:{server.server_port}', self.config, output)
            self.assertEqual(len(list(output.glob('*.jpg'))), 2)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_empty_and_expired(self):
        for mode, status in [('empty', 404), ('expired', 503)]:
            FakeNAS.mode = mode
            self.assertEqual(self.client.get('/photo.jpg', headers=self.headers).status_code, status)

    def test_invalid_image_and_redirect(self):
        for mode, status in [('html', 502), ('redirect', 503)]:
            FakeNAS.mode = mode
            response = self.client.get('/photo.jpg', headers=self.headers)
            self.assertEqual(response.status_code, status)
            self.assertNotIn(b'token=test', response.data)

    def test_contain_orientation_and_small_rejection(self):
        converted = Image.open(io.BytesIO(prepare_image(jpeg(size=(800, 1200), orientation=6), 'contain')))
        self.assertEqual(converted.getpixel((0, 0)), (255, 255, 255))
        self.assertGreater(converted.getpixel((216, 288))[0], 240)
        self.assertEqual(dict(converted.getexif()), {})
        with self.assertRaises(ServiceError):
            prepare_image(jpeg(size=(165, 248)))

    def test_config_reload_and_health(self):
        self.assertEqual(self.client.get('/healthz').status_code, 200)
        self.config.write_text('{}')
        self.assertEqual(self.client.get('/photo.jpg', headers=self.headers).status_code, 503)
        self.config.write_text(json.dumps(self.cfg))
        self.assertEqual(self.client.get('/photo.jpg', headers=self.headers).status_code, 200)


if __name__ == '__main__':
    unittest.main()
