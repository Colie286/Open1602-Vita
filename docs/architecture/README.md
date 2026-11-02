# Architecture decisions (ADRs)

Short, dated decisions with their rationale. New decision = new file
`NNNN-title.md`; superseded ADRs are not deleted but marked as "superseded by NNNN".

| # | Title | Status |
|---|---|---|
| [0001](0001-language-and-build.md) | C++20, CMake, SDL3 | accepted |
| [0002](0002-license-and-original-data.md) | GPL-3.0, no original data in the repository | accepted |
| [0003](0003-reimplementation.md) | Reimplementation from a specification | accepted |
| [0004](0004-deterministic-simulation.md) | Deterministic, command-based simulation | accepted |
| [0005](0005-module-structure.md) | Module structure and dependency rules | accepted |
| [0006](0006-handling-original-bugs.md) | Handling bugs of the original, `sim::Rules` | accepted |
| [0007](0007-extensibility-and-modding.md) | Extensibility, content and modding (requirements, banners, open player count) | accepted |
| [0008](0008-game-settings.md) | Game settings in `sim::Rules` (endless game configurable as in later Anno titles) | accepted |
| [0009](0009-own-save-format.md) | Own save format `.o1602`; original files only read and exported | accepted |
| [0010](0010-controls-and-interface.md) | Controls and interface: modern controls (right-click, minimap, command chains), freely assignable keys, confirmations | accepted |
| [0011](0011-multiplayer-lockstep.md) | Multiplayer: lockstep through the host, resync by savegame, ENet, lobby and LAN search, host migration | accepted |
| [0012](0012-goods-statistics.md) | Goods statistics: a ledger per city booked where goods move, the window "Warenbilanz", the early warning of a shortage | accepted |
