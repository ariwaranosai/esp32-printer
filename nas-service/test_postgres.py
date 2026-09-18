import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch

from PIL import Image
import psycopg
from app import PostgresClient, ServiceError, create_app


class PostgresTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name).resolve()
        self.photos = self.root / 'photos'
        self.photos.mkdir()
        self.image = self.photos / 'sample.jpg'
        Image.new('RGB', (1000, 1000), 'green').save(self.image)
        self.cfg = {'source': 'postgres', 'api_token': 'x'*32,
                    'photo_root': str(self.photos),
                    'postgres': {'host': '/nas-pg', 'user': 'photopainter'}}

    def test_cookie_free_http_pipeline(self):
        config = self.root / 'config.json'
        config.write_text(json.dumps(self.cfg))
        connection = MagicMock()
        connection.execute.return_value.fetchall.return_value = [(str(self.image),)]
        with patch('psycopg.connect') as connect:
            connect.return_value.__enter__.return_value = connection
            response = create_app(config).test_client().get(
                '/photo.jpg', headers={'Authorization': 'Bearer ' + 'x'*32})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(Image.open(io.BytesIO(response.data)).size, (432, 576))
        self.assertIn('default_transaction_read_only=on', connect.call_args.kwargs['options'])
        self.assertEqual(connect.call_args.kwargs['user'], 'photopainter')

    def test_reject_outside_and_traversal(self):
        outside = self.root / 'outside.jpg'
        outside.write_bytes(self.image.read_bytes())
        client = PostgresClient(self.cfg)
        for path in (outside, self.photos / '..' / 'outside.jpg'):
            with self.assertRaises(ServiceError):
                client.large_image({'path': str(path)})

    def test_database_errors_do_not_leak_credentials(self):
        config = self.root / 'config.json'
        config.write_text(json.dumps(self.cfg))
        with patch('psycopg.connect', side_effect=psycopg.OperationalError('secret-password')):
            response = create_app(config).test_client().get(
                '/photo.jpg', headers={'Authorization': 'Bearer ' + 'x'*32})
        self.assertEqual(response.status_code, 503)
        self.assertNotIn(b'secret-password', response.data)


if __name__ == '__main__':
    unittest.main()
