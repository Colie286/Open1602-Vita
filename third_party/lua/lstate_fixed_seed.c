/* SPDX-License-Identifier: MIT */
/*
** open1602: lstate.c with a fixed string hash seed. Lua normally seeds its string hashes from the
** clock and from addresses, which changes the order in which pairs() visits string keys from run
** to run. Scripts of a game have to give the same results on every computer and in every replay,
** so the seed is a constant here. lstate.c itself stays unchanged.
*/

#define luai_makeseed(L) 0u

#include "lstate.c"
