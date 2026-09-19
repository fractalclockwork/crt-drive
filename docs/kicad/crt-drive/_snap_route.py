#!/usr/bin/env python3
"""Snap THT footprints to the Pico 2.54 mm hole grid and route straight insulated jumpers."""

from __future__ import annotations

import os

import pcbnew
from pcbnew import (
    B_Cu,
    Cmts_User,
    Edge_Cuts,
    F_Cu,
    F_SilkS,
    FILL_T_NO_FILL,
    FromMM,
    GR_TEXT_H_ALIGN_LEFT,
    GR_TEXT_V_ALIGN_CENTER,
    In1_Cu,
    In2_Cu,
    In3_Cu,
    In4_Cu,
    In5_Cu,
    In6_Cu,
    LoadBoard,
    PCB_SHAPE,
    PCB_TEXT,
    PCB_TRACK,
    SHAPE_T_RECTANGLE,
    SaveBoard,
    ToMM,
    VECTOR2I,
)

PCB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "crt-drive.kicad_pcb")
G = FromMM(2.54)
POWER = {"+5V", "+5V_BUFFER", "GND", "/VSYS", "VSYS"}
LAYERS = [F_Cu, B_Cu, In1_Cu, In2_Cu, In3_Cu, In4_Cu, In5_Cu, In6_Cu]
LAYER_NAMES = [
    "F.Cu",
    "B.Cu",
    "Jumper1",
    "Jumper2",
    "Jumper3",
    "Jumper4",
    "Jumper5",
    "Jumper6",
]


def mm(x: float, y: float) -> VECTOR2I:
    return VECTOR2I(FromMM(x), FromMM(y))


def snap_iu(v: int, origin: int) -> int:
    return origin + int(round((v - origin) / G)) * G


def same_point(a: VECTOR2I, b: VECTOR2I, tol: int = 50000) -> bool:
    return abs(a.x - b.x) <= tol and abs(a.y - b.y) <= tol


def aligned(a: VECTOR2I, b: VECTOR2I, tol: int = FromMM(0.2)) -> bool:
    return abs(a.x - b.x) <= tol or abs(a.y - b.y) <= tol


def straighten(a: VECTOR2I, b: VECTOR2I, tol: int = FromMM(0.2)) -> tuple[VECTOR2I, VECTOR2I]:
    if abs(a.x - b.x) <= tol:
        x = a.x
        return VECTOR2I(x, a.y), VECTOR2I(x, b.y)
    if abs(a.y - b.y) <= tol:
        y = a.y
        return VECTOR2I(a.x, y), VECTOR2I(b.x, y)
    return a, b


HIT = FromMM(0.85)


def point_near_segment(p: VECTOR2I, a: VECTOR2I, b: VECTOR2I, hit: int = HIT) -> bool:
    if abs(a.y - b.y) <= FromMM(0.2):
        if abs(p.y - a.y) > hit:
            return False
        lo, hi = (a.x, b.x) if a.x <= b.x else (b.x, a.x)
        return lo - hit <= p.x <= hi + hit
    if abs(a.x - b.x) <= FromMM(0.2):
        if abs(p.x - a.x) > hit:
            return False
        lo, hi = (a.y, b.y) if a.y <= b.y else (b.y, a.y)
        return lo - hit <= p.y <= hi + hit
    return False


def path_blocked(path: list[VECTOR2I], net: str, pads: list, ends: list[VECTOR2I]) -> bool:
    for p1, p2 in polyline_segments(path):
        for pad in pads:
            if pad["net"] == net:
                continue
            pos = pad["pos"]
            if any(same_point(pos, e, HIT) for e in ends):
                continue
            if point_near_segment(pos, p1, p2):
                return True
    return False


def grid_range(v0: int, v1: int, origin: int, extra: int = 4) -> list[int]:
    lo, hi = (v0, v1) if v0 <= v1 else (v1, v0)
    start = snap_iu(lo, origin) - extra * G
    stop = snap_iu(hi, origin) + extra * G
    vals = []
    v = start
    while v <= stop:
        vals.append(v)
        v += G
    return vals


