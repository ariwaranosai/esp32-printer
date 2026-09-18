# 极相册收藏 → PhotoPainter

默认 **PostgreSQL 原图模式（无需 Cookie）**：极空间通过 Docker Hub 镜像及 Compose 部署时，从 [IMPORT-IMAGE.md](IMPORT-IMAGE.md) 开始，使用 `compose.image.yaml`。命令行部署见 [DEPLOY.md](DEPLOY.md)，原理见 [POSTGRES.md](POSTGRES.md)。用户已在 Q4 上验证专用角色可读 10 张收藏、不可读完整照片表。镜像已在本机通过真实 PostgreSQL 和 Unix Socket 取图测试，NAS 容器部署仍待实测。下文描述可选的 HTTP API 模式。

Python Web 服务，每次 `GET /photo.jpg` 从当前 NAS 账号的“我喜欢的”中随机选一张图片，下载 **large 大图预览**，返回 **432×576、RGB、非渐进 JPEG**。与现有固件图片解码兼容；六色抖动由板子现有渲染代码完成，不在服务端重复处理。

本服务不会定时推送。板子或调用方每 3 小时请求一次即可；每次请求都会重新读取收藏列表。有多个候选时避免与上次成功结果连续重复（单进程内有效，重启后重置）。视频和加密照片跳过。一次最多尝试 3 张候选；失败返回 JSON 错误，调用方应保留上一张图片。

## 在极空间 Docker 部署

1. 将整个 `nas-service` 目录复制到 NAS 的一个文件夹。
2. 创建 `config` 子目录，将 `config.example.json` 复制为 `config/config.json`。
3. 填写配置：

```json
{
  "nas_url": "http://192.168.31.133:5055",
  "cookie": "从 NAS 浏览器请求中复制的完整 Cookie 请求头值",
  "api_token": "自行生成至少24字符的随机密钥",
  "fit": "cover"
}
```

生成服务密钥：`python -c "import secrets; print(secrets.token_urlsafe(32))"`。

在正常浏览器登录 NAS，打开开发者工具 → 网络，进入极相册“我喜欢的”，选中发往 NAS 的列表请求，在请求标头里复制 **Cookie 的值**（不是 Set-Cookie，不含 `Cookie:` 前缀）。不需要将密码或 Cookie 发到聊天里。Cookie 只放在 NAS 上的配置文件中；该目录已被 Git 和 Docker 构建上下文排除。

第一版使用已有会话，不做账号密码登录或自动续期。会话过期后重新登录并更新配置文件；服务每次请求重新加载配置，不用重启。请不要只复制 token，保留 device_id、device、plat 等会话字段。

4. 在该目录运行，或在极空间的 Compose 项目中导入：

```sh
docker compose -f compose.api.yaml up -d
docker compose -f compose.api.yaml logs --tail=50
```

服务端口默认 11280；冲突时修改 Compose 左侧宿主机端口。配置目录必须能被容器 UID 65532 读取。容器无需照片目录挂载，通过 NAS 接口下载图片。

## 调用

```sh
curl --fail -H "Authorization: Bearer YOUR_SERVICE_TOKEN" \
  http://192.168.31.133:11280/photo.jpg -o photo.jpg
```

- `GET /healthz`：进程存活检查，无需密钥，不代表 NAS 登录有效。
- `GET /photo.jpg`：需要服务密钥；成功直接返回 JPEG，可存入 SD 卡 `photos` 文件夹测试。
- `fit=cover`（配置文件）：居中裁剪铺满；`contain`：保留整张照片，白边补齐。
- 输出会应用 EXIF 方向、去除原始元数据，并拒绝明显过小的缩略图。
- HTTP 401：服务密钥错误；404：没有可用收藏图片；503：配置、会话、连接异常或服务正忙；502：上游响应/图片异常。

服务只从配置的 NAS 地址请求固定接口，不跟随重定向、不向调用者返回 NAS Cookie 或照片路径。仅部署到自己的局域网，远程使用请走已有 VPN；不要直接端口映射到公网。

## 接口依据与验证范围

从 Q4 的 `2.3.2026082801` 网页代码确认：

- `POST /v2/album/ilike/list`，表单 `liked=1,start=0,num=100`，按返回条数分页。
- `GET /transcode/thumb`，参数 `file_path,s,up,dest_fmt=large,request_purpose=5,device_type=web`。
- 返回列表的 `ftype=101` 为图片，102 为视频。

这些是内部接口，NAS 升级可能需要适配。大图是 NAS 生成的大尺寸预览，并非原始相机文件。可通过可选 `web_version` 配置调整公共版本参数。

测试使用本地模拟 NAS，通过真实 HTTP 覆盖分页、过滤视频、大图参数、图片转换、重复规避、登录失效和重定向。实际 Q4 的收藏页面已确认有 10 项且缩略图可导出；**独立 Cookie 调用、大图端到端以及 NAS 容器运行仍需配置后实测**。

```sh
python -m pip install -r requirements.txt
python -m unittest -v test_app.py
```

Windows 开发运行（生产使用 Docker 中的 Gunicorn）：

```powershell
$env:CONFIG_PATH = (Resolve-Path config/config.json).Path
python -m flask --app app run --host 127.0.0.1 --port 8080
```

本次仅开发服务，未修改或刷写 ESP32 固件；现有固件暂时不会自动请求此接口。


## Photo clarity processing

The service retains the 432x576 baseline JPEG contract (quality 95, no chroma
subsampling). After EXIF orientation and Lanczos resizing, it adjusts luminance
and applies a small-radius unsharp mask to luminance only. Chroma is not sharpened.
Contain-mode white padding is added after enhancement. Defaults apply to existing
configuration files without requiring new fields:

```json
"image_processing": {
  "enabled": true,
  "gamma": 0.96,
  "contrast": 1.05,
  "sharpness": 80
}
```

- `enabled`: boolean; `false` restores the previous JPEG processing exactly.
- `gamma`: 0.8–1.2; values below 1 lift midtones. Default 0.96.
- `contrast`: 0.9–1.2; luminance contrast around mid-gray. Default 1.05.
- `sharpness`: 0–150, unsharp percentage with fixed radius 0.8 pixels and threshold 3;
  zero disables sharpening. Default 80. High values may produce halos.

Invalid types, non-finite/out-of-range numbers and unknown processing keys reject
the configuration instead of silently applying an extreme filter. Configuration
is reloaded per request. The updated server image must be deployed for this feature;
changing JSON on an older image does not add processing.

The companion firmware uses serpentine Floyd–Steinberg error diffusion and bumps
its processed-photo cache version. UI layout and panel colors remain unchanged.
Palette colors have not been measured on the physical panel. Software previews
use ideal RGB display colors; they cannot predict exact ink colors or prove a
perceived sharpness improvement. Use the same photo for before/after hardware checks.

Run service regressions: `python -m unittest test_app test_image_processing test_postgres`
from this directory.


## Top-information layout (20260919-topinfo)

`GET /photo.jpg` with `X-Photo-Layout: compact` returns 456x656 JPEG; default requests still return 432x576 for existing devices. The firmware sends the header automatically and accepts both native and legacy dimensions during rollout. `X-Photo-Width` and `X-Photo-Height` reflect the selected dimensions. Run `python smoke_test.py --layout compact` to verify native output. Existing config.json needs no edits. Use image `nkssai/esp32-printer-nas:20260919-topinfo`.
