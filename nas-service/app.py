"""ZSpace favourites to PhotoPainter JPEG. NAS credentials never leave this service."""
import hmac
import io
import json
import math
import os
from pathlib import Path
import stat
from http.cookies import SimpleCookie
import random
import threading
import time
import warnings
from urllib.parse import urlsplit

from flask import Flask, Response, jsonify, request
from PIL import Image, ImageOps, ImageFilter, UnidentifiedImageError
import requests

SIZE = (432, 576)
COMPACT_SIZE = (456, 656)
MAX_BYTES = 32 * 1024 * 1024
Image.MAX_IMAGE_PIXELS = 50_000_000


class ServiceError(Exception):
    def __init__(self, code, status=502):
        self.code, self.status = code, status


def load_config(path):
    with open(path, encoding="utf-8") as f:
        cfg = json.load(f)
    source = cfg.get("source", "api")
    if source == "api":
        url = urlsplit(cfg.get("nas_url", ""))
        if url.scheme not in ("http", "https") or not url.hostname or url.username or url.password or url.query or url.fragment or url.path not in ("", "/"):
            raise ValueError("nas_url must be an HTTP(S) origin")
        if not isinstance(cfg.get("cookie"), str) or not cfg["cookie"].strip() or any(c in cfg["cookie"] for c in "\r\n"):
            raise ValueError("cookie must contain the NAS login Cookie header")
    elif source == "postgres":
        pg = cfg.get("postgres", {})
        if not isinstance(pg, dict) or not pg.get("host") or not pg.get("user"):
            raise ValueError("postgres host/user required")
        if not all(isinstance(pg.get(key, ''), str) for key in ('host', 'user', 'password', 'dbname')):
            raise ValueError("postgres connection fields must be strings")
        if type(pg.get('port', 2222)) is not int or not 1 <= pg.get('port', 2222) <= 65535:
            raise ValueError("invalid postgres port")
        if not Path(cfg.get("photo_root", "")).is_absolute():
            raise ValueError("absolute photo_root required")
    else:
        raise ValueError("source must be api or postgres")
    if not isinstance(cfg.get("api_token"), str) or len(cfg["api_token"]) < 24:
        raise ValueError("api_token must contain at least 24 characters")
    if cfg.get("fit", "cover") not in ("cover", "contain"):
        raise ValueError("fit must be cover or contain")
    processing_settings(cfg.get("image_processing"))
    return cfg


def processing_settings(value=None):
    settings = {"enabled": True, "gamma": 0.96, "contrast": 1.05, "sharpness": 80}
    if value is None:
        return settings
    if not isinstance(value, dict) or set(value) - set(settings):
        raise ValueError("invalid image_processing settings")
    settings.update(value)
    if type(settings["enabled"]) is not bool:
        raise ValueError("image_processing.enabled must be boolean")
    for key, low, high in (("gamma", 0.8, 1.2), ("contrast", 0.9, 1.2), ("sharpness", 0, 150)):
        number = settings[key]
        if type(number) not in (int, float) or not math.isfinite(number) or not low <= number <= high:
            raise ValueError("invalid image_processing." + key)
    return settings


def enhance_photo(image, settings):
    """Enhance at final pixel size; do not sharpen each RGB channel independently."""
    if not settings["enabled"]:
        return image
    # Y-only processing avoids amplifying chroma noise. Keep pure endpoints intact.
    luminance, cb, cr = image.convert("YCbCr").split()
    curve = [max(0, min(255, round(((i / 255) ** settings["gamma"] * 255 - 128)
                                  * settings["contrast"] + 128))) for i in range(256)]
    curve[0], curve[255] = 0, 255
    luminance = luminance.point(curve)
    if settings["sharpness"]:
        luminance = luminance.filter(ImageFilter.UnsharpMask(
            radius=0.8, percent=round(settings["sharpness"]), threshold=3))
    return Image.merge("YCbCr", (luminance, cb, cr)).convert("RGB")


