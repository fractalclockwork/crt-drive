#!/usr/bin/env python3
"""USB CDC monitor for crt-drive firmware apps (pattern, term, demos, beam, hello_pico)."""

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

HELP = {
    "pattern": (
        "pattern: 1/c crosshatch  2/i intensity  3/f focus  4/n indian-head  "
        "5/o full-on  6 sync-squares  7/g pixel-code  m 80/132  r 60/78  "
        "a/d H  w/s V  0 reset  BOOTSEL cycles  ? help  q quit"
    ),
    "term": "term: type to the glass TTY; BOOTSEL cycles 78/60 x 80/132; ? status; Ctrl-C quit",
    "demos": (
        "demos: 1 starfield  2 radar  3/l lissajous  4/x xor  5 wireframe  "
        "6/t text  n next  m 80/132  r 60/78  a/d H  w/s V  0 reset  ? help  q quit"
    ),
    "cross60": (
        "cross60: 1 plus  2 box  3 grid  4 meas  m 80/132  r 60/78  "
        "a/d H 16px  w/s V 1 line  A/D 64px  W/S 5 lines  0 reset  ? help  q quit"
    ),
    "beam": (
        "beam: 1 comet  2 behind  3 ahead  4 vblank  5 tear  p reset pio  "
        "m 80/132  r 60/78  ? status  q quit"
    ),
    "hello": "hello_pico: p ping  q quit",
}

HIL_TERM_LINE = "Hello café\r\n".encode("utf-8")


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


def detect_app(line: str) -> str | None:
    if line.startswith("crt-pattern"):
        return "pattern"
    if line.startswith("crt-term"):
        return "term"
    if line.startswith("crt-demos"):
        return "demos"
    if line.startswith("crt-beam"):
        return "beam"
    if line.startswith("crt-cross60"):
        return "cross60"
    if line.startswith("crt-drive hello_pico"):
        return "hello"
    return None


def digest_ok(line: str, expected: str) -> bool:
    if not expected:
        return True
    return f"digest={expected}" in line


def wait_detected_banner(
    fd: int, timeout_s: float, expected_app: str | None, expected_digest: str
) -> tuple[str, str]:
    for line in read_lines(fd, timeout_s):
        if not line:
            continue
        print(f"rx: {line}", flush=True)
        app = detect_app(line)
        if app is None:
            continue
        if expected_app and app != expected_app:
            raise SystemExit(
                f"banner app mismatch: got {app} ({line!r}), expected {expected_app}"
            )
        if expected_digest and app not in ("pattern", "cross60") and not digest_ok(
            line, expected_digest
        ):
            raise SystemExit(
                f"banner digest mismatch: got {line!r}, expected digest={expected_digest}"
            )
        return app, line
    raise SystemExit("timed out waiting for firmware banner")


def hil_pattern(_fd: int, line: str, _timeout_s: float, _digest: str) -> int:
    if not line.startswith("crt-pattern"):
        raise SystemExit(f"expected crt-pattern banner, got {line!r}")
    print("crt-pattern monitor: ok", flush=True)
    return 0


def hil_term(fd: int, line: str, timeout_s: float, digest: str) -> int:
    os.write(fd, HIL_TERM_LINE)
    os.write(fd, b"?")
    for nxt in read_lines(fd, timeout_s):
        if not nxt:
            continue
        print(f"rx: {nxt}", flush=True)
        if not nxt.startswith("crt-term digest="):
            continue
        if not digest_ok(nxt, digest):
            raise SystemExit(
                f"status digest mismatch: got {nxt!r}, expected digest={digest}"
            )
        if "cursor=0,1" not in nxt:
            raise SystemExit(f"expected cursor=0,1 after UTF-8 line, got {nxt!r}")
        print("crt-term monitor: ok", flush=True)
        return 0
    raise SystemExit("timed out waiting for crt-term cursor status")


def hil_demos(fd: int, _line: str, timeout_s: float, digest: str) -> int:
    os.write(fd, b"2")
    for nxt in read_lines(fd, timeout_s):
        if not nxt:
            continue
        print(f"rx: {nxt}", flush=True)
        if not nxt.startswith("crt-demos digest="):
            continue
        if not digest_ok(nxt, digest):
            raise SystemExit(
                f"status digest mismatch: got {nxt!r}, expected digest={digest}"
            )
        if "scene=radar" not in nxt:
            continue
        print("crt-demos monitor: ok", flush=True)
        return 0
    raise SystemExit("timed out waiting for crt-demos scene=radar")


