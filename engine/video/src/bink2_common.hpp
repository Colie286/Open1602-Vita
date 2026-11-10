// SPDX-License-Identifier: GPL-3.0-or-later
//
// What both halves of the Bink 2 decoder share (Librempeg's libavcodec/bink2.h, see
// bink2_video.hpp): small arithmetic helpers, the order of the 16 luma blocks, the filters of
// the luma motion compensation.
// Copyright (c) 2014 Konstantin Shishkov
// Copyright (c) 2019 Paul B Mahol
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace o1602::video::bink {

template <typename T>
constexpr T clip(T v, T lo, T hi) noexcept {
    return v < lo ? lo : v > hi ? hi : v;
}
constexpr std::uint8_t clip_uint8(int v) noexcept {
    return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}
/// The middle one of three.
template <typename T>
constexpr T mid_pred(T a, T b, T c) noexcept {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

inline void copy_block(std::uint8_t* dst, const std::uint8_t* src, std::ptrdiff_t dst_stride, std::ptrdiff_t src_stride,
                       int width, int height) noexcept {
    for (int i = 0; i < height; i++) {
        std::memcpy(dst, src, static_cast<std::size_t>(width));
        dst += dst_stride;
        src += src_stride;
    }
}

/// Where the 16 luma blocks of a macroblock lie: (index & 3) across, (index >> 2) down.
inline constexpr std::array<std::uint8_t, 16> kLumaRepos = {
    0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15,
};

inline constexpr std::array<std::uint8_t, 16> kNextSkips = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0,
};

inline constexpr std::size_t kNumAcSkips = 14;

/// The half-pel filters of the luma motion compensation (LHFILTER, LVFILTER).
template <typename T>
constexpr int lh_filter(const T* src) noexcept {
    return ((((src[0] + src[1]) * 19) >> 1) - (src[-1] + src[2]) * 2 + ((src[-2] + src[3]) >> 1) + 8) >> 4;
}
template <typename T>
constexpr int lv_filter(const T* src, int i) noexcept {
    return ((((src[0] + src[i]) * 19) >> 1) - (src[-i] + src[2 * i]) * 2 + ((src[-2 * i] + src[3 * i]) >> 1) + 8) >> 4;
}

}  // namespace o1602::video::bink
