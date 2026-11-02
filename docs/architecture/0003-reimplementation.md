# 0003 – Reimplementation from a specification

Status: accepted · 2026-10-16

## Context
The options would be (a) patching the original program, (b) replacing it piece by piece via a hook
DLL (early OpenRCT2) or (c) a reimplementation from a specification.

## Decision
**(c) Reimplementation.** The behaviour of the original is written down as specifications (`docs/`),
and the engine is written from them cleanly and modularly. Behaviour faithful to the original is ensured by tests,
not by adopted code.

## Rationale
- Readable, extensible code (a project goal) instead of 1998-era structures with global state.
- A clean separation of simulation and presentation is hardly achievable with (a)/(b), but is required for multiplayer.
- Legally cleaner.

## Consequences
- Parity tests are mandatory: same starting position in the original (Wine) and in Open1602, compare the state after
  N ticks (`tests/parity/`, from M3).
- Bugs of the original are collected and fixed in Open1602; where that changes the feel
  of the game, it can be switched off via an "original behaviour" option.
