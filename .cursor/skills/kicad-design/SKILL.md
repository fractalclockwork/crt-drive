---
name: kicad-design
description: >-
  Designs and validates this repo's KiCad 10 schematic and hand-wired
  protoboard (ERC, DRC, pcbnew Python, insulated jumpers). Use when editing
  .kicad_sch, .kicad_pcb, .kicad_pro, docs/kicad, footprints, nets, routing,
  jumpers, Pcbnew, or when the user asks for schematic, PCB, ERC, or DRC.
---

# KiCad schematic and PCB

## Project

| Item | Path |
| --- | --- |
| Project | [`docs/kicad/crt-drive/`](../../../docs/kicad/crt-drive/) |
| Electrical source of truth | [`docs/hardware-design.md`](../../../docs/hardware-design.md), [`docs/kicad/power_supply.md`](../../../docs/kicad/power_supply.md) |
| Regenerator | [`docs/kicad/crt-drive/_snap_route.py`](../../../docs/kicad/crt-drive/_snap_route.py) |

KiCad **10** on the Dev-Host (`kicad-cli --version`). This board is **through-hole protoboard**, stuffed by hand with **insulated jumper wires**. It is not a fabbed 2-layer PCB.

## Do this every time

1. Read the schematic/PCB already in git. Do not rebuild from a blank sheet.
2. Change the schematic (or PCB) to match **Decided** hardware notes.
3. Validate with [`scripts/validate.sh`](scripts/validate.sh). Fix real errors. Re-run until the report matches the expected exceptions below.
4. If Pcbnew/Eeschema is open, tell the user to **reload** after `SaveBoard` / file overwrite.

Never install a second KiCad. Never commit `.history/`, `*.kicad_prl`, `*.lck`, or autosaves (see root `.gitignore`).

## Schematic

- GPIO map is decided: Pico **GPIO0 V0**, **GPIO1 V1**, **GPIO2 /HSYNC**, **GPIO3 /VSYNC** into 74AHCT125 (U1) then J2 `V0 V1 H V GND`.
- Power: J1 `+5V`/`GND` → C1/C2 → D1 (1N5817) to Pico **VSYS pin 39**; FB1 to `+5V_BUFFER` and C3/C4/C5 at U1. Pico GND tap is **pin 38** only.
- Series damping: **R1/R2 100 Ω** on V0/V1; **R3/R4 47–68 Ω** on H/V.
- Footprints live on **symbol instances**, not in `lib_symbols`. Editing library-cache footprints causes ERC `lib_symbol_mismatch`.
- Every UUID must be unique. Copy symbols from `/usr/share/kicad/symbols/`; keep `lib_id` strings matching those libraries.
- D1 DO-41 **pin 1 is K** (cathode toward VSYS).

After schematic edits: ERC **0 errors**. Warnings only if they match an explicit exception.

## PCB (hand-wire)

Keep the user's placement. **Snap, do not rearrange.** Grid origin is Pico **A1 pad 1**, pitch **2.54 mm**. Disc P5.00 and radial P2.50 pads may sit slightly off-grid.

Insulated jumpers:

- Orthogonal **H/V** only (no diagonals). Straight runs beat short diagonals.
- Model crossings as extra copper layers: `F.Cu`, `B.Cu`, `Jumper1`–`Jumper6` (`In1.Cu`–`In6.Cu`). Same-layer crossings are wrong; different-layer crossings are the point.
- THT pads plate **every** copper layer. A track through a foreign pad is a short even on `JumperN`. Route in hole-grid alleys (including G/2 between DIP rows).
- Do not jumper Pico GND pins **3, 8, 13, 18, 23, 28, 33** — they are common inside the module.

Prefer `_snap_route.py` (or pcbnew Python) over hand-editing track sexpr. Details: [`pcbnew.md`](pcbnew.md).

## Validation

From repo root:

```bash
.cursor/skills/kicad-design/scripts/validate.sh
```

| Report item | Treat as |
| --- | --- |
| ERC errors | Must fix |
| `lib_symbol_mismatch` | You edited `lib_symbols`; revert those footprints |
| DRC shorts or same-layer `tracks_crossing` | Must fix: recolor/reroute onto another jumper layer |
| Unconnected Pico GND 8/13/18/23/28/33 | Expected |
| A1 pad 36 missing `+3V3` | Expected (board is VSYS-powered) |
| D1/C6 courtyard / PTH-in-courtyard | Acceptable if the user packed them |
| Clearance vs 0.20 mm on G/2 alleys | Acceptable for insulated wire; min clearance may be 0 |
| THT silk overlap / silk-over-mask | Ignore |
| `footprint_symbol_field_mismatch` Description/Datasheet | Ignore (schematic fields not copied to PCB) |
| `lib_footprint_mismatch` Pico THT | Expected (instance is `RaspberryPi_Pico_Common_THT`) |

Schematic-parity **net/pin** mismatches are real. Field-text mismatches are not.

## Safety

This skill is about the injector protoboard files. It is not permission to power a CRT. Isolation notes stay in the README.
