#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that the repository stays a clean reimplementation (CONTRIBUTING.md, ADR 0002 and 0003).

Usage: python3 tools/check_public.py [repo root]

Looks at every file git tracks (third_party/ and LICENSE aside) and fails when one
- is a file of the original game (graphics, sound, video, scripts, maps, savegames, programs),
- cites a code or data location of the original program (a hex number of the form 0x004xxxxx to
  0x006xxxxx, or an offset into one of its records in memory such as "city + 0x1F0"),
- names a function of the original program (CamelCase_CamelCase, e.g. Foo_DoBar),
- links to documents that are not part of this repository (docs/re/), or
- uses one of a few words about tooling that does not belong here.
Each hit is printed as path:line: reason. Exit code 1 when there are any.

A line that really needs such a number (test data, say) can be changed so it does not look like
one; there is no switch to silence the check.
"""
import base64
import json
import pathlib
import re
import subprocess
import sys

SKIP_PREFIXES = ("third_party/",)
SKIP_FILES = {"LICENSE", "tools/check_public.py"}

ORIGINAL_FILES = re.compile(r"\.(bsh|col|zei|cod|gad|scp|szs|szm|gam|smk|bik|bk2|wav|exe|dll|hss|rda)$", re.IGNORECASE)

ADDRESS = re.compile(r"\b0x00[4-6][0-9A-Fa-f]{5}\b")
MEMORY_OFFSET = re.compile(r"\b(?:runtime(?: player| island| city)?|city|island|player|ship) ?\+ ?0x[0-9A-Fa-f]+"
                           r"|\b[PC]\+0x[0-9A-Fa-f]+")
FUNCTION_NAME = re.compile(r"\b(?!SDL_|SDLK_|LUA_)[A-Z][a-z][A-Za-z]*_[A-Z][a-z][A-Za-z0-9]*\b")
# Names of that shape that are not functions of the original program.
ALLOWED_NAMES = {"Endlos_Laufend"}
PRIVATE_LINK = re.compile(r"docs/re/|docs/re\b|\]\((?:\.\./)*re/")

# The terms are stored encoded so that this file does not match itself.
_TERMS = json.loads(base64.b64decode(
    "eyJ3b3JkcyI6IFsicmV2ZXJzZVstIF0/ZW5naW5lZXIiLCAiZGlzYXNzZW1ibCIsICJkZWNvbXBpbCIsICJnaGlkcmEiLCAiXFxi"
    "aWRhIHByb1xcYiIsICJcXGJ4KD86MzJ8NjQpZGJnXFxiIiwgIm9sbHlkYmciLCAiXFxicmVhLWFnZW50c1xcYiIsICJvcGVuMTYw"
    "Mi1yZVxcYiJdLCAiY2FzZV93b3JkcyI6IFsiXFxiSURBXFxiIl19"))
WORDS = re.compile("|".join(_TERMS["words"]), re.IGNORECASE)
CASE_WORDS = re.compile("|".join(_TERMS["case_words"]))

# Local working folders (each clone lists them in .git/info/exclude); a tracked file there is a mistake.
LOCAL_ONLY = re.compile(r"^(?:re|docs/re)(?:/|$)")


def tracked_files(root: pathlib.Path) -> list[str]:
    out = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True, check=True)
    return [p for p in out.stdout.decode("utf-8").split("\0") if p]


def check_line(line: str) -> list[str]:
    reasons = []
    if ADDRESS.search(line):
        reasons.append("location in the original program")
    if MEMORY_OFFSET.search(line):
        reasons.append("offset into a record of the original program in memory")
    names = [n for n in FUNCTION_NAME.findall(line) if n not in ALLOWED_NAMES]
    if names:
        reasons.append(f"function name of the original program ({', '.join(sorted(set(names)))})")
    if PRIVATE_LINK.search(line):
        reasons.append("link to a document outside this repository")
    if WORDS.search(line) or CASE_WORDS.search(line):
        reasons.append("term that does not belong in this repository")
    return reasons


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    problems = []
    for path in tracked_files(root):
        if path in SKIP_FILES or path.startswith(SKIP_PREFIXES):
            continue
        if ORIGINAL_FILES.search(path):
            problems.append(f"{path}: file of the original game")
            continue
        if LOCAL_ONLY.match(path):
            problems.append(f"{path}: local working folder")
            continue
        full = root / path
        if not full.is_file():
            continue
        data = full.read_bytes()
        if b"\0" in data[:8192]:
            continue  # binary (images, icons)
        text = data.decode("utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), 1):
            for reason in check_line(line):
                problems.append(f"{path}:{number}: {reason}")
    for problem in problems:
        print(problem)
    if problems:
        print(f"{len(problems)} problem(s); see tools/check_public.py")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
