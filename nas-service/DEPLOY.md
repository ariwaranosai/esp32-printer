# Q4 部署：无需 Cookie

极空间界面部署请看 [IMPORT-IMAGE.md](IMPORT-IMAGE.md)，使用 `compose.image.yaml` 并填写绝对路径。本文描述在服务目录中通过命令行部署的方式。两种方式均直接拉取 Docker Hub 镜像。

部署文件：`compose.yaml`（默认 PostgreSQL）、`compose.postgres.yaml`（同模式显式命名）、`compose.api.yaml`（可选 Cookie 模式）。仅运行一份 Compose 项目。`Dockerfile` 保留用于开发者自行构建。

## 1. 准备

把部署包解压到 NAS 自己的服务目录。以下命令在该目录运行。使用同一份 NAS 实机查询确认的库 `zalbum`、端口 `2222`、账号范围 `user_id=1`。

先确认现有本地认证规则能支持专用服务角色。不要修改为 trust，也不需要开启数据库 TCP 端口。服务通过 `ipc: host` 访问宿主机 `/dev/shm` 中的 Socket，详见 IMPORT-IMAGE.md。

## 2. 建立受限读取视图和角色

审核 `postgres-view.sql` 后，由管理员执行一次：

```sh
sudo -u postgres env LD_LIBRARY_PATH=/zspace/applications/services/pgsql/lib \
  /zspace/applications/services/pgsql/bin/psql \
  -X -w -v ON_ERROR_STOP=1 -h /dev/shm -p 2222 -d zalbum -f "$PWD/postgres-view.sql"
```

设置服务角色密码（交互输入，不在命令行写明文）：

```sh
sudo -u postgres env LD_LIBRARY_PATH=/zspace/applications/services/pgsql/lib \
  /zspace/applications/services/pgsql/bin/psql \
  -X -w -h /dev/shm -p 2222 -d zalbum -c '\password photopainter'
```

用新角色验证读权限，`-W` 将提示输入刚设置的密码：

```sh
env LD_LIBRARY_PATH=/zspace/applications/services/pgsql/lib \
  /zspace/applications/services/pgsql/bin/psql \
  -X -W -h /dev/shm -p 2222 -U photopainter -d zalbum -P pager=off \
  -c "SELECT count(*) FROM public.photopainter_favourites; SELECT has_table_privilege(current_user,'public.feeds','SELECT') AS can_read_all_feeds;"
```

最后一项应为 `f`。如果认证失败或权限过宽，先处理实际原因，不改用 postgres 超级用户配置 Web 服务。

## 3. 配置

```sh
mkdir -p config
cp config.postgres.example.json config/config.json
cp .env.example .env
python3 -c 'import secrets; print(secrets.token_urlsafe(32))'
```

生成结果用于 `config/config.json` 的 `api_token`，数据库密码填入 `postgres.password`。将 `photo_root` 与 `.env` 的 `PHOTO_ROOT` 同时改为实际照片目录（两者完全一致）。照片目录下的所有祖先目录须允许容器用户读取/遍历。将 `.env` 中 HTTP_PORT 改成需要的服务端口。

容器默认 UID/GID 是 65532；配置好后可限制密钥文件权限：

```sh
sudo chown 65532:65532 config config/config.json
sudo chmod 750 config
sudo chmod 640 config/config.json
```

这些命令仅改变本服务的配置目录，不修改 NAS 照片目录或数据库权限。

## 4. 拉取、启动、测试

```sh
docker compose config --quiet
docker compose up -d
docker compose ps
docker compose exec -T photopainter python smoke_test.py
```

测试通过应打印 `PASS`，验证健康检查、未授权拒绝、两次真实取图、432×576 RGB 非渐进 JPEG。`Two outputs differ: True` 表示两次图片不同；只有一张可用图片时允许相同。

导出一个实际测试图片（在容器里请求并以二进制标准输出返回到 NAS 当前目录，密钥不会显示）：

```sh
docker compose exec -T photopainter python -c 'import json,requests,sys; c=json.load(open("/config/config.json")); r=requests.get("http://127.0.0.1:8080/photo.jpg",headers={"Authorization":"Bearer "+c["api_token"]},timeout=180); r.raise_for_status(); sys.stdout.buffer.write(r.content)' > test-photo.jpg
```

输出 JPEG 可复制到 SD 卡验证当前固件的解码显示。固件自动联网取图不在这次服务开发范围内。

## 已执行的测试

2026-09-18：在本机 WSL Docker 中构建 `esp32-printer-nas:20260918`（linux/amd64），镜像中 11 项测试全部通过，包括临时 PostgreSQL 17 的真实集成测试和受限角色权限检查。

另启动正式 Gunicorn 容器，使用 UID 65532、只读根目录、只读照片挂载及 Unix Socket 挂载，通过密码认证读取临时数据库。`smoke_test.py` 验证健康检查、未授权拒绝和两次不同的 432×576 RGB 非渐进 JPEG 响应，全部通过。Compose v2 配置校验通过。

用户已在 Q4 验证专用账号查询返回 10 张收藏，`can_read_all_feeds=f`。NAS 容器完整取图仍待部署后验证。GitHub Actions 尚未推送或运行。

后续 NAS 实测：采用 `ipc: host` 和 `postgres.host=/dev/shm` 部署后，通过宿主机 11280 端口请求健康检查和取图均返回 HTTP 200；下载图片确认为 432×576 RGB 非渐进 JPEG。
