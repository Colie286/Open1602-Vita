# Contributing to Open1602

## Basic legal rules

1. **No original data** in the repository – neither game files nor exports generated from them
   (PNGs, decoded `.cod` texts, memory dumps with game content).
2. **No code of the original program**. Behaviour is described as a specification in `docs/` and
   implemented from it (see ADR 0003).
3. Contributions are licensed under GPL-3.0-or-later. Third-party code only with a compatible licence,
   documented in `third_party/README.md`.

## Code

- C++20, formatting via `.clang-format` (`clang-format -i <files>`), 120 characters. CI checks it with
  clang-format 18 and fails on any difference, so format your changes with that version before a pull
  request ([tests/README.md](tests/README.md#before-a-pull-request)).
- Everything in the repository is in **English**: documents, identifiers and code comments, log and
  error messages, commit messages, PR titles and descriptions. Terms from the original game data
  (`Insel`, `Haus`, `Figur`) stay recognisable. In-game texts follow the language of the installed game;
  new texts go through the localisation ([docs/modding/languages.md](docs/modding/languages.md)).
- Names: types `PascalCase`, functions/variables `snake_case`, members `m_name`, constants `kName`.
- Error handling when loading data via `core::Result<T>` – never crash on broken files.
- No owning raw pointers, no `new`/`delete`; containers and RAII.
- Simulation (`engine/sim`): deterministic, no floating point in the state (ADR 0004).
- Every file starts with `// SPDX-License-Identifier: GPL-3.0-or-later`.

## User interface

Windows and dialogs that have no layout in the original game's files (menus, questions, settings,
chat, lobby) all look and behave alike. This is binding for every new window and every change to one:

- **Build them with `ui::Form`** (`engine/ui/include/o1602/ui/form.hpp`) and the shared
  `ui::FormStyle` that `Hud` sets up for Open1602's own windows (gold frame, dark ground, the hover and
  selected colours). No hand-drawn rectangles, borders or button looks; a widget the form lacks is
  added to `ui::Form`, so that every window gets it.
- **Layout**: a framed `panel` as the window, laid out in design units of the 1024 x 768 screen and
  centred in the game view (full-screen menus: in the window). The title is a left-aligned `label` in
  the `Heading` tone, explanations are `paragraph`s, values shown only are `entry`s. Buttons are 44
  units high with centred text and 8 units apart; rows of settings are 36 units high.
- **Behaviour**: the default button is `selected`; hover and press come from the form. Enter takes
  the default, Escape cancels or closes, and numbered choices also answer to the keys 1 to 9. Player
  colours are shown as a `swatch` of the banner's palette colour.
- **Texts** go through the localisation, at least in English and German; no visible text is written into the code.
- Windows that keep the original game's look (the parchment of the goods balance) use the same
  form with their own `FormStyle`, not code of their own.

## Documentation

- Document file formats and structures in `docs/formats/`.
- Describe behaviour that matches the original game as such ("as in the original"); bugs of the
  original that Open1602 fixes are named by their id (`// Fix S-002: …`).

## Commits

- Small, self-contained commits; prefix by area, e.g. `assets: decode ZEI glyph overhang`,
  `sim: market carts skip full stores`, `docs: describe INSELHAUS bits`.
- Before committing: `cmake --build --preset debug && ctest --preset debug`, and clang-format 18 over
  the changed files. How to run single tests and the tests against the original data:
  [tests/README.md](tests/README.md).
