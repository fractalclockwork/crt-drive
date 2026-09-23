#!/usr/bin/env python3
"""Live C310 view and the camera-HIL framing check.

Camera HIL always starts here. The check puts a full-raster pattern on the
glass (crosshatch on crt-pattern, measure box on crt-cross60) and watches
the Logitech C310 until that raster is visible and inside the picture on
every side.

    make camera         live view, leave it open
    make camera-check   live view, exit 0 only when the frame passes
"""

from __future__ import annotations

import argparse
import array
import fcntl
import glob
import os
import subprocess
import sys
import termios
import time
from pathlib import Path

TIOCMGET = 0x5415
TIOCMSET = 0x5418
TIOCM_DTR = 0x002
TIOCM_RTS = 0x004

PORT_GLOBS = (
    "/dev/serial/by-id/usb-Raspberry_Pi_Pico*",
    "/dev/serial/by-id/usb-Raspberry_Pi_Pico_*",
)

# Active-raster outline. pattern '1' is crosshatch; cross60 '4' is meas.
FRAMING_KEY = {
    "pattern": b"1",
    "cross60": b"4",
}

WIDTH = 1280
HEIGHT = 960
HOLD_S = 1.0
EDGE_MARGIN_FRAC = 0.02


def find_webcam(explicit: str | None) -> str:
    if explicit:
        if not os.path.exists(explicit):
            raise SystemExit(f"camera not found: {explicit}")
        return explicit
    root = "/sys/class/video4linux"
    if not os.path.isdir(root):
        raise SystemExit("no V4L2 devices")
    found: list[str] = []
    for node in sorted(os.listdir(root)):
        if not node.startswith("video"):
            continue
        name_path = os.path.join(root, node, "name")
        index_path = os.path.join(root, node, "index")
        try:
            name = open(name_path, encoding="utf-8", errors="replace").read()
            index = open(index_path, encoding="utf-8").read().strip()
        except OSError:
            continue
        if "046d:081b" not in name:
            continue
        path = f"/dev/{node}"
        if index == "0":
            return path
        found.append(path)
    if found:
        return found[0]
    raise SystemExit("Logitech C310 not found (USB 046d:081b)")


def find_port() -> str | None:
    matches: list[str] = []
    for pattern in PORT_GLOBS:
        matches.extend(glob.glob(pattern))
    matches = sorted(set(matches))
    if00 = [p for p in matches if "if00" in os.path.basename(p)]
    chosen = if00 or matches
    return chosen[0] if chosen else None


def configure_cdc(fd: int) -> None:
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


def detect_app(line: str) -> str | None:
    if line.startswith("crt-pattern"):
        return "pattern"
    if line.startswith("crt-cross60"):
        return "cross60"
    if line.startswith("crt-term"):
        return "term"
    if line.startswith("crt-demos"):
        return "demos"
    if line.startswith("crt-drive hello_pico"):
        return "hello"
    return None


