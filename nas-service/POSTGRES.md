# 无 Cookie 模式：PostgreSQL + 原图只读挂载

## 已验证与未验证

在用户 Q4 上通过本地 psql 只读查询确认：

- 当前数据库是 `zalbum`，Unix socket 在 `/dev/shm/.s.PGSQL.2222`。
- 旧 `zalbumv2.db.migrated` 不是当前数据库。
- `feeds` 中 `user_id=1 AND "ilike"=1 AND ftype=101` 返回 9 张图片。
- `ilike_seq` 也有 9 条，按 feed_id 关联到该用户的 feeds 可匹配 9 条；不能附加 uhash 相等条件（实测匹配 0 条）。
- 已知收藏原图在 `/tmp/zfsv3/sata11/<NAS账号>/data/...` 下，`stat` 确认存在，约 16 MB。

此前网页显示 10 项，目前数据库是 9 项，差异尚未解释。当前实现以查询时的数据库为准，不承诺已与网页逐张核对。收藏字段可能含多用户语义，当前 SQL 只适用于已检查的主账号 user_id=1，不能随意改用户 ID 用于其他账号。

后续用户已创建服务角色和视图，验证返回 10 张收藏、不可直接读取 feeds。镜像已在本机通过真实 PostgreSQL 测试。NAS 容器取图仍待验证。

## 部署步骤

1. 审核 `postgres-view.sql`。管理员在 `zalbum` 中执行后，会新建 `photopainter` 角色和只暴露该账号收藏图片路径的视图。不给此角色直接读取 feeds 的权限；不会修改照片或现有收藏记录。脚本遇到同名对象会失败回滚，不覆盖它们。
2. 在 psql 内执行 `\password photopainter` 交互设置服务密码，不将密码写进 SQL 或聊天。此密码用于本地数据库认证，不依赖极空间网页 Cookie。
3. 把 `config.postgres.example.json` 复制为 `config/config.json`，填写服务密钥、数据库密码和实际 `photo_root`。先验证 NAS 本地 socket 的现有认证规则是否接受这个角色；如果拒绝，停止并检查原因，不把认证改成 trust，不开放数据库公网端口。视图授权是否足够隔离也应在该角色下核对（包括现有 PUBLIC 权限）。
4. 将 `PHOTO_ROOT` 环境变量设置为与配置完全一致的照片目录，然后启动：

```sh
export PHOTO_ROOT='/tmp/zfsv3/sata11/你的NAS账号/data/照片目录'
docker compose -f compose.postgres.yaml up -d
```

原图目录以相同的绝对路径挂载到容器，避免 NAS 数据库路径无法解析。只挂载需要的照片目录；该目录外的收藏会跳过。配置目录和照片目录均只读。使用 `ipc: host` 共享宿主机 IPC 和 `/dev/shm`，配置 `postgres.host=/dev/shm`，无需显式挂载该目录。该模式扩大了共享内存访问范围，SQL 权限仍由角色/视图控制。不直接挂载 PostgreSQL 数据文件，不以 postgres 超级用户运行 Web 服务。

请求方式不变：

```sh
curl --fail -H 'Authorization: Bearer YOUR_SERVICE_TOKEN' \
  http://NAS_IP:11280/photo.jpg -o photo.jpg
```

每次请求重新查询视图并读取一张原图，随机选图和图片处理沿用 API 模式；收藏新增/取消随数据库变化，无需更新 Cookie。服务重启会重置连续重复规避状态。原图上限 32 MiB、像素上限 5000 万；目前 Pillow 可解码的格式可用，不保证 HEIC/RAW，无法解码时尝试其他候选。

运行全部测试：`python -m unittest discover -v`。
