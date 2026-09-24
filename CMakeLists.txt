cmake_minimum_required(VERSION 3.13)

# Firmware apps are separate CMake projects so they never share a UF2.
#   cmake -S apps/cross60 -B build/cross60
#   cmake -S apps/pattern -B build/pattern
#   cmake -S apps/term    -B build/term
#   cmake -S apps/demos   -B build/demos
#   cmake -S apps/vtty    -B build/vtty
#   cmake -S apps/rick    -B build/rick
# From the repo root: `make build` (cross60) or `make build APP=<name>`.

message(FATAL_ERROR
  "Do not configure the repo root. Pick an app:\n"
  "  cmake -S apps/cross60 -B build/cross60\n"
  "  cmake -S apps/pattern -B build/pattern\n"
  "  cmake -S apps/term -B build/term\n"
  "  cmake -S apps/demos -B build/demos\n"
  "  cmake -S apps/vtty -B build/vtty\n"
  "  cmake -S apps/rick -B build/rick\n"
  "Or: make build              (cross60, default)\n"
  "    make build APP=pattern\n"
  "    make build APP=term\n"
  "    make build APP=demos\n"
  "    make build APP=vtty\n"
  "    make build APP=rick"
)