def manhattan_path(a: VECTOR2I, b: VECTOR2I, net: str, pads: list, origin_x: int, origin_y: int) -> list[VECTOR2I]:
    a2, b2 = straighten(a, b)
    ends = [a, b]
    candidates: list[list[VECTOR2I]] = []
    if a2.x == b2.x or a2.y == b2.y:
        candidates.append([a, b])
    candidates.append([a, VECTOR2I(b.x, a.y), b])
    candidates.append([a, VECTOR2I(a.x, b.y), b])
    for x in grid_range(a.x, b.x, origin_x):
        if abs(x - a.x) < FromMM(0.4) or abs(x - b.x) < FromMM(0.4):
            continue
        candidates.append([a, VECTOR2I(x, a.y), VECTOR2I(x, b.y), b])
    for y in grid_range(a.y, b.y, origin_y):
        if abs(y - a.y) < FromMM(0.4) or abs(y - b.y) < FromMM(0.4):
            continue
        candidates.append([a, VECTOR2I(a.x, y), VECTOR2I(b.x, y), b])
    half = G // 2
    for x in (a.x + half, a.x - half, b.x + half, b.x - half):
        candidates.append([a, VECTOR2I(x, a.y), VECTOR2I(x, b.y), b])
    for y in (a.y + half, a.y - half, b.y + half, b.y - half):
        candidates.append([a, VECTOR2I(a.x, y), VECTOR2I(b.x, y), b])
    for sx in (-G, G):
        for sy in (-G, G):
            candidates.append(
                [
                    a,
                    VECTOR2I(a.x + sx, a.y),
                    VECTOR2I(a.x + sx, b.y + sy),
                    VECTOR2I(b.x, b.y + sy),
                    b,
                ]
            )
            candidates.append(
                [
                    a,
                    VECTOR2I(a.x, a.y + sy),
                    VECTOR2I(b.x + sx, a.y + sy),
                    VECTOR2I(b.x + sx, b.y),
                    b,
                ]
            )

    def score(path: list[VECTOR2I]) -> tuple:
        segs = polyline_segments(path)
        length = sum(dist(p, q) for p, q in segs)
        return (len(segs), length)

    clear = [p for p in candidates if not path_blocked(p, net, pads, ends)]
    if clear:
        clear.sort(key=score)
        return clear[0]
    candidates.sort(key=score)
    print(f"  warn: no clear path for {net}")
    return candidates[0]


def polyline_segments(pts: list[VECTOR2I]) -> list[tuple[VECTOR2I, VECTOR2I]]:
    return [(pts[i], pts[i + 1]) for i in range(len(pts) - 1) if not same_point(pts[i], pts[i + 1])]


def dist(a: VECTOR2I, b: VECTOR2I) -> float:
    dx = a.x - b.x
    dy = a.y - b.y
    return (dx * dx + dy * dy) ** 0.5


def segments_cross(p1: VECTOR2I, p2: VECTOR2I, q1: VECTOR2I, q2: VECTOR2I) -> bool:
    """True if open segments properly intersect (not at a shared endpoint)."""
    if (
        same_point(p1, q1)
        or same_point(p1, q2)
        or same_point(p2, q1)
        or same_point(p2, q2)
    ):
        return False

    def side(a, b, c) -> int:
        v = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)
        if v > 0:
            return 1
        if v < 0:
            return -1
        return 0

    s1 = side(p1, p2, q1)
    s2 = side(p1, p2, q2)
    s3 = side(q1, q2, p1)
    s4 = side(q1, q2, p2)
    if s1 == 0 or s2 == 0 or s3 == 0 or s4 == 0:
        return False
    return s1 != s2 and s3 != s4


def add_text(board, s, x, y, layer=F_SilkS, size=0.8):
    t = PCB_TEXT(board)
    t.SetText(s)
    t.SetLayer(layer)
    t.SetPosition(mm(x, y))
    t.SetTextSize(VECTOR2I(FromMM(size), FromMM(size)))
    t.SetTextThickness(FromMM(0.12))
    t.SetHorizJustify(GR_TEXT_H_ALIGN_LEFT)
    t.SetVertJustify(GR_TEXT_V_ALIGN_CENTER)
    board.Add(t)


def collect_all_pads(board):
    pads = []
    for pad in board.GetPads():
        fp = pad.GetParentFootprint()
        pads.append(
            {
                "ref": str(fp.GetReference()) if fp else "?",
                "pin": str(pad.GetNumber()),
                "pos": pad.GetPosition(),
                "net": str(pad.GetNetname() or ""),
                "netitem": pad.GetNet(),
            }
        )
    return pads


