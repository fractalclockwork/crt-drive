cmake_minimum_required(VERSION 3.13)

# Firmware apps are separate CMake projects so they never share a UF2.
#   cmake -S apps/patterns -B build/patterns
#   cmake -S apps/term     -B build/term
#   cmake -S apps/demos    -B build/demos
# From the repo root: `make build` (patterns) or `make APP=<name> build`.

message(FATAL_ERROR
  "Do not configure the repo root. Pick an app:\n"
  "  cmake -S apps/patterns -B build/patterns\n"
  "  cmake -S apps/term -B build/term\n"
  "  cmake -S apps/demos -B build/demos\n"
  "Or: make build     (patterns, default)\n"
  "    make APP=term build\n"
  "    make APP=demos build"
)
