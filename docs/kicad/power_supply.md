```
+5V MAIN INPUT RAIL
                          (+5V DC Source)
                                |
     +--------------------------+------------------------------+
     |                          |                              |
   +---+ C1                   +---+ C2                       +---+ FB1 (Ferrite Bead)
  ===  | 10 µF               ===  | 100 nF                  --- --- 100 Ω @ 100 MHz
   |   | Electrolytic/Ceramic |   | Ceramic                  |   |
   +---+                      +---+                          +---+
     |                          |                              | +5V_BUFFER
    GND                        GND                             +-----------------------+-------------------+
                                                               |                       |                   |
                                                             +---+ C3                +---+ C4            +---+ C5
                                                            ===  | 10 µF            ===  | 100 nF       ===  | 1 nF
                                                             |   | X7R Ceramic       |   | X7R Ceramic   |   | C0G MLCC
                                                             +---+                   +---+               +---+
                                                               |                       |                   |
                                                              GND                     GND                 GND
                                                                                       |
                                                                                       v
                                                                             [ 74AHCT125 IC Pin 14: VCC ]
                                                                             [ 74AHCT125 IC Pin 7:  GND ]
                                                                             [ 74AHCT125 Pins 1,4,10,13: /OE ] --> GND
```
```
     +---------------------------------------------------------+
     |
     | D1 (1N5817)
   +---+
   |   | Anode
   \   / (Schottky Diode, 1A / 20V)
    \ /
   ---
     | Cathode
     |
     +--------------------+--------------------> [ Raspberry Pi Pico Pin 39: VSYS ]
     |                    |                      [ Raspberry Pi Pico Pin 38: GND  ]
   +---+ C6             +---+ C7
  ===  | 10 µF         ===  | 100 nF
   |   | Ceramic        |   | Ceramic
   +---+                +---+
     |                    |
    GND                  GND
```