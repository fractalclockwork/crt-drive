# Injector power supply

Canonical power nets for the Pico + 74AHCT125 carrier. Schematic: [crt-drive/crt-drive.kicad_sch](crt-drive/crt-drive.kicad_sch). Signal map: [hardware-design.md](../hardware-design.md).

Status language: **Decided**, **Working hypothesis**, **Open**.

**Decided.** Logic-board +5 V enters **J1**. The Pico is VSYS-powered (pin 39) through D1. U1 (74AHCT125) runs from a filtered `+5V_BUFFER` rail. Common GND with the CRT harness (J2) and Pico pin 38.

Do not power the Pico from USB and the logic-board +5 V at the same time without D1; the Schottky is there to block USB back-feed into the terminal rail.

## Rails

```text
J1 +5V ── C1 10 µF ── C2 100 nF ──┬── FB1 (100 Ω @ 100 MHz) ── +5V_BUFFER
                                  │         ├── C3 10 µF
                                  │         ├── C4 100 nF
                                  │         └── C5 1 nF C0G
                                  │              U1 pin 14 VCC
                                  │              U1 pins 1,4,10,13 /OE → GND
                                  │              U1 pin 7 GND
                                  │
                                  └── D1 1N5817 (A→K) ── Pico VSYS pin 39
                                            ├── C6 10 µF
                                            └── C7 100 nF
                                                 Pico GND pin 38
```

| Net | Parts | Role |
| --- | --- | --- |
| `+5V` | J1, C1 (10 µF), C2 (100 nF) | Entry from the logic board |
| `+5V_BUFFER` | FB1, C3 (10 µF), C4 (100 nF), C5 (1 nF C0G) | Quiet 5 V for U1 only |
| `VSYS` | D1 (1N5817), C6 (10 µF), C7 (100 nF) | Pico pin 39; cathode toward VSYS |
| `GND` | J1, J2, U1 pin 7, Pico pin 38 | Common with the CRT harness |

D1 anode is on `+5V`; cathode is VSYS. DO-41 pin 1 is the cathode.

## Related

- [Hardware design](../hardware-design.md) — GPIO map, J2 harness, isolation
- [Toolchains](../toolchains.md) — USB CDC on the Pico does not replace J1 power when driving the CRT