def _field_int(line: str, key: str) -> int | None:
    token = key + "="
    start = line.find(token)
    if start < 0:
        return None
    i = start + len(token)
    j = i
    while j < len(line) and line[j].isdigit():
        j += 1
    if j == i:
        return None
    return int(line[i:j])


def hil_beam(fd: int, _line: str, timeout_s: float, digest: str) -> int:
    os.write(fd, b"?")
    for nxt in read_lines(fd, timeout_s):
        if not nxt:
            continue
        print(f"rx: {nxt}", flush=True)
        if not nxt.startswith("crt-beam digest="):
            continue
        if not digest_ok(nxt, digest):
            raise SystemExit(
                f"status digest mismatch: got {nxt!r}, expected digest={digest}"
            )
        line_n = _field_int(nxt, "line")
        then_n = _field_int(nxt, "then")
        if line_n is None or then_n is None:
            raise SystemExit(f"expected line= and then= in {nxt!r}")
        if line_n == then_n:
            raise SystemExit(f"beam line did not move: {nxt!r}")
        print("crt-beam monitor: ok", flush=True)
        return 0
    raise SystemExit("timed out waiting for crt-beam line sample")


def hil_cross60(_fd: int, line: str, _timeout_s: float, _digest: str) -> int:
    if not line.startswith("crt-cross60"):
        raise SystemExit(f"expected crt-cross60 banner, got {line!r}")
    print("crt-cross60 monitor: ok", flush=True)
    return 0


def hil_hello(fd: int, line: str, timeout_s: float, digest: str) -> int:
    os.write(fd, b"p")
    for nxt in read_lines(fd, timeout_s):
        if not nxt:
            continue
        print(f"rx: {nxt}", flush=True)
        if not nxt.startswith("pong"):
            continue
        if not digest_ok(nxt, digest):
            raise SystemExit(
                f"pong digest mismatch: got {nxt!r}, expected digest={digest}"
            )
        print("hello_pico monitor: ok", flush=True)
        return 0
    raise SystemExit("timed out waiting for pong")


HIL = {
    "pattern": hil_pattern,
    "term": hil_term,
    "demos": hil_demos,
    "beam": hil_beam,
    "cross60": hil_cross60,
    "hello": hil_hello,
}


def interactive(fd: int, forced_app: str | None) -> int:
    app = forced_app
    if app:
        print(HELP[app], flush=True)
    else:
        print("monitor: waiting for firmware banner…  q quit (pattern/demos/hello)  Ctrl-C always", flush=True)
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
                        text = line.decode("utf-8", errors="replace").rstrip()
                        print(text, flush=True)
                        detected = detect_app(text)
                        if detected and app is None:
                            app = detected
                            print(HELP[app], flush=True)
                        elif detected and forced_app and detected != forced_app:
                            raise SystemExit(
                                f"banner app mismatch: got {detected}, expected {forced_app}"
                            )
            if stdin_fd in r:
                ch = os.read(stdin_fd, 1)
                if not ch or ch == b"\x03":
                    break
                if app != "term" and ch in (b"q", b"Q"):
                    break
                os.write(fd, ch)
    finally:
        if old is not None:
            termios.tcsetattr(stdin_fd, termios.TCSANOW, old)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("PICO_PORT", ""))
    parser.add_argument("--app", default=os.environ.get("MONITOR_APP", ""),
                        help="force dialect: pattern, term, demos, beam, cross60, hello")
    parser.add_argument("--digest", default=os.environ.get("IMAGE_ID", ""))
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument(
        "--check",
        action="store_true",
        help="wait for a firmware banner and exit",
    )
    parser.add_argument(
        "--hil",
        action="store_true",
        help="app-specific HIL after the banner",
    )
    args = parser.parse_args()
    forced = args.app.strip() or None
    if forced and forced not in HELP:
        raise SystemExit(
            f"unknown --app {forced!r} (pattern, term, demos, beam, cross60, hello)"
        )
    digest = args.digest.strip()
    port = wait_port(args.port or None, args.timeout)
    print(f"using {port}", flush=True)

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure(fd)
        termios.tcflush(fd, termios.TCIOFLUSH)
        if args.hil or args.check or not sys.stdin.isatty():
            app, line = wait_detected_banner(fd, args.timeout, forced, digest)
            if args.hil:
                return HIL[app](fd, line, args.timeout, digest)
            print(f"{app} monitor: ok", flush=True)
            return 0
        return interactive(fd, forced)
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
