"""Inspect album schema read-only; never print photo paths or personal records."""
import argparse
import json
from pathlib import Path
import sqlite3
import time


def inspect(path):
    path = Path(path).resolve(strict=True)
    # Do not use immutable=1: live NAS databases may contain recent WAL changes.
    connection = sqlite3.connect(path.as_uri() + '?mode=ro', uri=True, timeout=3)
    try:
        connection.execute('PRAGMA query_only=ON')
        deadline = time.monotonic() + 10
        connection.set_progress_handler(lambda: int(time.monotonic() > deadline), 10000)
        tables = connection.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"
        ).fetchall()
        result = []
        for (name,) in tables:
            quoted = '"' + name.replace('"', '""') + '"'
            columns = connection.execute('PRAGMA table_info(' + quoted + ')').fetchall()
            entry = {'table': name, 'columns': [{'name': col[1], 'type': col[2]} for col in columns]}
            # Only numeric favourite flags; no row data or user IDs are emitted.
            for col in columns:
                if col[1].lower() in ('ilike', 'liked', 'is_like', 'is_favorite', 'is_favourite'):
                    field = '"' + col[1].replace('"', '""') + '"'
                    entry.setdefault('flag_counts', {})[col[1]] = connection.execute(
                        'SELECT COUNT(*) FROM ' + quoted + ' WHERE ' + field + '=1'
                    ).fetchone()[0]
            result.append(entry)
        return {'tables': result}
    finally:
        connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('database')
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.database), ensure_ascii=False, indent=2))
    except (OSError, sqlite3.Error) as error:
        parser.exit(1, f'Read-only inspection failed: {error}\n')
