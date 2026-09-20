# CRT firmware apps. Each directory is its own Pico CMake project and UF2.
#
#   make build              # apps/patterns → crt_drive (analog setup, default)
#   make APP=term build     # apps/term → crt_term
#   make APP=demos build    # apps/demos → crt_demos (phosphor reel)
#   make APP=<name> build   # apps/<name> → crt_<name>
#
# Shared 78 Hz scanout is in ../../video (not a UF2). USB bring-up is hello_pico/.
