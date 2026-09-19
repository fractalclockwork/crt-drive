# pcbnew Python (KiCad 10)

Use this when generating or mutating `crt-drive.kicad_pcb`. Prefer `pcbnew` over regex on the s-expr.

```python
import pcbnew
from pcbnew import FromMM, ToMM, VECTOR2I, LoadBoard, SaveBoard

board = LoadBoard("docs/kicad/crt-drive/crt-drive.kicad_pcb")
# mutate
if not SaveBoard(path, board):
    raise RuntimeError("SaveBoard failed")
```

## Order of operations

1. `LoadBoard`
2. Snap/rotate footprints
3. **`all_pads = list(board.GetPads())` immediately** — cache net, ref, pin, position
4. Then `SetCopperLayerCount`, delete drawings/tracks, add tracks
5. `board.BuildConnectivity()` then `SaveBoard`

After `SetCopperLayerCount` or `Remove`, `GetTracks()` / `FindPadByNumber` may return Swig wrappers that are not Python objects. Collect pads before those calls. Delete old tracks with `board.Tracks()` + `.size()` indexing, not a stale iterator.

## Units and grid

Internal unit is nanometers. `FromMM(2.54)` is one protoboard hole. Snap:

```python
G = FromMM(2.54)
origin = pico.GetPosition()  # A1 pad 1
def snap_iu(v, origin_v):
    return origin_v + int(round((v - origin_v) / G)) * G
```

## Jumpers as copper

```python
board.SetCopperLayerCount(8)
# F.Cu, B.Cu, In1..In6 named Jumper1..Jumper6
```

THT pads connect all copper layers. `path_blocked` must treat any foreign pad within ~0.85 mm of a segment as a hit, regardless of the jumper's layer.

Graph-color polylines so crossing or collinear-overlapping nets (including shared routing *corners*, not only proper intersections) get different layers. Two jumpers that only meet at a real pad may share a layer.

## Netclass

Hand-wire clearance is not fab clearance. `_snap_route.py` sets default netclass clearance to 0 so DRC tracks insulated-wire intent. Do not "fix" that by spreading parts.

## GUI

If `~*.lck` exists, Pcbnew has the board open. Saving from Python still works; the user must reload or they will overwrite the agent with a stale buffer.