def prepare_image(data, fit="cover", processing=None, size=SIZE):
    settings = processing_settings(processing)
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", Image.DecompressionBombWarning)
            with Image.open(io.BytesIO(data)) as source:
                if source.width < size[0] and source.height < size[1]:
                    raise ServiceError("source_image_too_small")
                image = ImageOps.exif_transpose(source).convert("RGBA")
                bg = Image.new("RGBA", image.size, "white")
                bg.alpha_composite(image)
                image = bg.convert("RGB")
                if fit == "cover":
                    image = ImageOps.fit(image, size, method=Image.Resampling.LANCZOS)
                    image = enhance_photo(image, settings)
                else:
                    scaled = ImageOps.contain(image, size, method=Image.Resampling.LANCZOS)
                    scaled = enhance_photo(scaled, settings)
                    image = Image.new("RGB", size, "white")
                    image.paste(scaled, ((size[0]-scaled.width)//2, (size[1]-scaled.height)//2))
                image.info.clear()
                out = io.BytesIO()
                image.save(out, "JPEG", quality=95, subsampling=0, progressive=False)
                return out.getvalue()
    except ServiceError:
        raise
    except (OSError, ValueError, UnidentifiedImageError, Image.DecompressionBombError, Image.DecompressionBombWarning):
        raise ServiceError("invalid_source_image") from None


class NasClient:
    def __init__(self, cfg):
        self.cfg = cfg
        self.base = cfg["nas_url"].rstrip("/")
        self.session = requests.Session()
        self.session.trust_env = False
        self.session.headers.update({"Cookie": cfg["cookie"]})
        cookies = SimpleCookie()
        cookies.load(cfg["cookie"])
        self.params = {"plat": "web", "_l": "zh-CN",
                       "version": cfg.get("web_version", "2.3.2026082801")}
        for key in ("device_id", "device", "plat"):
            if key in cookies:
                self.params[key] = cookies[key].value

    def close(self):
        self.session.close()

    def read(self, method, path, *, params=None, data=None, limit=MAX_BYTES):
        try:
            with self.session.request(method, self.base + path, params={**self.params, **(params or {})}, data=data,
                                      timeout=(5, 20), stream=True, allow_redirects=False) as res:
                if res.status_code in (301, 302, 303, 307, 308, 401, 403):
                    raise ServiceError("nas_auth_required", 503)
                if res.status_code != 200:
                    raise ServiceError("nas_http_error")
                if int(res.headers.get("Content-Length", "0")) > limit:
                    raise ServiceError("nas_response_too_large")
                chunks, size, started = [], 0, time.monotonic()
                for chunk in res.iter_content(65536):
                    size += len(chunk)
                    if size > limit or time.monotonic() - started > 45:
                        raise ServiceError("nas_response_limit")
                    chunks.append(chunk)
                return b"".join(chunks)
        except (requests.RequestException, ValueError):
            raise ServiceError("nas_unavailable", 503) from None

    def favourites(self):
        photos, seen = [], set()
        # Bound work and fail instead of silently sampling just the first pages.
        start = 0
        for _ in range(100):
            raw = self.read("POST", "/v2/album/ilike/list",
                            data={"liked": 1, "start": start, "num": 100},
                            limit=4*1024*1024)
            try:
                result = json.loads(raw)
                if str(result.get("code")) != "200":
                    raise ServiceError("nas_api_error_or_session_expired", 503)
                items = result["data"]["list"]
                total = int(result["data"]["total"])
                if not isinstance(items, list) or total < 0:
                    raise ValueError()
                for item in items:
                    path = item.get("path")
                    if str(item.get("ftype")) != "101" or not isinstance(path, str) or not path.startswith("/") or item.get("encrypted_path"):
                        continue
                    if path not in seen:
                        seen.add(path)
                        photos.append(item)
                if start + len(items) >= total:
                    return photos
                if not items:
                    raise ServiceError("nas_incomplete_list")
                start += len(items)
            except (ValueError, KeyError, TypeError, AttributeError):
                raise ServiceError("nas_invalid_list") from None
        raise ServiceError("too_many_favourites")

    def large_image(self, item):
        return self.read("GET", "/transcode/thumb", params={
            "file_path": item["path"], "s": item.get("size", ""),
            "up": item.get("updated_at", ""), "dest_fmt": "large",
            "request_purpose": 5, "device_type": "web"})


class PostgresClient:
    """Read a restricted view and original files from a read-only bind mount."""
    def __init__(self, cfg):
        self.cfg = cfg

    def close(self):
        pass

    def favourites(self):
        import psycopg
        pg = self.cfg["postgres"]
        try:
            with psycopg.connect(
                host=pg["host"], port=pg.get("port", 2222),
                dbname=pg.get("dbname", "zalbum"), user=pg["user"],
                password=pg.get("password", ""), connect_timeout=5,
                options="-c default_transaction_read_only=on -c statement_timeout=5000",
            ) as connection:
                rows = connection.execute(
                    'SELECT DISTINCT path FROM public.photopainter_favourites LIMIT 10001'
                ).fetchall()
            if len(rows) > 10000:
                raise ServiceError("too_many_favourites")
            return [{"path": row[0]} for row in rows]
        except psycopg.Error:
            raise ServiceError("database_unavailable", 503) from None

    def large_image(self, item):
        # photo_root must be the same absolute mount path used by the database.
        # Reject symlinks component by component to prevent mount-root escape.
        try:
            root = Path(self.cfg["photo_root"]).resolve(strict=True)
            candidate = Path(item["path"])
            if not candidate.is_absolute() or '..' in candidate.parts:
                raise ServiceError("photo_path_not_allowed")
            relative = candidate.relative_to(root)
            parent = root
            for part in relative.parts:
                parent = parent / part
                if parent.is_symlink():
                    raise ServiceError("photo_path_not_allowed")
            resolved = candidate.resolve(strict=True)
            if not resolved.is_relative_to(root):
                raise ServiceError("photo_path_not_allowed")
            if not resolved.is_file():
                raise ServiceError("source_file_limit")
            # Linux openat traversal closes the symlink replacement race between
            # validation and open. Windows development retains the checks above.
            if os.open in os.supports_dir_fd:
                directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    for part in relative.parts[:-1]:
                        next_directory = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
                        os.close(directory)
                        directory = next_directory
                    fd = os.open(relative.parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
                finally:
                    os.close(directory)
                source_file = os.fdopen(fd, 'rb')
            else:
                source_file = resolved.open('rb')
            with source_file as source:
                info = os.fstat(source.fileno())
                if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_BYTES:
                    raise ServiceError("source_file_limit")
                data = source.read(MAX_BYTES + 1)
                if len(data) > MAX_BYTES:
                    raise ServiceError("source_file_limit")
                return data
        except (OSError, ValueError, TypeError, KeyError):
            raise ServiceError("photo_file_unavailable", 503) from None


def create_app(config_path=None, client_factory=None):
    app = Flask(__name__)
    path = config_path or os.environ.get("CONFIG_PATH", "/config/config.json")
    lock = threading.Lock()
    last_path = None

    @app.get("/healthz")
    def health():
        return jsonify(status="ok")

    @app.get("/photo.jpg")
    def photo():
        nonlocal last_path
        try:
            cfg = load_config(path)  # Cookie can be replaced without restarting.
        except (OSError, ValueError, TypeError, AttributeError):
            return jsonify(error="configuration_required"), 503
        expected = "Bearer " + cfg["api_token"]
        if not hmac.compare_digest(request.headers.get("Authorization", "").encode(), expected.encode()):
            return jsonify(error="unauthorized"), 401
        if not lock.acquire(blocking=False):
            return jsonify(error="busy"), 503, {"Retry-After": "5"}
        client = None
        try:
            factory = client_factory or (PostgresClient if cfg.get("source") == "postgres" else NasClient)
            client = factory(cfg)
            items = client.favourites()
            if not items:
                raise ServiceError("no_favourite_images", 404)
            candidates = [item for item in items if item["path"] != last_path] or items
            random.SystemRandom().shuffle(candidates)
            failure = None
            for item in candidates[:3]:
                try:
                    data = prepare_image(client.large_image(item), cfg.get("fit", "cover"),
                                         cfg.get("image_processing"),
                                         size=COMPACT_SIZE if request.headers.get("X-Photo-Layout") == "compact" else SIZE)
                    last_path = item["path"]
                    return Response(data, mimetype="image/jpeg", headers={
                        "Cache-Control": "no-store", "X-Photo-Width": "456" if request.headers.get("X-Photo-Layout") == "compact" else "432",
                        "X-Photo-Height": "656" if request.headers.get("X-Photo-Layout") == "compact" else "576", "Content-Disposition": 'inline; filename="photo.jpg"'})
                except ServiceError as error:
                    failure = error
            raise failure
        except ServiceError as error:
            return jsonify(error=error.code), error.status
        finally:
            if client:
                client.close()
            lock.release()

    return app


app = create_app()
