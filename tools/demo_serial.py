#!/usr/bin/env python3
"""USB CDC for crt_demos: wait for digest banner, HIL scene switch, or interactive keys."""

from __future__ import annotations

import argparse
import array
import fcntl
import glob
import os
import select
import sys
import termios
import time
import tty

TIOCMGET = 0x5415
TIOCMSET = 0x5418
TIOCM_DTR = 0x002
TIOCM_RTS = 0x004

PORT_GLOBS = (
    "/dev/serial/by-id/usb-Raspberry_Pi_Pico*",
    "/dev/serial/by-id/usb-Raspberry_Pi_Pico_*",
)


def find_port(explicit: str | None) -> str:
    if explicit:
        if not os.path.exists(explicit):
            raise SystemExit(f"serial port not found: {explicit}")
        return explicit
    matches: list[str] = []
    for pattern in PORT_GLOBS:
        matches.extend(glob.glob(pattern))
    matches = sorted(set(matches))
    if00 = [p for p in matches if p.endswith("-if00") or "if00" in os.path.basename(p)]
    chosen = if00 or matches
    if not chosen:
        raise SystemExit(
            "no Pico USB serial under /dev/serial/by-id/usb-Raspberry_Pi_Pico* "
            "(is the board enumerated as CDC, not BOOTSEL?)"
        )
    return chosen[0]


def wait_port(explicit: str | None, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    last_err = "port not found"
    while time.monotonic() < deadline:
        try:
            return find_port(explicit)
        except SystemExit as exc:
            last_err = str(exc)
            time.sleep(0.25)
    raise SystemExit(last_err)


def configure(fd: int) -> None:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    cc = list(attrs[6])
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 1
    attrs[6] = cc
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    buf = array.array("I", [0])
    fcntl.ioctl(fd, TIOCMGET, buf, True)
    buf[0] |= TIOCM_DTR | TIOCM_RTS
    fcntl.ioctl(fd, TIOCMSET, buf, True)


def read_lines(fd: int, timeout_s: float):
    deadline = time.monotonic() + timeout_s
    buf = b""
    while time.monotonic() < deadline:
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            chunk = b""
        if chunk:
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                yield line.decode("utf-8", errors="replace").strip()
        else:
            time.sleep(0.05)


def digest_ok(line: str, expected: str) -> bool:
    if not expected:
        return True
    return f"digest={expected}" in line


def wait_banner(fd: int, timeout_s: float, expected_digest: str) -> str:
    for line in read_lines(fd, timeout_s):
        if not line:
            continue
        print(f"rx: {line}", flush=True)
        if line.startswith("crt-demos digest="):
            if not digest_ok(line, expected_digest):
                raise SystemExit(
                    f"banner digest mismatch: got {line!r}, expected digest={expected_digest}"
                )
            return line
    raise SystemExit("timed out waiting for crt-demos banner")


def hil(fd: int, timeout_s: float, expected_digest: str) -> int:
    wait_banner(fd, timeout_s, expected_digest)
    os.write(fd, b"2")
    for line in read_lines(fd, timeout_s):
        if not line:
            continue
        print(f"rx: {line}", flush=True)
        if not line.startswith("crt-demos digest="):
            continue
        if not digest_ok(line, expected_digest):
            raise SystemExit(
                f"status digest mismatch: got {line!r}, expected digest={expected_digest}"
            )
        if "scene=radar" not in line:
            continue
        print("crt-demos serial: ok", flush=True)
        return 0
    raise SystemExit("timed out waiting for crt-demos scene=radar")


def interactive(fd: int) -> int:
    print(
        "keys: 1/s starfield  2/r radar  3/l lissajous  4/x xor  5/w wireframe  a next  ? help  q quit",
        flush=True,
    )
    old = None
    stdin_fd = sys.stdin.fileno()
    if sys.stdin.isatty():
        old = termios.tcgetattr(stdin_fd)
        tty.setcbreak(stdin_fd)
    buf = b""
    try:
        while True:
            readers = [fd]
            if sys.stdin.isatty() or not sys.stdin.closed:
                readers.append(stdin_fd)
            r, _, _ = select.select(readers, [], [], 0.2)
            if fd in r:
                try:
                    chunk = os.read(fd, 256)
                except BlockingIOError:
                    chunk = b""
                if chunk:
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        print(
                            line.decode("utf-8", errors="replace").rstrip(),
                            flush=True,
                        )
            if stdin_fd in r:
                ch = os.read(stdin_fd, 1)
                if not ch or ch in (b"q", b"Q", b"\x03"):
                    break
                os.write(fd, ch)
    finally:
        if old is not None:
            termios.tcsetattr(stdin_fd, termios.TCSANOW, old)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("PICO_PORT", ""))
    parser.add_argument("--digest", default=os.environ.get("DEMO_IMAGE_ID", ""))
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument(
        "--check",
        action="store_true",
        help="wait for crt-demos digest= banner and exit",
    )
    parser.add_argument(
        "--hil",
        action="store_true",
        help="banner, send 2, expect scene=radar",
    )
    args = parser.parse_args()
    port = wait_port(args.port or None, args.timeout)
    print(f"using {port}", flush=True)

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure(fd)
        termios.tcflush(fd, termios.TCIOFLUSH)
        if args.hil:
            return hil(fd, args.timeout, args.digest.strip())
        if args.check or not sys.stdin.isatty():
            wait_banner(fd, args.timeout, args.digest.strip())
            print("crt-demos serial: ok", flush=True)
            return 0
        return interactive(fd)
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
