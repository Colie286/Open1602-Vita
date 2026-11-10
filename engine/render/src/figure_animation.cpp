// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/figure_animation.hpp"

namespace o1602::render {

namespace {

std::uint32_t next_random(std::uint32_t& state) noexcept {
    // xorshift32
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

}  // namespace

AnimState start_animation(const assets::FigureType& type, std::uint8_t anim, std::uint32_t seed) noexcept {
    AnimState s;
    s.anim = anim;
    s.random = seed * 2654435761U + 0x9E3779B9U;
    if (s.random == 0) {
        s.random = 1;
    }
    if (const assets::FigureAnim* a = type.anim(anim)) {
        s.frame = static_cast<std::uint8_t>(next_random(s.random) % a->count);
        s.elapsed_ms = next_random(s.random) % a->speed_ms;
    }
    return s;
}

void advance_animation(AnimState& state, const assets::FigureType& type, std::uint32_t dt_ms) noexcept {
    const assets::FigureAnim* a = type.anim(state.anim);
    if (a == nullptr) {
        return;
    }
    state.elapsed_ms += dt_ms;
    while (state.elapsed_ms >= a->speed_ms) {
        state.elapsed_ms -= a->speed_ms;
        if (++state.frame < a->count) {
            continue;
        }
        state.frame = 0;
        if (++state.runs < a->repeat) {
            continue;
        }
        state.runs = 0;
        std::uint8_t next = state.anim;
        if (a->kind == assets::AnimKind::JumpTo && !a->next.empty()) {
            next = a->next.front();
        } else if (a->kind == assets::AnimKind::Random && !a->next.empty()) {
            next = a->next[next_random(state.random) % a->next.size()];
        }
        if (next != state.anim) {
            const assets::FigureAnim* n = type.anim(next);
            if (n == nullptr) {
                return;  // broken data: stay on the last frame instead of looping on nothing
            }
            state.anim = next;
            a = n;
        }
    }
}

std::uint32_t walking_pace(std::uint32_t& rest, std::uint32_t dt_ms, std::uint32_t walk_speed) noexcept {
    const std::uint32_t speed = walk_speed != 0 ? walk_speed : 1;
    const std::uint64_t scaled = std::uint64_t{dt_ms} * 32U + rest;
    rest = static_cast<std::uint32_t>(scaled % speed);
    return static_cast<std::uint32_t>(scaled / speed);
}

}  // namespace o1602::render
