"""Safely transfer one validated MoveBeat level bundle to a PS3 FTP server.

Standard FTP has no portable remote SHA-256 command, so every uploaded file is
read back and checked before publishing the hidden staging directory.
"""

from __future__ import annotations

import ftplib
import hashlib
import json
from pathlib import Path
import re
import secrets
from typing import Callable

from tools.validate_bundle import validate_bundle


REMOTE_LIBRARY = "/dev_hdd0/game/MOVEBEAT1/USRDIR/levels"
SAFE_DIRECTORY = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]*\Z")


class TransferError(Exception):
    pass


def ps3_ftp() -> ftplib.FTP:
    """An FTP client for PS3 servers: replies are read as Latin-1.

    webMAN MOD's greeting reports temperatures with a Latin-1 degree sign
    (byte 0xB0), which ftplib's default UTF-8 decoding rejects. MoveBeat's
    own names are ASCII, so Latin-1 loses nothing.
    """
    return ftplib.FTP(encoding="latin-1")


def _names(ftp: ftplib.FTP) -> set[str]:
    try:
        return {item.rstrip("/").rsplit("/", 1)[-1].casefold()
                for item in ftp.nlst()}
    except ftplib.error_perm as exc:
        # Several PS3 FTP servers return 550 for an empty directory.
        if str(exc).startswith("550"):
            return set()
        raise


def _remote_digest(ftp: ftplib.FTP, filename: str) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0

    def consume(chunk: bytes) -> None:
        nonlocal size
        digest.update(chunk)
        size += len(chunk)

    ftp.retrbinary("RETR " + filename, consume, blocksize=65536)
    return size, digest.hexdigest()


def upload_bundle(
    directory: Path, host: str, port: int = 21, user: str = "anonymous",
    password: str = "", *,
    progress: Callable[[int, int], None] | None = None,
    ftp_factory: Callable[[], ftplib.FTP] = ps3_ftp,
) -> str:
    """Upload without replacing an existing level; return its remote path."""
    directory = Path(directory)
    name = directory.name
    if directory.is_symlink() or not SAFE_DIRECTORY.fullmatch(name) or name in (".", ".."):
        raise TransferError("Unsafe level directory name")
    if not host.strip() or not 1 <= port <= 65535:
        raise TransferError("Enter a valid PS3 address and FTP port")
    errors = validate_bundle(directory)
    if errors:
        raise TransferError("Local bundle is invalid: " + "; ".join(errors))

    report = json.loads((directory / "conversion-report.json").read_text(encoding="utf-8"))
    entries = [(entry["path"], entry["size"], entry["sha256"])
               for entry in report["files"]]
    if any(not SAFE_DIRECTORY.fullmatch(filename) for filename, _, _ in entries):
        raise TransferError("Bundle contains an unsafe FTP filename")
    report_path = directory / "conversion-report.json"
    report_bytes = report_path.read_bytes()
    entries.append((report_path.name, len(report_bytes),
                    hashlib.sha256(report_bytes).hexdigest()))
    total = sum(size for _, size, _ in entries) * 2
    done = 0
    stage = ".movebeat-upload-" + name + "-" + secrets.token_hex(6)
    uploaded: list[str] = []
    stage_created = False
    published = False

    with ftp_factory() as ftp:
        ftp.connect(host.strip(), port, timeout=20)
        ftp.login(user or "anonymous", password)
        try:
            ftp.cwd(REMOTE_LIBRARY)
        except ftplib.error_perm as exc:
            raise TransferError("MoveBeat is not installed or its levels directory is inaccessible") from exc
        if name.casefold() in _names(ftp):
            raise TransferError(f"Level {name} already exists on PS3; nothing was replaced")
        try:
            ftp.mkd(stage)
            stage_created = True
            ftp.cwd(stage)
            for filename, size, _digest in entries:
                uploaded.append(filename)
                with (directory / filename).open("rb") as source:
                    def sent(chunk: bytes) -> None:
                        nonlocal done
                        done += len(chunk)
                        if progress is not None:
                            progress(done, total)

                    ftp.storbinary("STOR " + filename, source,
                                   blocksize=65536, callback=sent)
                if (directory / filename).stat().st_size != size:
                    raise TransferError(f"Local file changed during upload: {filename}")
            for filename, size, digest in entries:
                remote_size, remote_hash = _remote_digest(ftp, filename)
                done += remote_size
                if progress is not None:
                    progress(done, total)
                if remote_size != size or remote_hash != digest:
                    raise TransferError(f"Remote verification failed: {filename}")
            ftp.cwd(REMOTE_LIBRARY)
            if name.casefold() in _names(ftp):
                raise TransferError(f"Level {name} appeared during upload; nothing was replaced")
            ftp.rename(stage, name)
            published = True
        finally:
            if stage_created and not published:
                try:
                    ftp.cwd(REMOTE_LIBRARY)
                    for filename in uploaded:
                        try:
                            ftp.delete(stage + "/" + filename)
                        except ftplib.error_perm:
                            pass
                    ftp.rmd(stage)
                except (OSError, ftplib.Error):
                    # A dropped connection can leave only a hidden staging dir;
                    # the PS3 loader ignores names beginning with a dot.
                    pass
    return REMOTE_LIBRARY + "/" + name
