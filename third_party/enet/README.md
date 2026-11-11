# ENet 1.3.18

Reliable, ordered UDP for the multiplayer (`engine/net`, ADR 0011). Unchanged sources of
[ENet](https://github.com/lsalzman/enet) at tag `v1.3.18` (commit `2662c0de`), MIT licence
([LICENSE](LICENSE)). Only the C sources and headers are kept; `CMakeLists.txt` is Open1602's own.

To update: replace the `.c` files and `include/enet/` with those of a newer tag and adjust the
version here and in `CMakeLists.txt`.
