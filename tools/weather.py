#!/usr/bin/env python3
"""San Francisco weather, news, and one zodiac sign on crt_vtty.

The cycle is weather, news, zodiac, news. Weather type is Noto Sans on the Pico
and stays up for 10 seconds. News and the day's horoscope use the glass
terminal font, typed at 400 words a minute. Each news visit continues after
the last headline. Each zodiac visit is the next sign, typed in a window under
the mark and the name. The last line of either page is held for 5 seconds.
Forecast is Open-Meteo. Headlines are Google News RSS. Horoscopes are
CosmyDay, one pull a day. Neither feed needs a key.

  uv run python tools/weather.py
"""

from __future__ import annotations

import json
import math
import os
import struct
import time
import unicodedata
import urllib.request
import xml.etree.ElementTree as ET
from datetime import date, datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

from vtty import blit_packed, connect, draw_label, relaunch_with_dialout
from vtty_proto import TYPE_AREA, TYPE_CAPS, TYPE_FILL, TYPE_GOTO, TYPE_TEXT, transact

SF_LAT = 37.7749
SF_LON = -122.4194
SF_TZ = ZoneInfo("America/Los_Angeles")
WEATHER_REFRESH_S = 600
# Weather is a fixed ten seconds. The sign is paced by the teletype, then a five-second hold.
WEATHER_S = 10
HOLD_S = 5
# 400 words a minute, 5 characters to a word.
CHAR_INTERVAL = 60.0 / (400 * 5)
# Google News RSS has no key. A fetch every 3 minutes is 480/day; the counter stops at 500.
NEWS_RSS = "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en"
NEWS_MIN_S = 180
NEWS_MAX_PER_DAY = 500
NEWS_CACHE = Path(__file__).resolve().parents[1] / ".tmp" / "news.json"
HOROSCOPE_URL = "https://api.cosmyday.com/content/daily"
FORECAST = (
    "https://api.open-meteo.com/v1/forecast"
    f"?latitude={SF_LAT}&longitude={SF_LON}"
    "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min"
    "&temperature_unit=fahrenheit&wind_speed_unit=mph"
    "&timezone=America%2FLos_Angeles&forecast_days=4"
)

# WMO weather codes. https://open-meteo.com/en/docs
KIND_NAME = {
    "clear": "Clear",
    "partly": "Partly cloudy",
    "cloud": "Overcast",
    "fog": "Fog",
    "rain": "Rain",
    "snow": "Snow",
    "storm": "Thunderstorm",
}


def kind_of(code: int) -> str:
    if code == 0:
        return "clear"
    if code in (1, 2):
        return "partly"
    if code == 3:
        return "cloud"
    if code in (45, 48):
        return "fog"
    if code in (51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82):
        return "rain"
    if code in (71, 73, 75, 77, 85, 86):
        return "snow"
    if code in (95, 96, 99):
        return "storm"
    return "cloud"


def fetch() -> dict:
    try:
        with urllib.request.urlopen(FORECAST, timeout=20) as resp:
            return json.load(resp)
    except OSError as exc:
        raise SystemExit(f"weather fetch failed: {exc}") from exc


