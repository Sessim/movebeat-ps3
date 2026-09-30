import argparse
import contextlib
import io
import pathlib
import tempfile
import unittest
from unittest import mock

from tools.monitor_ps3 import classify_status, monitor, parse_status


class Ps3MonitorTests(unittest.TestCase):
    def run_fake_monitor(self, payloads):
        class FakeFTP:
            def __init__(self):
                self.calls = 0

            def __enter__(self):
                return self

            def __exit__(self, *_):
                return False

            def connect(self, *_args, **_kwargs):
                pass

            def login(self, *_args):
                pass

            def retrbinary(self, _command, callback):
                callback(payloads[min(self.calls, len(payloads) - 1)])
                self.calls += 1

        tick = [0.0]

        def monotonic():
            tick[0] += 0.1
            return tick[0]

        with tempfile.TemporaryDirectory() as temporary:
            args = argparse.Namespace(
                host="127.0.0.1", port=21, user="anonymous", password="",
                remote_dir="/game", timeout=1.0, connect_timeout=1.0,
                interval=0.01, stale_after=100.0, minimum_heartbeats=2,
                output=pathlib.Path(temporary))
            with mock.patch("tools.monitor_ps3.ftplib.FTP", return_value=FakeFTP()), \
                 mock.patch("tools.monitor_ps3.time.monotonic", side_effect=monotonic), \
                 mock.patch("tools.monitor_ps3.time.sleep"), \
                 contextlib.redirect_stdout(io.StringIO()), \
                 contextlib.redirect_stderr(io.StringIO()):
                return monitor(args)

    def test_waits_for_more_than_one_running_heartbeat(self):
        status = parse_status(
            "movebeat_run_status=1\nstate=running\nphase=select\nheartbeat=1\n")
        self.assertEqual("WAITING", classify_status(status).code)

    def test_confirms_stable_main_loop(self):
        status = parse_status(
            "movebeat_run_status=1\nstate=running\nphase=select\nheartbeat=2\n")
        self.assertEqual("WAITING", classify_status(status).code)
        verdict = classify_status(status, observed_heartbeats=2, fresh=True,
                                  saw_running=True)
        self.assertEqual("BOOT_CONFIRMED", verdict.code)
        self.assertTrue(verdict.success)

    def test_old_report_cannot_prove_new_boot(self):
        old = parse_status(
            "movebeat_run_status=1\nstate=running\nphase=play\nheartbeat=999\n")
        self.assertEqual("WAITING", classify_status(old).code)
        self.assertEqual("WAITING", classify_status(
            old, observed_heartbeats=1, fresh=True, saw_running=True).code)
        exited = parse_status(
            "movebeat_run_status=1\nstate=clean_exit\nphase=shutdown\nheartbeat=999\n")
        self.assertEqual("WAITING", classify_status(exited).code)

    def test_monitor_requires_live_updates_after_old_report(self):
        def status(heartbeat):
            return (f"movebeat_run_status=1\nstate=running\nphase=play\n"
                    f"heartbeat={heartbeat}\n").encode()

        self.assertEqual(3, self.run_fake_monitor([status(999)]))
        self.assertEqual(0, self.run_fake_monitor(
            [status(999), status(1000), status(1001)]))

    def test_reports_precise_fatal_stage(self):
        status = parse_status(
            "movebeat_run_status=1\nstate=fatal\nphase=gem_initialization\n"
            "heartbeat=0\nerror_stage=7\nerror_code=0x80121004\n")
        self.assertEqual("WAITING", classify_status(status).code)
        verdict = classify_status(status, fresh=True)
        self.assertEqual("FATAL", verdict.code)
        self.assertIn("stage=7", verdict.detail)
        self.assertIn("0x80121004", verdict.detail)

    def test_rejects_unversioned_report(self):
        with self.assertRaises(ValueError):
            parse_status("state=running\nheartbeat=2\n")


if __name__ == "__main__":
    unittest.main()
