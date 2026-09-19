#!/usr/bin/env python3
"""Wait for Pico USB CDC, assert hello_pico banner, then ping/pong."""

from __future__ import annotations

import argparse
import array
import fcntl
import glob
import os
import sys
import termios
import time

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
    # Prefer interface 00 (CDC data) when several endpoints exist.
    matches = sorted(set(matches))
    if00 = [p for p in matches if p.endswith("-if00") or "if00" in os.path.basename(p)]
    chosen = (if00 or matches)
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("PICO_PORT", ""))
    parser.add_argument("--digest", default=os.environ.get("HELLO_IMAGE_ID", ""))
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()
    port = wait_port(args.port or None, args.timeout)
    print(f"using {port}", flush=True)

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure(fd)
        termios.tcflush(fd, termios.TCIOFLUSH)
        banner_ok = False
        pong_ok = False
        ping_sent = False
        expected_digest = args.digest.strip()
        for line in read_lines(fd, args.timeout):
            if not line:
                continue
            print(f"rx: {line}", flush=True)
            if line.startswith("crt-drive hello_pico ok"):
                if expected_digest and f"digest={expected_digest}" not in line:
                    raise SystemExit(
                        f"banner digest mismatch: got {line!r}, expected digest={expected_digest}"
                    )
                banner_ok = True
                if not ping_sent:
                    os.write(fd, b"p")
                    ping_sent = True
            if line.startswith("pong"):
                if expected_digest and f"digest={expected_digest}" not in line:
                    raise SystemExit(
                        f"pong digest mismatch: got {line!r}, expected digest={expected_digest}"
                    )
                pong_ok = True
                break
        if not banner_ok:
            raise SystemExit("timed out waiting for hello_pico banner")
        if not pong_ok:
            raise SystemExit("timed out waiting for pong")
        print("hello_pico serial: ok", flush=True)
        return 0
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