class Icon:
    """Linear 2 bpp. Level 0 is off, 3 is bold."""

    def __init__(self, w: int, h: int) -> None:
        self.w = w - (w % 4)
        self.h = h
        self.px = [0] * (self.w * self.h)

    def set(self, x: int, y: int, color: int) -> None:
        if 0 <= x < self.w and 0 <= y < self.h and color > self.px[y * self.w + x]:
            self.px[y * self.w + x] = color

    def disc(self, cx: int, cy: int, r: int, color: int) -> None:
        r2 = r * r
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r2:
                    self.set(x, y, color)

    def ray(self, cx: int, cy: int, ang: float, r0: int, r1: int, color: int) -> None:
        for step in range(r0, r1 + 1):
            x = int(round(cx + math.cos(ang) * step))
            y = int(round(cy + math.sin(ang) * step))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    self.set(x + dx, y + dy, color)

    def pack(self) -> bytes:
        out = bytearray((self.w // 4) * self.h)
        i = 0
        for y in range(self.h):
            row = self.px[y * self.w : (y + 1) * self.w]
            for x in range(0, self.w, 4):
                byte = (
                    (row[x] << 6)
                    | (row[x + 1] << 4)
                    | (row[x + 2] << 2)
                    | row[x + 3]
                )
                out[i] = byte
                i += 1
        return bytes(out)


def _sun(icon: Icon, cx: int, cy: int, r: int) -> None:
    icon.disc(cx, cy, r, 3)
    reach = r + max(8, r // 2)
    for i in range(8):
        icon.ray(cx, cy, i * math.pi / 4, r + 3, reach, 3)


def _cloud(icon: Icon, cx: int, cy: int, scale: int) -> None:
    icon.disc(cx - scale, cy + scale // 5, scale, 2)
    icon.disc(cx + scale // 5, cy - scale // 3, int(scale * 1.15), 2)
    icon.disc(cx + scale, cy + scale // 6, int(scale * 0.85), 2)
    icon.disc(cx, cy - scale // 2, scale // 2, 3)


def draw_icon(kind: str, size: int) -> Icon:
    icon = Icon(size, size)
    c = size // 2
    if kind == "clear":
        _sun(icon, c, c, size // 6)
    elif kind == "partly":
        _sun(icon, c - size // 8, c - size // 10, size // 8)
        _cloud(icon, c + size // 14, c + size // 10, size // 7)
    elif kind == "fog":
        for i, y in enumerate(range(size // 4, size - size // 5, size // 6)):
            thick = 3 if i % 2 == 0 else 2
            inset = size // 6 if i % 2 else size // 8
            for yy in range(y, y + thick):
                for x in range(inset, size - inset):
                    icon.set(x, yy, 2 if i % 2 == 0 else 3)
    elif kind == "snow":
        _cloud(icon, c, c - size // 8, size // 6)
        for x in (c - size // 5, c, c + size // 5):
            icon.disc(x, c + size // 4, 2, 3)
    elif kind == "storm":
        _cloud(icon, c, c - size // 7, size // 6)
        bolt = [
            (c + 2, c),
            (c - size // 10, c + size // 5),
            (c + 2, c + size // 5),
            (c - size // 8, c + size // 3),
        ]
        for (x0, y0), (x1, y1) in zip(bolt, bolt[1:]):
            steps = max(abs(x1 - x0), abs(y1 - y0), 1)
            for s in range(steps + 1):
                x = x0 + (x1 - x0) * s // steps
                y = y0 + (y1 - y0) * s // steps
                icon.disc(x, y, 2, 3)
    else:
        lift = 0 if kind == "cloud" else -size // 10
        _cloud(icon, c, c + lift, size // 5)
        if kind == "rain":
            for i, x in enumerate((c - size // 5, c, c + size // 5)):
                y0 = c + size // 5 + (4 if i == 1 else 0)
                for y in range(y0, y0 + size // 6):
                    icon.set(x, y, 3)
                    icon.set(x + 1, y, 2)
    return icon


def text_width(fd: int, hunt, timeout: float, size: int, text: str) -> tuple[int, int]:
    ack = draw_label(fd, hunt, timeout, 0, 0, size, 2, text, measure=True)
    return ack.a, ack.b


def local_now() -> datetime:
    return datetime.now(SF_TZ)


def clock_strings(now: datetime | None = None) -> tuple[str, str]:
    now = local_now() if now is None else now
    return now.strftime("%-I:%M %p"), now.strftime("%a, %b %-d")


def paint_clock(
    fd: int,
    hunt,
    timeout: float,
    screen_w: int,
    box_x: int,
    box_w: int,
) -> tuple[str, str]:
    """Clear the upper-right clock and draw the time and date."""
    transact(
        fd,
        hunt,
        timeout,
        TYPE_FILL,
        struct.pack("<HHHHB", box_x, 24, box_w, 104, 0),
    )
    clock, day = clock_strings()
    right = screen_w - 24
    _width, ascent = text_width(fd, hunt, timeout, 32, clock)
    time_baseline = 36 + ascent
    draw_label(fd, hunt, timeout, right - _width, time_baseline, 32, 3, clock)
    _width, _ascent = text_width(fd, hunt, timeout, 16, day)
    draw_label(fd, hunt, timeout, right - _width, time_baseline + 36, 16, 2, day)
    return clock, day


def draw_centered(
    fd: int, hunt, timeout: float, cx: int, baseline: int, size: int, color: int, text: str
) -> None:
    width, _ascent = text_width(fd, hunt, timeout, size, text)
    draw_label(fd, hunt, timeout, cx - width // 2, baseline, size, color, text)


def paint(fd: int, hunt, timeout: float, screen_w: int, screen_h: int, report: dict) -> int:
    current = report["current"]
    daily = report["daily"]
    now_kind = kind_of(int(current["weather_code"]))
    temp = int(round(current["temperature_2m"]))
    humidity = int(round(current["relative_humidity_2m"]))
    wind = int(round(current["wind_speed_10m"]))

    transact(fd, hunt, timeout, TYPE_FILL, struct.pack("<HHHHB", 0, 0, screen_w, screen_h, 0))

    icon = draw_icon(now_kind, 128)
    blit_packed(fd, hunt, timeout, 36, 36, icon.w, icon.h, icon.pack())

    city_w, ascent = text_width(fd, hunt, timeout, 28, "San Francisco")
    draw_label(fd, hunt, timeout, 196, 36 + ascent, 28, 3, "San Francisco")
    clock_x = (196 + city_w + 16) & ~3
    if clock_x > screen_w - 160:
        clock_x = screen_w - 160
    temp_text = f"{temp}°F"
    _w, ascent = text_width(fd, hunt, timeout, 48, temp_text)
    draw_label(fd, hunt, timeout, 196, 118, 48, 3, temp_text)
    condition = KIND_NAME[now_kind]
    _w, ascent = text_width(fd, hunt, timeout, 20, condition)
    draw_label(fd, hunt, timeout, 196, 168, 20, 2, condition)
    meta = f"Humidity {humidity}%    Wind {wind} mph"
    draw_label(fd, hunt, timeout, 196, 204, 16, 2, meta)

    transact(
        fd, hunt, timeout, TYPE_FILL, struct.pack("<HHHHB", 24, 248, screen_w - 48, 2, 1)
    )

    days = daily["time"]
    codes = daily["weather_code"]
    highs = daily["temperature_2m_max"]
    lows = daily["temperature_2m_min"]
    n = min(4, len(days), len(codes), len(highs), len(lows))
    span = screen_w // n
    for i in range(n):
        col = i * span
        cx = col + span // 2
        small = draw_icon(kind_of(int(codes[i])), 56)
        ix = (cx - small.w // 2) & ~3
        blit_packed(fd, hunt, timeout, ix, 264, small.w, small.h, small.pack())
        name = "Today" if i == 0 else date.fromisoformat(days[i]).strftime("%a")
        draw_centered(fd, hunt, timeout, cx, 340, 16, 3, name)
        pair = f"{int(round(highs[i]))}° / {int(round(lows[i]))}°"
        draw_centered(fd, hunt, timeout, cx, 362, 14, 2, pair)
    paint_clock(fd, hunt, timeout, screen_w, clock_x, screen_w - clock_x)
    return clock_x


def ascii_headline(text: str) -> str:
    folded = unicodedata.normalize("NFKD", text)
    folded = folded.encode("ascii", "ignore").decode("ascii")
    return " ".join(folded.split())


def _news_state() -> dict:
    try:
        data = json.loads(NEWS_CACHE.read_text())
    except (OSError, json.JSONDecodeError):
        data = {}
    today = datetime.now(timezone.utc).date().isoformat()
    if data.get("day") != today:
        data = {
            "day": today,
            "count": 0,
            "fetched_at": 0,
            "headlines": data.get("headlines") or [],
            "last": data.get("last", -1),
            "sign": data.get("sign", -1),
            "horoscope_day": data.get("horoscope_day"),
            "horoscopes": data.get("horoscopes") or {},
        }
    data.setdefault("count", 0)
    data.setdefault("fetched_at", 0)
    data.setdefault("headlines", [])
    data.setdefault("last", -1)
    return data


def _store_news(state: dict) -> None:
    NEWS_CACHE.parent.mkdir(parents=True, exist_ok=True)
    NEWS_CACHE.write_text(json.dumps(state))


def fetch_headlines() -> list[str]:
    request = urllib.request.Request(NEWS_RSS, headers={"User-Agent": "crt-vtty"})
    with urllib.request.urlopen(request, timeout=20) as resp:
        root = ET.fromstring(resp.read())
    titles = []
    for item in root.findall(".//item"):
        title = " ".join((item.findtext("title") or "").split())
        if title:
            titles.append(title)
    if not titles:
        raise SystemExit("news feed had no headlines")
    return titles


def headlines() -> list[str]:
    """Cached Google News titles. At most one pull every 3 minutes, and under 600 a day."""
    state = _news_state()
    age = time.time() - float(state["fetched_at"])
    if state["headlines"] and (age < NEWS_MIN_S or int(state["count"]) >= NEWS_MAX_PER_DAY):
        return list(state["headlines"])
    try:
        titles = fetch_headlines()
    except (OSError, ET.ParseError, SystemExit) as exc:
        print(f"news fetch failed ({exc})", flush=True)
        return list(state["headlines"])
    state["headlines"] = titles
    state["fetched_at"] = time.time()
    state["count"] = int(state["count"]) + 1
    _store_news(state)
    print(f"news pull {state['count']} today, {len(titles)} headlines", flush=True)
    return titles


def emit(fd: int, hunt, timeout: float, data: bytes, pace: bool) -> None:
    started = time.monotonic()
    transact(fd, hunt, timeout, TYPE_TEXT, data)
    if pace:
        delay = CHAR_INTERVAL - (time.monotonic() - started)
        if delay > 0:
            time.sleep(delay)


def type_headline(fd: int, hunt, timeout: float, text: str, cols: int) -> int:
    """One character at a time. Words wrap. Returns the rows advanced, ending on a new line."""
    row = 0
    col = 0

    def newline() -> None:
        nonlocal row, col
        emit(fd, hunt, timeout, b"\r\n", False)
        row += 1
        col = 0

    def put(ch: str) -> None:
        nonlocal row, col
        emit(fd, hunt, timeout, ch.encode("ascii"), True)
        col += 1
        if col >= cols:
            row += 1
            col = 0

    words = [word for word in ascii_headline(text).split(" ") if word]
    for word in words:
        if col > 0 and col + 1 + len(word) > cols:
            newline()
        if col > 0:
            put(" ")
        for ch in word:
            put(ch)
    if col != 0:
        newline()
    return row


def show_news(
    fd: int, hunt, timeout: float, screen_w: int, screen_h: int, lines: list[str]
) -> None:
    """Type headlines from the one after last time. Each starts on its own line.

    A headline that still begins on the glass is typed in full, even if it wraps
    off the bottom. The last one stays up, and its index is kept for the next cycle.
    """
    ack = set_area(fd, hunt, timeout, 0, 0, 0, 0)
    cols = max(8, ack.a)
    screen_rows = max(1, ack.b)
    transact(
        fd,
        hunt,
        timeout,
        TYPE_FILL,
        struct.pack("<HHHHB", 0, 0, screen_w & ~3, screen_h, 0),
    )
    transact(fd, hunt, timeout, TYPE_GOTO, struct.pack("<HH", 0, 0))
    state = _news_state()
    if not lines:
        type_headline(fd, hunt, timeout, "No headlines", cols)
        time.sleep(HOLD_S)
        return
    cursor = 0
    index = (int(state.get("last", -1)) + 1) % len(lines)
    seen: set[int] = set()
    while index not in seen and cursor < screen_rows:
        seen.add(index)
        print(f"headline {index + 1}/{len(lines)}", flush=True)
        cursor += type_headline(fd, hunt, timeout, lines[index], cols)
        state["last"] = index
        _store_news(state)
        index = (index + 1) % len(lines)
    time.sleep(HOLD_S)


def hold_weather(fd: int, hunt, timeout: float, screen_w: int, clock_x: int) -> None:
    end = time.monotonic() + WEATHER_S
    shown = local_now().strftime("%H:%M")
    while time.monotonic() < end:
        stamp = local_now().strftime("%H:%M")
        if stamp != shown:
            paint_clock(fd, hunt, timeout, screen_w, clock_x, screen_w - clock_x)
            print(f"{stamp}", flush=True)
            shown = stamp
        remain = end - time.monotonic()
        if remain > 0:
            time.sleep(min(0.5, remain))


SIGNS = (
    "Aries",
    "Taurus",
    "Gemini",
    "Cancer",
    "Leo",
    "Virgo",
    "Libra",
    "Scorpio",
    "Sagittarius",
    "Capricorn",
    "Aquarius",
    "Pisces",
)


def _dot(icon: Icon, x: float, y: float, color: int, radius: int = 2) -> None:
    icon.disc(int(round(x)), int(round(y)), radius, color)


def _line(icon: Icon, x0: float, y0: float, x1: float, y1: float, color: int) -> None:
    steps = max(int(math.hypot(x1 - x0, y1 - y0)), 1)
    for i in range(steps + 1):
        t = i / steps
        _dot(icon, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, color)


def _arc(icon: Icon, cx: float, cy: float, radius: float, a0: float, a1: float, color: int) -> None:
    steps = max(int(abs(a1 - a0) * radius / 2), 8)
    for i in range(steps + 1):
        a = a0 + (a1 - a0) * i / steps
        _dot(icon, cx + math.cos(a) * radius, cy + math.sin(a) * radius, color)


def draw_sign(name: str, size: int) -> Icon:
    """A 2 bpp mark. Noto Sans Regular does not include the zodiac glyphs."""
    icon = Icon(size, size)
    c = 3
    if name == "Aries":
        _arc(icon, 34, 64, 28, math.radians(210), math.radians(10), c)
        _arc(icon, 62, 64, 28, math.radians(170), math.radians(-30), c)
    elif name == "Taurus":
        _arc(icon, 48, 58, 16, 0, math.tau, c)
        _arc(icon, 28, 34, 16, math.radians(200), math.radians(10), c)
        _arc(icon, 68, 34, 16, math.radians(170), math.radians(-20), c)
    elif name == "Gemini":
        _line(icon, 28, 20, 28, 76, c)
        _line(icon, 68, 20, 68, 76, c)
        _line(icon, 20, 22, 76, 22, c)
        _line(icon, 20, 74, 76, 74, c)
    elif name == "Cancer":
        _arc(icon, 36, 34, 14, 0, math.tau, c)
        _arc(icon, 60, 62, 14, 0, math.tau, c)
        icon.disc(30, 40, 3, c)
        icon.disc(66, 56, 3, c)
    elif name == "Leo":
        _arc(icon, 38, 34, 14, 0, math.tau, c)
        _arc(icon, 64, 64, 16, math.radians(210), math.radians(30), c)
    elif name == "Virgo":
        _line(icon, 20, 18, 20, 72, c)
        _line(icon, 38, 18, 38, 72, c)
        _line(icon, 56, 18, 56, 50, c)
        _arc(icon, 70, 58, 14, math.radians(180), math.radians(470), c)
    elif name == "Libra":
        _line(icon, 16, 72, 80, 72, c)
        _line(icon, 22, 46, 74, 46, c)
        _arc(icon, 48, 46, 22, math.radians(180), math.radians(360), c)
    elif name == "Scorpio":
        _line(icon, 18, 16, 18, 68, c)
        _line(icon, 36, 16, 36, 68, c)
        _line(icon, 54, 16, 54, 56, c)
        _line(icon, 54, 56, 78, 76, c)
        _line(icon, 78, 76, 62, 76, c)
        _line(icon, 78, 76, 78, 60, c)
    elif name == "Sagittarius":
        _line(icon, 16, 80, 80, 16, c)
        _line(icon, 50, 16, 80, 16, c)
        _line(icon, 80, 16, 80, 46, c)
        _line(icon, 30, 30, 50, 50, c)
    elif name == "Capricorn":
        _line(icon, 22, 20, 40, 68, c)
        _arc(icon, 62, 60, 16, math.radians(110), math.radians(420), c)
    elif name == "Aquarius":
        for base in (36, 58):
            prev = None
            for i in range(33):
                x = 12 + i * 2.2
                y = base + math.sin(i * 0.55) * 8
                if prev is not None:
                    _line(icon, prev[0], prev[1], x, y, c)
                prev = (x, y)
    else:
        _arc(icon, 30, 48, 22, math.radians(-80), math.radians(80), c)
        _arc(icon, 66, 48, 22, math.radians(100), math.radians(260), c)
        _line(icon, 20, 48, 76, 48, c)
    return icon


def set_area(fd: int, hunt, timeout: float, col: int, row: int, cols: int, rows: int):
    ack = transact(fd, hunt, timeout, TYPE_AREA, struct.pack("<HHHH", col, row, cols, rows))
    if ack.status != 0:
        raise SystemExit(f"terminal window rejected ({ack.status})")
    return ack


def horoscopes() -> dict[str, str]:
    """Today's readings for all twelve signs. One CosmyDay pull per UTC day."""
    state = _news_state()
    today = datetime.now(timezone.utc).date().isoformat()
    cached = state.get("horoscopes") or {}
    if state.get("horoscope_day") == today and cached:
        return cached
    try:
        request = urllib.request.Request(HOROSCOPE_URL, headers={"User-Agent": "crt-vtty"})
        with urllib.request.urlopen(request, timeout=20) as resp:
            payload = json.load(resp)
        found = {}
        for name, item in (payload.get("horoscopes") or {}).items():
            content = ((item or {}).get("content") or "").strip()
            if content:
                found[name] = content
        if not found:
            raise SystemExit("horoscope feed was empty")
    except (OSError, json.JSONDecodeError, SystemExit) as exc:
        print(f"horoscope fetch failed ({exc})", flush=True)
        return cached
    state["horoscopes"] = found
    state["horoscope_day"] = payload.get("date") or today
    _store_news(state)
    print(f"horoscope {state['horoscope_day']} {len(found)} signs", flush=True)
    return found


def horoscope_paragraphs(raw: str) -> list[str]:
    """Drop the dated title line. Each section is its own typed paragraph."""
    lines = raw.strip().splitlines()
    if len(lines) > 1:
        lines = lines[1:]
    parts = [ascii_headline(part) for part in "\n".join(lines).split("\n\n")]
    parts = [part for part in parts if part]
    if parts:
        return parts
    folded = ascii_headline(raw)
    return [folded] if folded else ["Horoscope unavailable."]


def show_zodiac(fd: int, hunt, timeout: float, screen_w: int, screen_h: int) -> None:
    """One sign: a drawn mark and a Noto name, then today's horoscope in the window."""
    state = _news_state()
    index = (int(state.get("sign", -1)) + 1) % len(SIGNS)
    name = SIGNS[index]
    text = horoscopes().get(name) or "Horoscope unavailable."
    transact(fd, hunt, timeout, TYPE_FILL, struct.pack("<HHHHB", 0, 0, screen_w, screen_h, 0))
    symbol = 96
    sx, sy = 36, 28
    mark = draw_sign(name, symbol)
    blit_packed(fd, hunt, timeout, sx, sy, mark.w, mark.h, mark.pack())
    size = 36
    _width, ascent = text_width(fd, hunt, timeout, size, name)
    draw_label(fd, hunt, timeout, sx + symbol + 24, sy + ascent, size, 3, name)

    cell_w = 9 if screen_w >= 1000 else 10
    cell_h = 13 if screen_h <= 420 else 16
    header = sy + symbol + 12
    origin = (header + cell_h - 1) // cell_h
    grid_rows = min(26, screen_h // cell_h)
    grid_cols = screen_w // cell_w
    window_rows = grid_rows - origin
    if window_rows < 2:
        raise SystemExit("zodiac window has no rows")
    ack = set_area(fd, hunt, timeout, 0, origin, grid_cols, window_rows)
    print(f"sign {index + 1}/{len(SIGNS)} {name} window {ack.a}x{ack.b}", flush=True)
    for paragraph in horoscope_paragraphs(text):
        type_headline(fd, hunt, timeout, paragraph, ack.a)
    state["sign"] = index
    _store_news(state)
    time.sleep(HOLD_S)
    set_area(fd, hunt, timeout, 0, 0, 0, 0)


def status_line(report: dict, screen_w: int, screen_h: int) -> str:
    current = report["current"]
    clock, day = clock_strings()
    return (
        f"San Francisco {int(round(current['temperature_2m']))}°F "
        f"{KIND_NAME[kind_of(int(current['weather_code']))]} "
        f"{clock} {day} on {screen_w}x{screen_h}"
    )


def main() -> int:
    relaunch_with_dialout()
    fd = None
    hunt = None
    screen_w = 0
    screen_h = 0
    report: dict | None = None
    weather_at = 0.0
    try:
        while True:
            try:
                if fd is None:
                    fd, hunt = connect(None, 20.0)
                    caps = transact(fd, hunt, 20.0, TYPE_CAPS)
                    screen_w = caps.a
                    screen_h = caps.b
                assert hunt is not None
                if report is None or time.time() - weather_at >= WEATHER_REFRESH_S:
                    report = fetch()
                    weather_at = time.time()
                clock_x = paint(fd, hunt, 20.0, screen_w, screen_h, report)
                print(status_line(report, screen_w, screen_h), flush=True)
                hold_weather(fd, hunt, 20.0, screen_w, clock_x)
                lines = headlines()
                print(f"news {len(lines)} headlines", flush=True)
                show_news(fd, hunt, 20.0, screen_w, screen_h, lines)
                show_zodiac(fd, hunt, 20.0, screen_w, screen_h)
                lines = headlines()
                print(f"news {len(lines)} headlines", flush=True)
                show_news(fd, hunt, 20.0, screen_w, screen_h, lines)
            except (SystemExit, OSError) as exc:
                print(f"link dropped ({exc}); waiting for the pico to come back", flush=True)
                if fd is not None:
                    os.close(fd)
                fd = None
                time.sleep(2.0)
    finally:
        if fd is not None:
            os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())
