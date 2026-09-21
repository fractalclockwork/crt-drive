# CRT firmware apps. Each directory is its own Pico CMake project and UF2.
#
#   make build                 # apps/cross60 → crt_cross60 (60 Hz 80-col plus, default)
#   make build APP=pattern     # apps/pattern → crt_pattern (78 Hz analog setup)
#   make build APP=term        # apps/term → crt_term
#   make build APP=demos       # apps/demos → crt_demos
#   make monitor               # USB CDC; follows whichever app is running
#   make test                  # HIL for APP=cross60 (default); APP=pattern|term|demos|hello
#
# 60 Hz scanout lives in this tree (hsync60/vsync60 + 78 Hz vsync). USB CDC: 1–4 patterns,
# m toggles 80-col / 132-col, r toggles 60/78 Hz, a/d H phase, w/s V porch
# (`make monitor`). Four modes HIL-settled in docs/test-pattern-design.md.
# Shared 78 Hz scanout for APP=pattern is in ../../video (not a UF2; still 338-line).
# USB bring-up is hello_pico/.