def spanning_jumpers(pads: list) -> list[tuple]:
    n = len(pads)
    if n < 2:
        return []
    edges = []
    for i in range(n):
        for j in range(i + 1, n):
            a, b = pads[i]["pos"], pads[j]["pos"]
            w = dist(a, b)
            if aligned(a, b):
                w *= 0.01
            edges.append((w, i, j))
    edges.sort()
    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    jumpers = []
    for _w, i, j in edges:
        a, b = find(i), find(j)
        if a == b:
            continue
        parent[a] = b
        jumpers.append((pads[i], pads[j]))
        if len(jumpers) == n - 1:
            break
    return jumpers


def collinear_overlap(p1, p2, q1, q2, tol: int = FromMM(0.2)) -> bool:
    """True if two axis-aligned segments overlap in their interiors."""
    def iv(a0, a1, b0, b1) -> bool:
        lo_a, hi_a = (a0, a1) if a0 <= a1 else (a1, a0)
        lo_b, hi_b = (b0, b1) if b0 <= b1 else (b1, b0)
        return lo_a < hi_b - tol and lo_b < hi_a - tol

    if abs(p1.y - p2.y) <= tol and abs(q1.y - q2.y) <= tol and abs(p1.y - q1.y) <= tol:
        return iv(p1.x, p2.x, q1.x, q2.x)
    if abs(p1.x - p2.x) <= tol and abs(q1.x - q2.x) <= tol and abs(p1.x - q1.x) <= tol:
        return iv(p1.y, p2.y, q1.y, q2.y)
    return False


def jumpers_conflict(path_a, path_b) -> bool:
    segs_a = polyline_segments(path_a)
    segs_b = polyline_segments(path_b)
    for a, b in segs_a:
        for c, d in segs_b:
            if segments_cross(a, b, c, d) or collinear_overlap(a, b, c, d):
                return True
            if same_point(a, c) or same_point(a, d) or same_point(b, c) or same_point(b, d):
                return True
    return False


def color_paths(paths: list) -> list[int]:
    colors = [-1] * len(paths)
    for i, path in enumerate(paths):
        used = set()
        for j in range(i):
            if colors[j] < 0:
                continue
            if jumpers_conflict(path, paths[j]):
                used.add(colors[j])
        color = 0
        while color in used:
            color += 1
        if color >= len(LAYERS):
            raise RuntimeError("need more jumper layers")
        colors[i] = color
    return colors


