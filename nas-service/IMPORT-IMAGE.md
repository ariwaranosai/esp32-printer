# 极空间使用 Docker Hub 镜像

镜像名：`nkssai/esp32-printer-nas:20260918`，架构：`linux/amd64`（适用于本次 Q4）。

## 1. 镜像来源

Compose 会直接从 Docker Hub 拉取镜像，无需上传 tar 或在 NAS 构建。也可以提前拉取：

```sh
sudo docker pull nkssai/esp32-printer-nas:20260918
```

仓库：https://hub.docker.com/r/nkssai/esp32-printer-nas 。Compose 固定使用日期版本；`latest` 也指向本次发布。配置中的密码和 token 不在镜像内。

## 2. 配置文件

在自己选择的服务目录里创建 `config/config.json`，内容参考 `config.postgres.example.json`。

- `source`：`postgres`
- `postgres.host`：`/dev/shm`
- `postgres.port`：`2222`
- `postgres.dbname`：`zalbum`
- `postgres.user`：`photopainter`
- `postgres.password`：此前设置的数据库密码。
- `api_token`：自行生成至少 24 字符随机密钥。
- `photo_root`：`/tmp/zfsv3/sata11/18600205946/data`

### 极空间禁止挂载 `/dev/shm` 时

当前 PostgreSQL Compose 使用 `ipc: host`，让容器访问宿主机的 `/dev/shm`，不再声明 `/dev/shm` 目录挂载。现有配置需要将 `postgres.host` 从 `/nas-pg` 改为 `/dev/shm`。数据库继续使用 Unix Socket，无需启用 TCP，也无需修改 `pg_hba.conf`。

这会共享宿主机 IPC 和 `/dev/shm`，隔离范围比原先只读挂载更宽；容器仍以 UID 65532 运行，数据库仍使用专用受限账号。本机已实测该 UID 在只读根文件系统和禁用额外 capabilities 的配置下可连接宿主机测试 Socket。极空间界面是否接受 `ipc: host`，以及 NAS 实际取图，需部署验证。

数据库账号和视图已由用户验证：返回 10 张收藏，`can_read_all_feeds=f`。新 NAS 的初始化步骤见 DEPLOY.md。

配置目录须允许容器用户 UID/GID 65532 遍历、读取。进入服务目录后可执行：

```sh
sudo chown 65532:65532 config config/config.json
sudo chmod 750 config
sudo chmod 640 config/config.json
```

## 3. Compose 部署

导入 `compose.image.yaml`，设置项目环境变量：

```dotenv
NAS_SERVICE_DIR=/服务目录的实际绝对路径
PHOTO_ROOT=/tmp/zfsv3/sata11/18600205946/data
HTTP_PORT=11280
```

`NAS_SERVICE_DIR` 指包含 `config` 子目录的目录。界面不支持环境变量时，可将 YAML 中的 `${NAS_SERVICE_DIR:?Set NAS_SERVICE_DIR}/config` 和两处 `${PHOTO_ROOT:?Set PHOTO_ROOT}` / `${PHOTO_ROOT}` 替换为对应绝对路径，将 `${HTTP_PORT:-11280}:8080` 改成 `11280:8080`。

SSH 部署也可以：在同目录 `.env` 中填写上述变量后运行：

```sh
sudo docker compose -f compose.image.yaml up -d
sudo docker compose -f compose.image.yaml exec -T photopainter python smoke_test.py
```

界面部署时，在服务容器的终端执行 `python smoke_test.py`。应输出 `PASS`；这一步验证 NAS 上真正的数据库连接、文件挂载和图片转换。

## 4. 取图

```sh
curl --fail -H "Authorization: Bearer YOUR_API_TOKEN" \
  http://192.168.31.133:11280/photo.jpg -o photo.jpg
```

返回 432×576 RGB 非渐进 JPEG。板子定时请求此接口仍需另外接入固件。
