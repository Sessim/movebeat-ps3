"""Optional end-to-end FTP smoke test; requires pyftpdlib on the developer PC."""

from pathlib import Path
import tempfile
import threading
import unittest

try:
    from pyftpdlib.authorizers import DummyAuthorizer
    from pyftpdlib.handlers import FTPHandler
    from pyftpdlib.servers import FTPServer
except ImportError:
    DummyAuthorizer = FTPHandler = FTPServer = None

from tests.test_ftp_upload import bundle_at
from tools.ftp_upload import TransferError, upload_bundle


@unittest.skipIf(FTPServer is None, "pyftpdlib is not installed")
class FtpLoopbackTests(unittest.TestCase):
    def test_real_ftp_protocol_and_no_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "dev_hdd0/game/MOVEBEAT1/USRDIR/levels"
            library.mkdir(parents=True)
            local_root = root / "local"
            local_root.mkdir()
            local = bundle_at(local_root)
            authorizer = DummyAuthorizer()
            authorizer.add_user("movebeat", "secret", str(root),
                                perm="elradfmwMT")
            handler = type("LoopbackHandler", (FTPHandler,), {"authorizer": authorizer})
            server = FTPServer(("127.0.0.1", 0), handler)
            port = server.socket.getsockname()[1]
            thread = threading.Thread(
                target=server.serve_forever,
                kwargs={"timeout": 0.1, "blocking": True, "handle_exit": False},
                daemon=True)
            thread.start()
            try:
                upload_bundle(local, "127.0.0.1", port, "movebeat", "secret")
                final = library / "54851"
                self.assertEqual((final / "audio.ogg").read_bytes(),
                                 (local / "audio.ogg").read_bytes())
                self.assertFalse(any(path.name.startswith(".movebeat-upload-")
                                     for path in library.iterdir()))
                with self.assertRaisesRegex(TransferError, "already exists"):
                    upload_bundle(local, "127.0.0.1", port, "movebeat", "secret")
            finally:
                server.close_all()
                thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
