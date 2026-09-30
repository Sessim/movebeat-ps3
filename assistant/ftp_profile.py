"""Non-secret connection profile for the Windows track assistant."""

from __future__ import annotations

import json
import os
from pathlib import Path


def default_profile_path() -> Path:
    root = os.environ.get("APPDATA")
    if root:
        return Path(root) / "MoveBeat" / "ftp-profile.json"
    return Path.home() / ".movebeat" / "ftp-profile.json"


def _validate(host: str, port: int, user: str) -> tuple[str, int, str]:
    if not isinstance(host, str) or not isinstance(user, str):
        raise ValueError("Укажите корректные адрес и имя пользователя PS3")
    host = host.strip()
    user = user.strip() or "anonymous"
    if (not host or any(ord(char) < 32 for char in host) or
            not isinstance(port, int) or isinstance(port, bool) or
            not 1 <= port <= 65535 or
            any(ord(char) < 32 for char in user)):
        raise ValueError("Укажите корректные адрес, порт и имя пользователя PS3")
    return host, port, user


def save_ftp_profile(path: Path, host: str, port: int, user: str) -> None:
    host, port, user = _validate(host, port, user)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    payload = {"format": 1, "host": host, "port": port, "user": user}
    try:
        temporary.write_text(json.dumps(payload, ensure_ascii=False) + "\n",
                             encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def load_ftp_profile(path: Path) -> tuple[str, int, str] | None:
    path = Path(path)
    if not path.is_file():
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("format") != 1:
        raise ValueError("Неизвестный формат профиля PS3")
    return _validate(data.get("host", ""), data.get("port", 0),
                     data.get("user", "anonymous"))
