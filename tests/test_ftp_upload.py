from __future__ import annotations

import hashlib
import json
import logging
from pathlib import Path
import tempfile
from threading import Thread
import unittest

from tools.ftp_upload import REMOTE_LIBRARY, TransferError, upload_bundle

try:
    from pyftpdlib.authorizers import DummyAuthorizer
    from pyftpdlib.handlers import FTPHandler
    from pyftpdlib.servers import FTPServer
except ImportError:
    DummyAuthorizer = FTPHandler = FTPServer = None

HAS_LOOPBACK_FTP = FTPServer is not None


def bundle_at(root: Path, name: str = "54851") -> Path:
    bundle = root / name
    bundle.mkdir()
    files = {
        "level.ini": b"format=1\ntitle=Test\n",
        "audio.ogg": b"OggS" + bytes(128),
        "library.mblc": b"MBLC\x02\x00\x00\x60" + bytes(512 * 96 * 2),
    }
    for filename, data in files.items():
        (bundle / filename).write_bytes(data)
    report = {
        "bundleFormat": 1,
        "source": {"type": "beatsaver", "key": name,
                   "contentSha1": "1" * 40, "archiveSha1": "2" * 40},
        "files": [{"path": filename, "size": len(data),
                   "sha256": hashlib.sha256(data).hexdigest()}
                  for filename, data in files.items()],
    }
    (bundle / "conversion-report.json").write_text(json.dumps(report),
                                                         encoding="utf-8")
    return bundle


class FakeFTP:
    def __init__(self, *, corrupt: bool = False, existing: bool = False):
        self.corrupt = corrupt
        self.directories = {REMOTE_LIBRARY}
        if existing:
            self.directories.add(REMOTE_LIBRARY + "/54851")
        self.files: dict[str, bytes] = {}
        self.current = REMOTE_LIBRARY
        self.renames: list[tuple[str, str]] = []

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return None

    def connect(self, host, port, timeout):
        self.host, self.port, self.timeout = host, port, timeout

    def login(self, user, password):
        self.user, self.password = user, password

    def cwd(self, path):
        target = path if path.startswith("/") else self.current + "/" + path
        if target not in self.directories:
            raise RuntimeError("missing directory: " + target)
        self.current = target

    def nlst(self):
        prefix = self.current + "/"
        return [path for path in self.directories
                if path.startswith(prefix) and "/" not in path[len(prefix):]]

    def mkd(self, name):
        self.directories.add(self.current + "/" + name)

    def storbinary(self, command, source, blocksize, callback):
        name = command.removeprefix("STOR ")
        chunks = []
        while chunk := source.read(blocksize):
            chunks.append(chunk)
            callback(chunk)
        self.files[self.current + "/" + name] = b"".join(chunks)

    def retrbinary(self, command, callback, blocksize):
        name = command.removeprefix("RETR ")
        data = self.files[self.current + "/" + name]
        if self.corrupt and name == "audio.ogg":
            data = b"x" + data[1:]
        for offset in range(0, len(data), blocksize):
            callback(data[offset:offset + blocksize])

    def rename(self, old, new):
        source = self.current + "/" + old
        target = self.current + "/" + new
        self.directories.remove(source)
        self.directories.add(target)
        for path in list(self.files):
            if path.startswith(source + "/"):
                self.files[target + path[len(source):]] = self.files.pop(path)
        self.renames.append((old, new))

    def delete(self, path):
        self.files.pop(self.current + "/" + path, None)

    def rmd(self, name):
        self.directories.remove(self.current + "/" + name)


