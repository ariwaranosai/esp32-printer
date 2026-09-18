"""Test a running deployment without printing credentials or NAS file paths."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import sys

import requests
from PIL import Image


def check(url, config, output=None):
    token = json.loads(Path(config).read_text(encoding='utf-8'))['api_token']
    session = requests.Session()
    session.trust_env = False
    base = url.rstrip('/')
    try:
        response = session.get(base + '/healthz', timeout=10, allow_redirects=False)
        if response.status_code != 200:
            raise ValueError('health check failed')
        response = session.get(base + '/photo.jpg', timeout=10, allow_redirects=False)
        if response.status_code != 401:
            raise ValueError('unauthenticated request was not rejected with 401')
        hashes = []
        for index in range(2):
            response = session.get(base + '/photo.jpg', headers={'Authorization': 'Bearer ' + token},
                                   timeout=180, allow_redirects=False)
            if response.status_code != 200:
                # Do not echo arbitrary upstream text or stack traces.
                raise ValueError(f'photo request returned HTTP {response.status_code}')
            if response.headers.get('Content-Type', '').split(';')[0] != 'image/jpeg':
                raise ValueError('expected JPEG content type')
            with Image.open(io.BytesIO(response.content)) as image:
                if image.format != 'JPEG' or image.mode != 'RGB' or image.size != (432, 576):
                    raise ValueError('wrong image format, mode or dimensions')
                if image.info.get('progressive') or image.getexif():
                    raise ValueError('unexpected progressive JPEG or EXIF')
                image.load()
            hashes.append(hashlib.sha256(response.content).hexdigest())
            if output:
                directory = Path(output)
                directory.mkdir(parents=True, exist_ok=True)
                # Exclusive creation avoids overwriting a user's previous image.
                with (directory / f'photo-{index+1}.jpg').open('xb') as file:
                    file.write(response.content)
        print('PASS: health, authentication, two 432x576 RGB baseline JPEG responses')
        print('Two outputs differ:', hashes[0] != hashes[1], '(one available image can repeat)')
    finally:
        session.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8080')
    parser.add_argument('--config', default='/config/config.json')
    parser.add_argument('--output', help='Optional new directory for private test images')
    args = parser.parse_args()
    try:
        check(args.url, args.config, args.output)
    except (OSError, ValueError, KeyError, requests.RequestException):
        print('FAIL: deployment check failed; verify configuration, NAS connection and file permissions.', file=sys.stderr)
        sys.exit(1)
