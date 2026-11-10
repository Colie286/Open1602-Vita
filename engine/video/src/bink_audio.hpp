// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bink Audio, the sound of Bink and Bink 2 videos: blocks of quantised frequency coefficients,
// turned back into samples by an inverse DCT or an inverse real DFT, with a short crossfade
// between blocks. Ported from FFmpeg's binkaudio.c, dct.c and rdft.c (LGPL-2.1-or-later;
// Copyright (c) 2007-2011 Peter Ross, Copyright (c) 2009 Daniel Verkamp, Copyright (c) 2009
// Alex Converse), with the coefficients of Bink Audio 2 ("KB2n") from Librempeg's binkaudio.c.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::video::bink {

class Bits;

/// The inverse transforms of one frame length of 2^nbits: FFmpeg's DCT-III (dct.c) or its
/// DFT_C2R real DFT (rdft.c), in place.
class BinkTransform {
public:
    BinkTransform(int nbits, bool dct);
    void run(float* data);

private:
    void fft(float* data);
    void rdft(float* data);

    int m_n;
    bool m_dct;
    bool m_fft_inverse;
    bool m_negative_sin;
    float m_sign_convention;
    std::vector<float> m_tcos;  ///< cos(2 pi i / n), i < n / 4
    std::vector<float> m_tsin;  ///< sin(2 pi i / n)
    std::vector<float> m_dcos;  ///< DCT: cos(pi i / (2 n)), i <= n
    std::vector<float> m_csc2;  ///< DCT: 0.5 / sin(pi (2 i + 1) / (2 n))
    std::vector<float> m_fft_cos;
    std::vector<float> m_fft_sin;
    std::vector<std::uint32_t> m_bitrev;
};

class BinkAudio {
public:
    /// A track of `rate` Hz with `channels` (1 or 2), DCT or RDFT coded; `revision` is the
    /// container's ("KB2j": 'j'): 'n' is Bink Audio 2.
    BinkAudio(std::uint32_t rate, std::uint8_t channels, bool use_dct, char revision);

    /// Decodes a frame's packet of this track (with its leading sample count) and appends its
    /// samples, interleaved 16-bit.
    core::Status decode(std::span<const std::uint8_t> packet, std::vector<std::int16_t>& out,
                        std::size_t max_samples=static_cast<std::size_t>(-1));

    [[nodiscard]] std::uint8_t channels() const noexcept { return m_out_channels; }

private:
    static constexpr std::size_t kMaxChannels = 2;
    static constexpr std::size_t kMaxFrame = 4096;

    bool decode_block(Bits& gb, std::size_t channels);

    bool m_use_dct;
    bool m_version_2;
    std::size_t m_channels;  ///< coded channels (1 for RDFT, where they are interleaved)
    std::uint8_t m_out_channels;
    int m_frame_len = 0;
    int m_overlap_len = 0;
    int m_block_size = 0;
    int m_num_bands = 0;
    float m_root = 0;
    bool m_first = true;
    std::array<unsigned, 26> m_bands{};
    std::array<float, 96> m_quant_table{};
    std::array<std::array<float, kMaxFrame / 16>, kMaxChannels> m_previous{};
    std::array<std::vector<float>, kMaxChannels> m_out;
    BinkTransform m_transform;
};

}  // namespace o1602::video::bink
