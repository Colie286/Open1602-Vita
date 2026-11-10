#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
: "${VITASDK:?Set VITASDK to your VitaSDK installation}"
export VITASDK
export PATH="$VITASDK/bin:$PATH"
export TMPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/build/tmp"
mkdir -p "$TMPDIR"
