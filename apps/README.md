# CRT firmware apps. Each directory is its own Pico CMake project and UF2.
#
#   make build                 # apps/cross60 → crt_cross60 (60 Hz 80-col plus, default)
#   make build APP=pattern     # apps/pattern → crt_pattern (four-mode analog setup)
#   make build APP=term        # apps/term → crt_term
#   make build APP=demos       # apps/demos → crt_demos
#   make monitor               # USB CDC; follows whichever app is running
#   make test                  # HIL for APP=cross60 (default); APP=pattern|term|demos|hello
#
# Four-mode scanout (cross60 + pattern + demos): video/scanout.c, USB m 80/132, r 60/78.
# term still uses video/video.c (338-line 78 Hz). USB bring-up is hello_pico/.
