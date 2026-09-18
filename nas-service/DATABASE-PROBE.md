# 验证数据库访问（尚未在 Q4 实测）

已有项目 [ZalbumSyncTool 源码](https://github.com/lianjun007/ZalbumSyncTool/blob/main/server/zalbumPath.js) 使用宿主机 `/zspace/zsrp/zalbum/zalbumv2.db`，查询 `dirs` 和 `feeds` 表。该项目容器中的路径多一个 `/zspace` 挂载前缀。这只是路径线索，不保证所有固件一致；收藏关系和账号隔离必须实际检查后才能实现。

先在 NAS 的 SSH 终端检查文件是否存在（不修改设置或文件）：

```sh
ls -l /zspace/zsrp/zalbum/zalbumv2.db*
```

如 NAS 已有 Python 3，可将 `probe_album_db.py` 上传到自己的服务文件夹，然后执行：

```sh
python3 /你的服务目录/probe_album_db.py /zspace/zsrp/zalbum/zalbumv2.db
```

如需 Docker，将下面的 `/你的服务目录` 替换为脚本所在的实际路径：

```sh
docker run --rm --network none \
  --mount type=bind,src=/zspace/zsrp/zalbum,dst=/db,readonly \
  --mount type=bind,src=/你的服务目录/probe_album_db.py,dst=/probe.py,readonly \
  python:3.12-slim python /probe.py /db/zalbumv2.db
```

脚本以 SQLite `mode=ro` 打开数据库，并启用 `query_only`，只输出表名、字段名、类型和已知收藏标志为 1 的行数，不输出照片、路径、账号或 Cookie。挂载整个数据库目录是为了同时读取 WAL；不能用忽略 WAL 的 immutable 模式，否则可能漏掉最新收藏。若有权限或 WAL 读取错误，请保留错误信息，不修改原数据库及其权限。

拿到结构后，下一步才能确定按哪个账号筛选收藏、是否有删除标志，以及数据库路径与照片目录的映射。不要把全量数据库上传到聊天里。
