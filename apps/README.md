# CRT firmware apps. Each directory is its own Pico CMake project and UF2.
#
#   make build                 # apps/cross60 → crt_cross60 (60 Hz 80-col plus, default)
#   make build APP=pattern     # apps/pattern → crt_pattern (four-mode analog setup)
#   make build APP=term        # apps/term → crt_term
#   make build APP=demos       # apps/demos → crt_demos
#   make build APP=beam        # apps/beam → crt_beam (beam-line update tests)
#   make build APP=vtty        # apps/vtty → crt_vtty (host session; docs/vtty.md)
#   make build APP=rick        # apps/rick → crt_rick (rick.gif, 2 bpp, DMA from flash)
#   make monitor               # USB CDC; follows whichever app is running
#   make test                  # HIL for APP=cross60 (default); APP=pattern|term|demos|beam|vtty|rick|hello
#
# Four-mode scanout (cross60 + pattern + demos + term + beam + vtty + rick): video/scanout.c.
# Term boots 78 Hz 80-col; host bytes are not m/r keys. USB bring-up is hello_pico/.