def _request_via_docker() -> str:
    """Host user is not in dialout; the Pico container is root and has /dev."""
    repo = Path(__file__).resolve().parents[1]
    docker = repo / "docker"
    cmd = [
        str(docker / "with-docker.sh"),
        "docker",
        "compose",
        "--env-file",
        str(docker / "TOOLCHAIN_VERSIONS"),
        "-f",
        str(docker / "docker-compose.yml"),
        "-f",
        str(docker / "docker-compose.usb.yml"),
        "run",
        "--rm",
        "--no-deps",
        "pico-dev",
        "python3",
        "/workspace/tools/camera.py",
        "--select-pattern",
    ]
    try:
        proc = subprocess.run(
            cmd,
            cwd=repo,
            check=False,
            capture_output=True,
            text=True,
            timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"CDC unavailable ({exc}) — framing pattern not selected"
    note = (proc.stdout or proc.stderr).strip().splitlines()
    if proc.returncode != 0:
        detail = note[-1] if note else f"exit {proc.returncode}"
        return f"CDC unavailable ({detail}) — framing pattern not selected"
    return note[-1] if note else "framing pattern selected"


def _select_on_fd(fd: int, timeout_s: float) -> tuple[int, str]:
    """Read the banner, ask with '?' if the image has gone quiet, then select."""
    deadline = time.monotonic() + timeout_s
    buf = b""
    app = None
    asked = False
    while time.monotonic() < deadline and app is None:
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            chunk = b""
        if chunk:
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                app = detect_app(line.decode("utf-8", errors="replace").strip())
                if app:
                    break
            continue
        if not asked and time.monotonic() > deadline - timeout_s + 0.4:
            os.write(fd, b"?")
            asked = True
        time.sleep(0.05)
    if app in FRAMING_KEY:
        os.write(fd, FRAMING_KEY[app])
        name = "crosshatch" if app == "pattern" else "meas"
        return 0, f"{app}: selected {name}"
    if app:
        return 0, f"running {app} — need crt-pattern crosshatch or crt-cross60 meas"
    return 1, "no firmware banner — framing pattern not selected"


def request_framing_pattern(timeout_s: float = 2.0) -> str:
    """Select the full-raster pattern when the running image has one.

    Closes the CDC port before returning so a monitor can attach afterwards.
    """
    port = find_port()
    if port is None:
        return "no Pico CDC — framing pattern not selected"
    try:
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as exc:
        if exc.errno in (13, 1) and os.geteuid() != 0:
            return _request_via_docker()
        return f"CDC busy ({exc.strerror}) — framing pattern not selected"
    try:
        configure_cdc(fd)
        _code, note = _select_on_fd(fd, timeout_s)
        return note
    finally:
        os.close(fd)


def _luma_grid(rgb: bytes, w: int, h: int, step: int) -> list[list[int]]:
    gw = w // step
    gh = h // step
    grid = [[0] * gw for _ in range(gh)]
    for gy in range(gh):
        y = gy * step
        row = y * w
        for gx in range(gw):
            i = (row + gx * step) * 3
            r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
            grid[gy][gx] = (r + g + b) // 3
    return grid


def _valley_threshold(grid: list[list[int]]) -> int | None:
    hist = [0] * 256
    for row in grid:
        for lum in row:
            hist[lum] += 1
    # Smooth so a single spike is not a peak.
    smooth = [0] * 256
    for i in range(256):
        acc = 0
        for k in range(max(0, i - 4), min(256, i + 5)):
            acc += hist[k]
        smooth[i] = acc
    dark = max(range(15, 140), key=lambda i: smooth[i], default=40)
    bright = max(range(170, 250), key=lambda i: smooth[i], default=220)
    if smooth[dark] < 20 or smooth[bright] < 20:
        return None
    valley = min(range(dark, bright), key=lambda i: smooth[i])
    if smooth[valley] > min(smooth[dark], smooth[bright]) * 0.85:
        return None
    return valley


def assess_frame(rgb: bytes, w: int, h: int) -> dict:
    """Return glass/phosphor boxes and whether the raster is fully framed.

    The glass is the dark CRT face. The pattern passes when that face sits
    inside the picture on every side and phosphor reaches all four quadrants
    of it (the crosshatch or measure box, not a corner of text).

    When the room is as dark as the face, the dark mask fills the picture.
    Framing is then the raster box itself: inset on every side, in every
    quadrant, and large in the frame.
    """
    step = 4
    grid = _luma_grid(rgb, w, h, step)
    gh = len(grid)
    gw = len(grid[0])
    thresh = _valley_threshold(grid)
    result = {
        "ok": False,
        "reason": "glass not found",
        "glass": None,
        "phosphor": None,
        "threshold": thresh,
    }
    if thresh is None:
        result["reason"] = "glass not found — face and surround are not separated"
        return result

    dark_cols = [0] * gw
    dark_rows = [0] * gh
    for y in range(gh):
        for x in range(gw):
            if grid[y][x] < thresh:
                dark_cols[x] += 1
                dark_rows[y] += 1
    col_cut = max(2, gh // 5)
    row_cut = max(2, gw // 5)
    xs = [x for x in range(gw) if dark_cols[x] >= col_cut]
    ys = [y for y in range(gh) if dark_rows[y] >= row_cut]
    if not xs or not ys:
        return result

    gx0, gx1 = xs[0], xs[-1]
    gy0, gy1 = ys[0], ys[-1]
    # Drop a detached dark run at the border (a shadow) by shrinking to the
    # longest contiguous run.
    def longest_run(idxs: list[int]) -> tuple[int, int]:
        best = (idxs[0], idxs[0])
        start = idxs[0]
        prev = idxs[0]
        for i in idxs[1:]:
            if i != prev + 1:
                if prev - start > best[1] - best[0]:
                    best = (start, prev)
                start = i
            prev = i
        if prev - start > best[1] - best[0]:
            best = (start, prev)
        return best

    gx0, gx1 = longest_run(xs)
    gy0, gy1 = longest_run(ys)
    glass_w = (gx1 - gx0 + 1) * step
    glass_h = (gy1 - gy0 + 1) * step
    if glass_w * glass_h < (w * h) // 12:
        result["reason"] = "glass is only a small part of the picture"
        return result
    glass = (gx0 * step, gy0 * step, (gx1 + 1) * step, (gy1 + 1) * step)
    result["glass"] = glass

    margin_x = max(8, int(w * EDGE_MARGIN_FRAC))
    margin_y = max(8, int(h * EDGE_MARGIN_FRAC))
    surround_dark = (
        glass[0] < margin_x
        and glass[1] < margin_y
        and w - glass[2] < margin_x
        and h - glass[3] < margin_y
    )
    clipped = []
    if not surround_dark:
        if glass[0] < margin_x:
            clipped.append("left")
        if glass[1] < margin_y:
            clipped.append("top")
        if w - glass[2] < margin_x:
            clipped.append("right")
        if h - glass[3] < margin_y:
            clipped.append("bottom")

    # Phosphor: green/cyan clearly above the glass, inside the face.
    quads = [0, 0, 0, 0]
    minx = w
    miny = h
    maxx = 0
    maxy = 0
    count = 0
    cx = (glass[0] + glass[2]) // 2
    cy = (glass[1] + glass[3]) // 2
    y = glass[1]
    while y < glass[3]:
        x = glass[0]
        row = y * w
        while x < glass[2]:
            i = (row + x) * 3
            r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
            if g > thresh + 25 and g > r + 12 and g >= b - 30:
                count += 1
                if x < minx:
                    minx = x
                if y < miny:
                    miny = y
                if x > maxx:
                    maxx = x
                if y > maxy:
                    maxy = y
                quads[(0 if y < cy else 2) + (0 if x < cx else 1)] += 1
            x += step
        y += step
    if count < 30:
        result["reason"] = "pattern not visible on the glass"
        if clipped:
            result["reason"] = "clipped " + ",".join(clipped) + "; pattern not visible"
        return result
    phosphor = (minx, miny, maxx + step, maxy + step)
    result["phosphor"] = phosphor
    pw = phosphor[2] - phosphor[0]
    ph = phosphor[3] - phosphor[1]
    if surround_dark:
        clipped = []
        if phosphor[0] < margin_x:
            clipped.append("left")
        if phosphor[1] < margin_y:
            clipped.append("top")
        if w - phosphor[2] < margin_x:
            clipped.append("right")
        if h - phosphor[3] < margin_y:
            clipped.append("bottom")
        if clipped:
            result["reason"] = "clipped " + ",".join(clipped)
            return result
        fill_w, fill_h = w, h
    else:
        if clipped:
            result["reason"] = "clipped " + ",".join(clipped)
            return result
        fill_w, fill_h = glass_w, glass_h
    if any(q < 8 for q in quads):
        result["reason"] = "pattern does not reach every corner of the glass"
        return result
    if pw < fill_w * 0.55 or ph < fill_h * 0.45:
        result["reason"] = "pattern does not fill the glass"
        return result
    result["ok"] = True
    result["reason"] = "full frame"
    return result


def annotate(rgb: bytes, w: int, h: int, report: dict):
    from PIL import Image, ImageDraw

    im = Image.frombytes("RGB", (w, h), rgb)
    draw = ImageDraw.Draw(im)
    glass = report.get("glass")
    phosphor = report.get("phosphor")
    if glass:
        draw.rectangle(glass, outline=(255, 220, 0), width=3)
    if phosphor:
        draw.rectangle(phosphor, outline=(80, 255, 120), width=3)
    color = (80, 255, 120) if report["ok"] else (255, 80, 80)
    draw.rectangle((0, 0, w, 36), fill=(0, 0, 0))
    draw.text((8, 8), report["reason"], fill=color)
    return im


class FrameGrabber:
    def __init__(self, device: str):
        self.proc = subprocess.Popen(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-fflags",
                "nobuffer",
                "-flags",
                "low_delay",
                "-f",
                "v4l2",
                "-input_format",
                "mjpeg",
                "-video_size",
                f"{WIDTH}x{HEIGHT}",
                "-framerate",
                "10",
                "-i",
                device,
                "-an",
                "-f",
                "rawvideo",
                "-pix_fmt",
                "rgb24",
                "-",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self.size = WIDTH * HEIGHT * 3

    def read(self) -> bytes | None:
        assert self.proc.stdout is not None
        buf = bytearray()
        while len(buf) < self.size:
            chunk = self.proc.stdout.read(self.size - len(buf))
            if not chunk:
                raise SystemExit("camera capture stopped")
            buf += chunk
        return bytes(buf)

    def close(self) -> None:
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def run_window(device: str, pattern_note: str, check: bool, timeout_s: float) -> int:
    import tkinter as tk
    from PIL import ImageTk

    if not os.environ.get("DISPLAY"):
        raise SystemExit("no DISPLAY — the camera check needs the live view")

    grabber = FrameGrabber(device)
    root = tk.Tk()
    root.title("crt-drive camera")
    view_w = 960
    view_h = 720
    status = tk.StringVar(value=pattern_note)
    label = tk.Label(root, textvariable=status, anchor="w", justify="left")
    label.pack(fill="x")
    panel = tk.Label(root)
    panel.pack()
    state = {"photo": None, "good_since": None, "code": 1, "closed": False}
    deadline = time.monotonic() + timeout_s

    def finish(code: int) -> None:
        state["code"] = code
        state["closed"] = True
        root.destroy()

    def on_close() -> None:
        finish(0 if not check else 1)

    root.protocol("WM_DELETE_WINDOW", on_close)
    root.bind("<q>", lambda _e: on_close())
    root.bind("<Escape>", lambda _e: on_close())

    def tick() -> None:
        if state["closed"]:
            return
        try:
            rgb = grabber.read()
        except SystemExit as exc:
            status.set(str(exc))
            root.after(200, lambda: finish(1))
            return
        if rgb is None:
            root.after(30, tick)
            return
        report = assess_frame(rgb, WIDTH, HEIGHT)
        shown = annotate(rgb, WIDTH, HEIGHT, report)
        shown.thumbnail((view_w, view_h))
        photo = ImageTk.PhotoImage(shown)
        state["photo"] = photo
        panel.configure(image=photo)
        line = f"{pattern_note}    {report['reason']}"
        status.set(line)
        now = time.monotonic()
        if report["ok"]:
            if state["good_since"] is None:
                state["good_since"] = now
                print("camera check: holding full frame", flush=True)
            elif check and now - state["good_since"] >= HOLD_S:
                print("camera check: full frame", flush=True)
                finish(0)
                return
        else:
            state["good_since"] = None
        if check and now >= deadline:
            print(f"camera check: timed out ({report['reason']})", flush=True)
            finish(1)
            return
        root.after(1, tick)

    print(f"camera: {device}", flush=True)
    print(pattern_note, flush=True)
    root.after(1, tick)
    root.mainloop()
    grabber.close()
    return state["code"]


def camera_check(timeout_s: float = 120.0, device: str | None = None) -> int:
    """Show the webcam and return 0 only when the framing pattern is fully in view.

    Every camera HIL calls this before trusting a glass picture.
    """
    path = find_webcam(device)
    note = request_framing_pattern()
    return run_window(path, note, check=True, timeout_s=timeout_s)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C310 live view and CRT framing check")
    parser.add_argument("--device", help="V4L2 node (default: Logitech C310)")
    parser.add_argument(
        "--check",
        action="store_true",
        help="exit 0 only after the raster pattern is fully framed",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=120.0,
        help="seconds to wait in --check (default 120)",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="print one assessment and save /tmp/crt-camera-once.jpg",
    )
    parser.add_argument(
        "--select-pattern",
        action="store_true",
        help="select the framing pattern on CDC and print the result",
    )
    args = parser.parse_args(argv)
    if args.select_pattern:
        # Local open only. The host calls this inside the Pico container.
        port = find_port()
        if port is None:
            print("no Pico CDC — framing pattern not selected")
            return 1
        try:
            fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as exc:
            print(f"CDC busy ({exc.strerror}) — framing pattern not selected")
            return 1
        try:
            configure_cdc(fd)
            code, note = _select_on_fd(fd, 2.0)
            print(note)
            return code
        finally:
            os.close(fd)
    device = find_webcam(args.device)
    note = request_framing_pattern()
    if args.once:
        print(f"camera: {device}", flush=True)
        print(note, flush=True)
        grabber = FrameGrabber(device)
        try:
            rgb = None
            for _ in range(5):
                rgb = grabber.read()
            assert rgb is not None
            report = assess_frame(rgb, WIDTH, HEIGHT)
            annotate(rgb, WIDTH, HEIGHT, report).save("/tmp/crt-camera-once.jpg", quality=90)
        finally:
            grabber.close()
        print(report["reason"], flush=True)
        print(f"glass={report['glass']} phosphor={report['phosphor']}", flush=True)
        return 0 if report["ok"] else 1
    return run_window(device, note, check=args.check, timeout_s=args.timeout)


if __name__ == "__main__":
    sys.exit(main())
