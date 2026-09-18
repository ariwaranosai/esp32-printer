"""Optional real PostgreSQL gate, only on a disposable empty zalbum database."""
import io
import json
import os
from pathlib import Path
import tempfile
import unittest

from PIL import Image
import psycopg
from app import create_app


@unittest.skipUnless(os.environ.get('RUN_DISPOSABLE_PG_TEST') == '1', 'requires disposable PostgreSQL')
class LivePostgresTests(unittest.TestCase):
    def test_restricted_role_and_actual_query(self):
        # Dedicated CI service only. No connection defaults point at the user's NAS.
        host = os.environ.get('PG_TEST_HOST', '127.0.0.1')
        port = int(os.environ.get('PG_TEST_PORT', '5432'))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            photos = root / 'photos'
            photos.mkdir()
            original = photos / 'photo.jpg'
            Image.new('RGB', (1200, 800), 'orange').save(original)
            with psycopg.connect(host=host, port=port, dbname='zalbum', user='postgres', password='ci-only') as admin:
                if admin.execute("SELECT count(*) FROM pg_tables WHERE schemaname='public'").fetchone()[0]:
                    self.fail('Refusing to initialize a nonempty test database')
                admin.execute('CREATE TABLE public.feeds (user_id bigint, "ilike" integer, ftype integer, path text)')
                admin.execute('INSERT INTO public.feeds VALUES (1,1,101,%s),(2,1,101,%s),(1,0,101,%s),(1,1,102,%s)',
                              (str(original), str(photos/'other-user.jpg'), str(photos/'unliked.jpg'), str(photos/'video.mp4')))
                admin.execute(Path('postgres-view.sql').read_text().replace('BEGIN;', '').replace('COMMIT;', ''))
                admin.execute("ALTER ROLE photopainter PASSWORD 'ci-reader-only'")
            with psycopg.connect(host=host, port=port, dbname='zalbum', user='photopainter', password='ci-reader-only', autocommit=True) as reader:
                self.assertEqual(reader.execute('SELECT path FROM public.photopainter_favourites').fetchall(), [(str(original),)])
                with self.assertRaises(psycopg.errors.InsufficientPrivilege):
                    reader.execute('SELECT * FROM public.feeds')
                with self.assertRaises(psycopg.Error):
                    reader.execute('DELETE FROM public.photopainter_favourites')
            cfg = root/'config.json'
            cfg.write_text(json.dumps({'source':'postgres', 'photo_root':str(photos), 'api_token':'x'*32,
                           'postgres':{'host':host, 'port':port, 'user':'photopainter', 'password':'ci-reader-only'}}))
            response = create_app(cfg).test_client().get('/photo.jpg', headers={'Authorization':'Bearer '+'x'*32})
            self.assertEqual(response.status_code, 200)
            self.assertEqual(Image.open(io.BytesIO(response.data)).size, (432,576))
