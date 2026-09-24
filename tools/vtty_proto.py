"""Framing for the crt_vtty USB session. Shared by tools/vtty.py and HIL."""

from __future__ import annotations

import os
import struct
import time
from dataclasses import dataclass

SYNC0 = 0xA5
SYNC1 = 0x5A
MAX_PAYLOAD = 1024

TYPE_TEXT = 0x01
TYPE_GOTO = 0x02
TYPE_CLEAR = 0x03
TYPE_SHOW = 0x04
TYPE_PUT = 0x05
TYPE_DATA = 0x06
TYPE_BLIT = 0x07
TYPE_FILL = 0x08
TYPE_MODE = 0x09
TYPE_CAPS = 0x0A
TYPE_LABEL = 0x0B
TYPE_AREA = 0x0C
TYPE_ACK = 0x81

STATUS_NAME = {
    0: "ok",
    1: "len",
    2: "state",
    3: "range",
    4: "flash",
    5: "full",
}

DATA_CHUNK = MAX_PAYLOAD - 4


@dataclass
class Ack:
    type: int
    status: int
    a: int
    b: int


def frame(typ: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload {len(payload)} exceeds {MAX_PAYLOAD}")
    return bytes((SYNC0, SYNC1, typ)) + struct.pack("<H", len(payload)) + payload


def write_all(fd: int, data: bytes, timeout_s: float = 5.0) -> None:
    view = memoryview(data)
    deadline = time.monotonic() + timeout_s
    while view:
        try:
            n = os.write(fd, view)
        except BlockingIOError:
            if time.monotonic() > deadline:
                raise SystemExit("timed out writing to vtty")
            time.sleep(0.01)
            continue
        if n <= 0:
            raise SystemExit("serial write failed")
        view = view[n:]


class Hunt:
    """Byte stream → frames. Bytes that are not a sync word are skipped."""

    def __init__(self) -> None:
        self.buf = b""

    def feed(self, data: bytes) -> None:
        if data:
            self.buf += data

    def pop(self) -> tuple[int, bytes] | None:
        b = self.buf
        i = 0
        while i + 5 <= len(b):
            if b[i] != SYNC0 or b[i + 1] != SYNC1:
                i += 1
                continue
            ln = b[i + 3] | (b[i + 4] << 8)
            if ln > MAX_PAYLOAD:
                i += 1
                continue
            end = i + 5 + ln
            if len(b) < end:
                self.buf = b[i:]
                return None
            typ = b[i + 2]
            payload = b[i + 5 : end]
            self.buf = b[end:]
            return typ, payload
        self.buf = b[i:]
        return None


def read_frame(fd: int, hunt: Hunt, timeout_s: float) -> tuple[int, bytes]:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        got = hunt.pop()
        if got is not None:
            return got
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            chunk = b""
        if chunk:
            hunt.feed(chunk)
        else:
            time.sleep(0.02)
    raise SystemExit("timed out waiting for a vtty frame")


def parse_ack(payload: bytes) -> Ack:
    if len(payload) < 6:
        raise SystemExit(f"short vtty ack ({len(payload)} bytes)")
    typ, status, a, b = struct.unpack_from("<BBHH", payload)
    return Ack(typ, status, a, b)


def expect_ack(fd: int, hunt: Hunt, timeout_s: float, typ: int) -> Ack:
    got_typ, payload = read_frame(fd, hunt, timeout_s)
    if got_typ != TYPE_ACK:
        raise SystemExit(f"expected ack, got type {got_typ:#x}")
    ack = parse_ack(payload)
    if ack.type != typ:
        raise SystemExit(f"ack for type {ack.type:#x}, expected {typ:#x}")
    if ack.status != 0:
        name = STATUS_NAME.get(ack.status, str(ack.status))
        raise SystemExit(f"vtty status {name}")
    return ack


def _wait_banner(fd: int, hunt: Hunt, timeout_s: float) -> bool:
    """True when the firmware speaks again after dropping a stuck frame or rebooting."""
    hunt.buf = b""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            chunk = b""
        except OSError:
            return False
        if chunk and b"crt-vtty" in chunk:
            return True
        time.sleep(0.05)
    return False


def transact(fd: int, hunt: Hunt, timeout_s: float, typ: int, payload: bytes = b"") -> Ack:
    wire = frame(typ, payload)
    write_all(fd, wire, timeout_s)
    try:
        return expect_ack(fd, hunt, timeout_s, typ)
    except SystemExit as exc:
        if "timed out" not in str(exc):
            raise
        if not _wait_banner(fd, hunt, 2.0):
            raise
        write_all(fd, wire, timeout_s)
        return expect_ack(fd, hunt, timeout_s, typ)


def _self_check() -> None:
    raw = b"noise" + frame(TYPE_CAPS) + frame(TYPE_ACK, struct.pack("<BBHH", TYPE_CAPS, 0, 800, 377))
    hunt = Hunt()
    hunt.feed(raw[:3])
    assert hunt.pop() is None
    hunt.feed(raw[3:])
    typ, payload = hunt.pop()
    assert typ == TYPE_CAPS and payload == b""
    typ, payload = hunt.pop()
    ack = parse_ack(payload)
    assert typ == TYPE_ACK and ack.a == 800 and ack.b == 377
    assert hunt.pop() is None


if __name__ == "__main__":
    _self_check()
    print("vtty_proto: ok")
