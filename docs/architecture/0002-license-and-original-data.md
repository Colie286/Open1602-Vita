# 0002 – GPL-3.0, no original data in the repository

Status: accepted · 2026-10-16

## Decision
- Licence: **GPL-3.0-or-later** (like OpenRCT2 and OpenTTD). Every source file carries
  `SPDX-License-Identifier: GPL-3.0-or-later`.
- The repository contains **no** files of the original game (graphics, sound, video, texts, maps,
  binaries), no exports generated from them (PNG dumps, decoded `.cod` texts) and no code of the original program.
- The engine needs a legally acquired installation; the path is configured
  (`ANNO1602_DIR`, later a settings dialog).

## Rationale
A common, legally established model for engine reimplementations. The GPL ensures that
community improvements stay open.

## Consequences
- `.gitignore` excludes typical game files and export folders.
- Tests against original data only run when `ANNO1602_DIR` is set (skipped in CI).
- Third-party code only with a GPL-3.0-compatible licence; document its origin in `third_party/README.md`.