def main() -> None:
    board = LoadBoard(PCB)
    footprints = list(board.GetFootprints())
    pico = board.FindFootprintByReference("A1")
    if pico is None:
        raise RuntimeError("A1 Pico missing")
    ox, oy = pico.GetPosition().x, pico.GetPosition().y

    print("grid origin A1 pad1", ToMM(ox), ToMM(oy))
    minx = miny = 10**18
    maxx = maxy = -(10**18)
    for fp in footprints:
        deg = fp.GetOrientationDegrees()
        snapped_deg = round(deg / 90.0) * 90.0
        fp.SetOrientationDegrees(snapped_deg)
        pos = fp.GetPosition()
        fp.SetPosition(VECTOR2I(snap_iu(pos.x, ox), snap_iu(pos.y, oy)))
        print(
            f"  {fp.GetReference():3} rot {snapped_deg:4.0f} "
            f"({ToMM(fp.GetPosition().x):8.2f}, {ToMM(fp.GetPosition().y):8.2f})"
        )
        bbox = fp.GetBoundingBox(False, False)
        minx = min(minx, bbox.GetLeft())
        maxx = max(maxx, bbox.GetRight())
        miny = min(miny, bbox.GetTop())
        maxy = max(maxy, bbox.GetBottom())

    all_pads = collect_all_pads(board)
    by_net: dict[str, list] = {}
    for p in all_pads:
        net = p["net"]
        if not net or net.startswith("unconnected"):
            continue
        if p["ref"] == "A1" and net == "GND" and p["pin"] != "38":
            continue
        by_net.setdefault(net, []).append(p)

    j1_pads = {}
    j2_pads = {}
    j1 = board.FindFootprintByReference("J1")
    j2 = board.FindFootprintByReference("J2")
    a1 = board.FindFootprintByReference("A1")
    if j1:
        for n in ("1", "2"):
            j1_pads[n] = j1.FindPadByNumber(n).GetPosition()
    if j2:
        for n in ("1", "2", "3", "4", "5"):
            j2_pads[n] = j2.FindPadByNumber(n).GetPosition()

    # Drop leftover silk/comments/outline; jumpers replace Eco guides.
    for item in list(board.GetDrawings()):
        board.Remove(item)

    board.SetCopperLayerCount(8)
    for layer, name in zip(LAYERS, LAYER_NAMES):
        board.SetLayerName(layer, name)
    ds = board.GetDesignSettings()
    ds.m_MinClearance = 0
    try:
        nc = ds.m_NetSettings.GetDefaultNetclass()
        nc.SetClearance(0)
    except Exception:
        pass

    tracks = board.Tracks()
    if hasattr(tracks, "size"):
        old = [tracks[i] for i in range(tracks.size())]
        for tr in old:
            board.Remove(tr)

    all_jumpers = []
    for net, pads in sorted(by_net.items()):
        unique = []
        seen = []
        for p in pads:
            if any(same_point(p["pos"], q["pos"]) for q in unique):
                continue
            unique.append(p)
        js = spanning_jumpers(unique)
        print(f"net {net}: {len(unique)} pads, {len(js)} jumpers")
        for a, b in js:
            path = manhattan_path(a["pos"], b["pos"], net, all_pads, ox, oy)
            all_jumpers.append((a, b, net, a["netitem"], path))

    paths = [p for _a, _b, _n, _ni, p in all_jumpers]
    colors = color_paths(paths)
    used_layers = sorted(set(colors))
    print("jumper layers used", used_layers)

    for (a, b, net, ni, path), color in zip(all_jumpers, colors):
        width = FromMM(0.8 if net in POWER or net.endswith("VSYS") else 0.45)
        for p0, p1 in polyline_segments(path):
            tr = PCB_TRACK(board)
            tr.SetStart(p0)
            tr.SetEnd(p1)
            tr.SetWidth(width)
            tr.SetLayer(LAYERS[color])
            if ni is not None:
                tr.SetNet(ni)
            board.Add(tr)
        print(
            f"  {net:16} {a['ref']}.{a['pin']:>2} -> {b['ref']}.{b['pin']:<2}  "
            f"L{color}  {[(round(ToMM(p.x),2), round(ToMM(p.y),2)) for p in path]}"
        )

    # Outline around parts, 2-hole margin.
    margin = 2 * G
    x0, y0 = snap_iu(minx - margin, ox), snap_iu(miny - margin, oy)
    x1, y1 = snap_iu(maxx + margin, ox), snap_iu(maxy + margin, oy)
    outline = PCB_SHAPE(board, SHAPE_T_RECTANGLE)
    outline.SetLayer(Edge_Cuts)
    outline.SetWidth(FromMM(0.15))
    outline.SetFilled(False)
    outline.SetFillMode(FILL_T_NO_FILL)
    outline.SetStart(VECTOR2I(x0, y0))
    outline.SetEnd(VECTOR2I(x1, y1))
    board.Add(outline)

    if j1_pads:
        for pad, label in (("1", "+5V"), ("2", "GND")):
            p = j1_pads[pad]
            add_text(board, label, ToMM(p.x) - 5.2, ToMM(p.y), size=0.8)
    if j2_pads:
        for pad, label in (("1", "V0"), ("2", "V1"), ("3", "H"), ("4", "V"), ("5", "GND")):
            p = j2_pads[pad]
            add_text(board, label, ToMM(p.x) - 4.6, ToMM(p.y), size=0.8)
    add_text(
        board,
        "Straight insulated jumpers (layers may cross).",
        ToMM(x0) + 2,
        ToMM(y1) - 2.2,
        layer=Cmts_User,
        size=1.0,
    )
    if a1:
        add_text(
            board,
            "USB",
            ToMM(a1.GetPosition().x) + 6,
            ToMM(a1.GetPosition().y) - 4,
            layer=Cmts_User,
            size=1.0,
        )

    board.BuildConnectivity()
    if not SaveBoard(PCB, board):
        raise RuntimeError("SaveBoard failed")
    print("saved", PCB)


if __name__ == "__main__":
    main()