class FtpUploadTests(unittest.TestCase):
    @unittest.skipUnless(HAS_LOOPBACK_FTP,
                         "optional pyftpdlib is required for protocol integration")
    def test_real_ftp_protocol_stages_verifies_and_publishes_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root.joinpath(*REMOTE_LIBRARY.strip("/").split("/"))
            library.mkdir(parents=True)
            authorizer = DummyAuthorizer()
            authorizer.add_user("movebeat_test", "local-only-test", str(root),
                                perm="elradfmwMT")

            class Handler(FTPHandler):
                pass

            Handler.authorizer = authorizer
            server = FTPServer(("127.0.0.1", 0), Handler)
            port = server.socket.getsockname()[1]
            thread = Thread(
                target=server.serve_forever, kwargs={"timeout": 0.05},
                daemon=True)
            previous_log_disable = logging.root.manager.disable
            logging.disable(logging.CRITICAL)
            thread.start()
            try:
                bundle = bundle_at(root)
                progress = []
                remote = upload_bundle(
                    bundle, "127.0.0.1", port, "movebeat_test",
                    "local-only-test",
                    progress=lambda done, total: progress.append((done, total)))
                published = library / bundle.name
                expected = {item.name: item.read_bytes()
                            for item in bundle.iterdir() if item.is_file()}
                actual = {item.name: item.read_bytes()
                          for item in published.iterdir() if item.is_file()}
                self.assertEqual(remote, REMOTE_LIBRARY + "/54851")
                self.assertEqual(actual, expected)
                self.assertEqual(
                    [item.name for item in library.iterdir()
                     if item.name.startswith(".movebeat-upload-")], [])
                self.assertTrue(progress)
                self.assertEqual(progress[-1][0], progress[-1][1])
            finally:
                server.close_all()
                thread.join(timeout=3)
                logging.disable(previous_log_disable)
            self.assertFalse(thread.is_alive())

    def test_upload_validates_remote_bytes_before_publish(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = bundle_at(Path(temporary))
            ftp = FakeFTP()
            progress = []
            remote = upload_bundle(
                bundle, "192.168.1.9", ftp_factory=lambda: ftp,
                progress=lambda done, total: progress.append((done, total)))
            self.assertEqual(remote, REMOTE_LIBRARY + "/54851")
            self.assertEqual(ftp.renames[0][1], "54851")
            self.assertEqual(ftp.files[remote + "/level.ini"],
                             (bundle / "level.ini").read_bytes())
            self.assertEqual(progress[-1][0], progress[-1][1])

    def test_corrupt_remote_data_is_not_published(self):
        with tempfile.TemporaryDirectory() as temporary:
            ftp = FakeFTP(corrupt=True)
            with self.assertRaisesRegex(TransferError, "verification failed"):
                upload_bundle(bundle_at(Path(temporary)), "ps3.local",
                              ftp_factory=lambda: ftp)
            self.assertEqual(ftp.directories, {REMOTE_LIBRARY})
            self.assertEqual(ftp.files, {})

    def test_existing_level_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as temporary:
            ftp = FakeFTP(existing=True)
            with self.assertRaisesRegex(TransferError, "already exists"):
                upload_bundle(bundle_at(Path(temporary)), "ps3.local",
                              ftp_factory=lambda: ftp)
            self.assertEqual(ftp.renames, [])
            self.assertEqual(ftp.files, {})

    def test_invalid_bundle_never_connects(self):
        with tempfile.TemporaryDirectory() as temporary:
            invalid = Path(temporary) / "bad"
            invalid.mkdir()
            with self.assertRaisesRegex(TransferError, "invalid"):
                upload_bundle(invalid, "ps3.local",
                              ftp_factory=lambda: self.fail("FTP connection opened"))

    def test_self_consistent_but_invalid_card_never_connects(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = bundle_at(Path(temporary))
            card = bundle / "library.mblc"
            damaged = b"MBLC" + bytes(128)
            card.write_bytes(damaged)
            report_path = bundle / "conversion-report.json"
            report = json.loads(report_path.read_text(encoding="utf-8"))
            entry = next(item for item in report["files"]
                         if item["path"] == "library.mblc")
            entry["size"] = len(damaged)
            entry["sha256"] = hashlib.sha256(damaged).hexdigest()
            report_path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(TransferError, "RGB565 card"):
                upload_bundle(bundle, "ps3.local",
                              ftp_factory=lambda: self.fail("FTP connection opened"))

    def test_unsafe_ftp_filename_never_connects(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = bundle_at(Path(temporary))
            unsafe = bundle / "bad name.txt"
            unsafe.write_bytes(b"x")
            report_path = bundle / "conversion-report.json"
            report = json.loads(report_path.read_text(encoding="utf-8"))
            report["files"].append({"path": unsafe.name, "size": 1,
                                    "sha256": hashlib.sha256(b"x").hexdigest()})
            report_path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(TransferError, "unsafe FTP filename"):
                upload_bundle(bundle, "ps3.local",
                              ftp_factory=lambda: self.fail("FTP connection opened"))


if __name__ == "__main__":
    unittest.main()
