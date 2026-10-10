#!/usr/bin/env python3
"""Poll MoveBeat's on-console heartbeat through an FTP server."""

from __future__ import annotations

import argparse
import configparser
import ftplib
import io
import json
import pathlib
import sys
import time
from dataclasses import dataclass


@dataclass(frozen=True)
class Verdict:
    code: str
    terminal: bool
    success: bool
    detail: str


def parse_status(payload: bytes | str) -> dict[str, str]:
    text = payload.decode("utf-8", "replace") if isinstance(payload, bytes) else payload
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string("[movebeat]\n" + text)
    values = dict(parser["movebeat"])
    if values.get("movebeat_run_status") != "1":
        raise ValueError("unsupported or incomplete MoveBeat status file")
    return values


def classify_status(status: dict[str, str], minimum_heartbeats: int = 2,
                    observed_heartbeats: int = 0, fresh: bool = False,
                    saw_running: bool = False) -> Verdict:
    state = status.get("state", "unknown")
    phase = status.get("phase", "unknown")
    try:
        heartbeat = int(status.get("heartbeat", "0"))
    except ValueError:
        return Verdict("INVALID", True, False, "heartbeat is not an integer")
    if state == "fatal" and fresh:
        stage = status.get("error_stage", "?")
        error = status.get("error_code", "?")
        return Verdict("FATAL", True, False, f"failure at {phase}, stage={stage}, error={error}")
    if state == "clean_exit" and fresh and saw_running:
        return Verdict("CLEAN_EXIT", True, True, "application shut down normally")
    if state == "running" and observed_heartbeats >= minimum_heartbeats:
        return Verdict("BOOT_CONFIRMED", True, True,
                       f"main loop is alive (phase={phase}, heartbeat={heartbeat}, "
                       f"observed={observed_heartbeats})")
    return Verdict("WAITING", False, False,
                   f"state={state}, phase={phase}, heartbeat={heartbeat}, "
                   f"observed={observed_heartbeats}")


def _retrieve(ftp: ftplib.FTP, remote_path: str) -> bytes:
    output = io.BytesIO()
    ftp.retrbinary("RETR " + remote_path, output.write)
    return output.getvalue()


def _save_evidence(output_dir: pathlib.Path, status_payload: bytes,
                   status: dict[str, str]) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    report_dir = output_dir / stamp
    report_dir.mkdir()
    (report_dir / "run_status.ini").write_bytes(status_payload)
    (report_dir / "summary.json").write_text(
        json.dumps(status, ensure_ascii=False, indent=2), encoding="utf-8")
    return report_dir


def monitor(args: argparse.Namespace) -> int:
    deadline = time.monotonic() + args.timeout
    remote = args.remote_dir.rstrip("/") + "/run_status.ini"
    last_payload: bytes | None = None
    last_change = time.monotonic()
    latest_status: dict[str, str] | None = None
    latest_payload: bytes | None = None
    last_observed_heartbeat: int | None = None
    observed_heartbeats = 0
    saw_running = False
    saw_missing = False

    # Latin-1: webMAN MOD's greeting carries a Latin-1 degree sign.
    with ftplib.FTP(encoding="latin-1") as ftp:
        ftp.connect(args.host, args.port, timeout=args.connect_timeout)
        ftp.login(args.user, args.password)
        while time.monotonic() < deadline:
            try:
                payload = _retrieve(ftp, remote)
                status = parse_status(payload)
                latest_status, latest_payload = status, payload
                if payload != last_payload:
                    fresh = last_payload is not None or saw_missing
                    last_payload = payload
                    last_change = time.monotonic()
                    if status.get("state") == "running":
                        try:
                            heartbeat = int(status.get("heartbeat", "0"))
                        except ValueError:
                            heartbeat = None
                        if fresh and heartbeat is not None:
                            saw_running = True
                            if heartbeat != last_observed_heartbeat:
                                observed_heartbeats += 1
                                last_observed_heartbeat = heartbeat
                    verdict = classify_status(
                        status, args.minimum_heartbeats,
                        observed_heartbeats, fresh, saw_running)
                    print(f"[{verdict.code}] {verdict.detail}", flush=True)
                    if verdict.terminal:
                        report = _save_evidence(args.output, payload, status)
                        print(f"Evidence: {report}")
                        return 0 if verdict.success else 2
                elif time.monotonic() - last_change >= args.stale_after:
                    phase = status.get("phase", "unknown")
                    print(f"[STALE] heartbeat has not changed for {args.stale_after:g}s; "
                          f"last phase={phase}", flush=True)
                    break
            except ftplib.error_perm as error:
                # A 550 is expected until the process creates its first report.
                if str(error).startswith("550"):
                    saw_missing = True
                else:
                    raise
            time.sleep(args.interval)

    if latest_status is not None and latest_payload is not None:
        report = _save_evidence(args.output, latest_payload, latest_status)
        print(f"Evidence: {report}")
    print("[TIMEOUT] no stable main-loop heartbeat was confirmed", file=sys.stderr)
    return 3


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Confirm MoveBeat startup by polling run_status.ini over FTP")
    parser.add_argument("--host", required=True, help="PS3 IP address")
    parser.add_argument("--port", type=int, default=21)
    parser.add_argument("--user", default="anonymous")
    parser.add_argument("--password", default="movebeat@localhost")
    parser.add_argument("--remote-dir",
                        default="/dev_hdd0/game/MOVEBEAT1/USRDIR")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--connect-timeout", type=float, default=10.0)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--stale-after", type=float, default=12.0)
    parser.add_argument("--minimum-heartbeats", type=int, default=2)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("out/ps3-sessions"))
    return parser


def main() -> int:
    args = make_parser().parse_args()
    try:
        return monitor(args)
    except (OSError, ftplib.Error, ValueError) as error:
        print(f"[MONITOR_ERROR] {error}", file=sys.stderr)
        return 4


if __name__ == "__main__":
    raise SystemExit(main())
