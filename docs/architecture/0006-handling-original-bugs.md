# 0006 – Handling bugs of the original

Status: accepted · 2026-10-16

## Context
Anno 1602 has technical problems on today's systems, gameplay bugs and exploits, as well as
fixed limits whose overflow leads to misbehaviour. They are collected in a bug catalogue with ids
such as S-002. Open1602 should not carry them over, but it should be able to load
savegames of the original and preserve the feel of the game.

## Decision
1. **Technical (T, M):** Not reproduced. The platform layer (SDL3), our own loaders and our
   own networking replace the original components entirely.
2. **Gameplay (S):** Bugs and exploits are **always fixed**, without a switch.
   Three rules apply to all simulation code for this:
   - *Commands validate themselves.* Every player action is a `Command` (ADR 0004). When executing it, the simulation
     checks owner, range, costs, building and build level rules. The interface
     must not allow anything the command does not check. (S-005, S-006, S-010 … S-015, S-017)
   - *The entire state belongs to the savegame.* Loading and saving do not change the game.
     This also covers disasters, pending events and the player assignment. A test loads
     a saved state, simulates N ticks and compares the hash with the original run straight through.
     (S-001, S-007, S-008, S-016)
   - *References via stable IDs, never via array positions.* Carriers, animals, goods and missions belong
     to an object. If it disappears, they are dissolved. (S-002, S-009)
3. **Limits (L, S-020, S-021):** There are no silent technical limits. Containers grow
   dynamically, pools hand out stable IDs. Where a limit is needed (file format, balance), it is
   named, checked and produces an error message instead of misbehaviour.
4. **Only what affects balance or the feel of the game can be switched off.** Such points live in
   `sim::Rules`. The default is the corrected behaviour, `Rules::original()` returns the
   original behaviour. `Rules` is part of the savegame and of the multiplayer session, so that all
   participants have the same rules.
   Since 2026-10-27 this also covers gameplay bugs whose fix changes how the game plays (point 2
   aside): they can be brought back one by one in `Rules::bugs` (ADR 0008, "Original settings";
   S-002, S-003, S-005, S-008, S-012, S-013, S-049, S-055, S-056, S-058 and the computer players
   of S-097 to S-099). The fix stays the default.
5. **Data bugs (D):** The `.cod` interpreter stays faithful to the original. Corrections come as
   data patches that can be switched off and are applied after loading (`assets/data_fixes.hpp`,
   `ui::Font::fix_accents`; `data_fixes` in `settings.cfg`, `--original-data`). A patch replaces only
   the value the original files have, so changed or modded data stays as it is. The patches so far
   may differ between the computers of a game (picture, sound, the places of a formation, which the
   interface sends with the order); one the simulation itself depends on would belong in `sim::Rules`.
6. **Traceability:** Code that fixes an entry names its id (`// Fix S-002: …`).
   Every correction gets a test.

## Savegames of the original
Original `.GAM` files can contain states that only arose through a bug,
such as buildings on former river tiles (S-009) or more than 33 ships. They are checked on import.
Impossible states are repaired or reported, never silently adopted.

## Consequences
- Some speedrun tricks and exploits do not work in Open1602. That is intended.
- Parity tests against the original (plan, section 3) must avoid scenarios that trigger a
  fixed bug, or use `Rules::original()` where possible.
