// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bink Audio (see bink_audio.hpp): FFmpeg's binkaudio.c with its DCT-III and RDFT (dct.c, rdft.c),
// the complex FFT beneath them written out plainly.
// Copyright (c) 2007-2011 Peter Ross (pross@xvid.org)
// Copyright (c) 2009 Daniel Verkamp (daniel@drv.nu)
// Copyright (c) 2009 Alex Converse
#include "bink_audio.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <numbers>

#include "bink_bits.hpp"

namespace o1602::video::bink {

using core::ErrorCode;
using core::make_error;

namespace {

/// The Bark band edges in Hz (FFmpeg's ff_wma_critical_freqs).
constexpr std::array<std::uint16_t, 25> kCriticalFreqs = {
    100,  200,  300,  400,  510,  630,  770,  920,  1080, 1270,  1480,  1720,  2000,
    2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 24500,
};

constexpr std::array<std::uint8_t, 16> kRleLengths = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};

int frame_len_bits(std::uint32_t rate, std::uint8_t channels, bool use_dct) noexcept {
    int bits = rate < 22050 ? 9 : rate < 44100 ? 10 : 11;
    if (!use_dct) {
        // The RDFT variant codes its channels interleaved in one longer block.
        bits += std::bit_width(static_cast<unsigned>(std::max<std::uint8_t>(channels, 1))) - 1;
    }
    return bits;
}

float get_float(Bits& gb) noexcept {
    const int power = static_cast<int>(gb.bits(5));
    float f = std::ldexp(static_cast<float>(gb.bits(23)), power - 23);
    if (gb.bit() != 0) {
        f = -f;
    }
    return f;
}

std::int16_t to_s16(float v) noexcept {
    const long s = std::lrint(v * 32768.0f);
    return static_cast<std::int16_t>(std::clamp<long>(s, -32768, 32767));
}

}  // namespace

BinkTransform::BinkTransform(int nbits, bool dct)
    : m_n(1 << nbits),
      m_dct(dct),
      // The DCT runs an IDFT_C2R RDFT, the RDFT variant a DFT_C2R one.
      m_fft_inverse(dct),
      m_negative_sin(!dct),
      m_sign_convention(dct ? -1.0f : 1.0f) {
    const double pi = std::numbers::pi;
    m_tcos.resize(static_cast<std::size_t>(m_n / 4 + 1));
    m_tsin.resize(m_tcos.size());
    for (std::size_t i = 0; i < m_tcos.size(); ++i) {
        m_tcos[i] = static_cast<float>(std::cos(2 * pi * static_cast<double>(i) / m_n));
        m_tsin[i] = static_cast<float>(std::sin(2 * pi * static_cast<double>(i) / m_n));
    }
    if (dct) {
        m_dcos.resize(static_cast<std::size_t>(m_n + 1));
        for (std::size_t i = 0; i < m_dcos.size(); ++i) {
            m_dcos[i] = static_cast<float>(std::cos(pi * static_cast<double>(i) / (2.0 * m_n)));
        }
        m_csc2.resize(static_cast<std::size_t>(m_n / 2));
        for (std::size_t i = 0; i < m_csc2.size(); ++i) {
            m_csc2[i] = static_cast<float>(0.5 / std::sin(pi / (2.0 * m_n) * static_cast<double>(2 * i + 1)));
        }
    }
    const auto half = static_cast<std::size_t>(m_n / 2);  // the complex FFT's length
    m_fft_cos.resize(half / 2);
    m_fft_sin.resize(half / 2);
    for (std::size_t k = 0; k < half / 2; ++k) {
        const double a = 2 * pi * static_cast<double>(k) / static_cast<double>(half);
        m_fft_cos[k] = static_cast<float>(std::cos(a));
        m_fft_sin[k] = static_cast<float>((m_fft_inverse ? 1.0 : -1.0) * std::sin(a));
    }
    m_bitrev.resize(half);
    const int bits = std::bit_width(half) - 1;
    for (std::size_t i = 0; i < half; ++i) {
        std::uint32_t r = 0;
        for (int b = 0; b < bits; ++b) {
            r |= ((static_cast<std::uint32_t>(i) >> b) & 1U) << (bits - 1 - b);
        }
        m_bitrev[i] = r;
    }
}

void BinkTransform::fft(float* data) {
    // An unscaled complex DFT of n / 2 points in place: exp(-2 pi i jk / N) forward, exp(+...)
    // inverse, as FFmpeg's fft_permute and fft_calc together give it.
    const std::size_t n = m_bitrev.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = m_bitrev[i];
        if (j > i) {
            std::swap(data[2 * i], data[2 * j]);
            std::swap(data[2 * i + 1], data[2 * j + 1]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t step = n / len;
        for (std::size_t start = 0; start < n; start += len) {
            for (std::size_t k = 0; k < len / 2; ++k) {
                const float wr = m_fft_cos[k * step];
                const float wi = m_fft_sin[k * step];
                float* a = data + 2 * (start + k);
                float* b = data + 2 * (start + k + len / 2);
                const float tr = b[0] * wr - b[1] * wi;
                const float ti = b[0] * wi + b[1] * wr;
                b[0] = a[0] - tr;
                b[1] = a[1] - ti;
                a[0] += tr;
                a[1] += ti;
            }
        }
    }
}

void BinkTransform::rdft(float* data) {
    // rdft_calc_c of an inverse transform: unmangle, then the half-length complex FFT.
    const int n = m_n;
    constexpr float k1 = 0.5f;
    constexpr float k2 = -0.5f;

    const float ev_re0 = data[0];
    data[0] = ev_re0 + data[1];
    data[1] = ev_re0 - data[1];

    int i = 1;
    for (; i < (n >> 2); i++) {
        const int i1 = 2 * i;
        const int i2 = n - i1;
        const float ev_re = k1 * (data[i1] + data[i2]);
        const float od_im = k2 * (data[i2] - data[i1]);
        const float ev_im = k1 * (data[i1 + 1] - data[i2 + 1]);
        const float od_re = k2 * (data[i1 + 1] + data[i2 + 1]);
        const auto c = m_tcos[static_cast<std::size_t>(i)];
        const auto s = m_tsin[static_cast<std::size_t>(i)];
        float odsum_re = 0;
        float odsum_im = 0;
        if (m_negative_sin) {
            odsum_re = od_re * c + od_im * s;
            odsum_im = od_im * c - od_re * s;
        } else {
            odsum_re = od_re * c - od_im * s;
            odsum_im = od_im * c + od_re * s;
        }
        data[i1] = ev_re + odsum_re;
        data[i1 + 1] = ev_im + odsum_im;
        data[i2] = ev_re - odsum_re;
        data[i2 + 1] = odsum_im - ev_im;
    }
    data[2 * i + 1] = m_sign_convention * data[2 * i + 1];
    data[0] *= k1;
    data[1] *= k1;
    fft(data);
}

void BinkTransform::run(float* data) {
    if (!m_dct) {
        rdft(data);
        return;
    }
    // dct_calc_III_c
    const int n = m_n;
    const float next = data[n - 1];
    const float inv_n = 1.0f / static_cast<float>(n);

    for (int i = n - 2; i >= 2; i -= 2) {
        const float val1 = data[i];
        const float val2 = data[i - 1] - data[i + 1];
        const float c = m_dcos[static_cast<std::size_t>(i)];
        const float s = m_dcos[static_cast<std::size_t>(n - i)];
        data[i] = c * val1 + s * val2;
        data[i + 1] = s * val1 - c * val2;
    }
    data[1] = 2 * next;

    rdft(data);

    for (int i = 0; i < n / 2; i++) {
        float tmp1 = data[i] * inv_n;
        const float tmp2 = data[n - i - 1] * inv_n;
        const float csc = m_csc2[static_cast<std::size_t>(i)] * (tmp1 - tmp2);
        tmp1 += tmp2;
        data[i] = tmp1 + csc;
        data[n - i - 1] = tmp1 - csc;
    }
}

BinkAudio::BinkAudio(std::uint32_t rate, std::uint8_t channels, bool use_dct, char revision)
    : m_use_dct(use_dct),
      m_version_2(revision == 'n'),
      m_channels(use_dct ? channels : 1),
      m_out_channels(channels),
      m_transform(frame_len_bits(rate, channels, use_dct), use_dct) {
    const int bits = frame_len_bits(rate, channels, use_dct);
    // The RDFT variant: the channels interleaved count as one at their joint rate.
    const std::uint64_t coded_rate = use_dct ? rate : std::uint64_t{rate} * channels;
    m_frame_len = 1 << bits;
    m_overlap_len = m_frame_len / 16;
    m_block_size = (m_frame_len - m_overlap_len) * static_cast<int>(std::min(kMaxChannels, m_channels));
    // Bink::open refuses a rate of 0; at least 1 keeps the band edges defined anyway.
    const auto rate_half = std::max(static_cast<unsigned>((coded_rate + 1) / 2), 1U);
    if (!use_dct) {
        m_root = static_cast<float>(2.0 / (std::sqrt(m_frame_len) * 32768.0));
    } else {
        m_root = static_cast<float>(m_frame_len / (std::sqrt(m_frame_len) * 32768.0));
    }
    for (std::size_t i = 0; i < m_quant_table.size(); i++) {
        // the constant is 0.066399999 / log10(e)
        m_quant_table[i] = std::exp(static_cast<float>(i) * 0.15289164787221953823f) * m_root;
    }
    for (m_num_bands = 1; m_num_bands < 25; m_num_bands++) {
        if (rate_half <= kCriticalFreqs[static_cast<std::size_t>(m_num_bands - 1)]) {
            break;
        }
    }
    m_bands[0] = 2;
    for (std::size_t i = 1; i < static_cast<std::size_t>(m_num_bands); i++) {
        m_bands[i] = (kCriticalFreqs[i - 1] * static_cast<unsigned>(m_frame_len) / rate_half) & ~1U;
    }
    m_bands[static_cast<std::size_t>(m_num_bands)] = static_cast<unsigned>(m_frame_len);
    for (auto& o : m_out) {
        // Two floats more: the RDFT packs its highest coefficient there in other implementations.
        o.assign(static_cast<std::size_t>(m_frame_len) + 2, 0.0f);
    }
}

bool BinkAudio::decode_block(Bits& gb, std::size_t channels) {
    std::array<float, 25> quant{};
    const int quant_idx_size = m_version_2 ? 7 : 8;

    if (m_use_dct) {
        gb.skip(2);
    }

    for (std::size_t ch = 0; ch < channels; ch++) {
        float* coeffs = m_out[ch].data();
        if (gb.left() < 58) {
            return false;
        }
        coeffs[0] = get_float(gb) * m_root;
        coeffs[1] = get_float(gb) * m_root;

        if (gb.left() < static_cast<std::int64_t>(m_num_bands) * quant_idx_size) {
            return false;
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(m_num_bands); i++) {
            const auto value = static_cast<std::size_t>(gb.bits(quant_idx_size));
            quant[i] = m_quant_table[std::min<std::size_t>(value, 95)];
        }

        std::size_t k = 0;
        float q = quant[0];

        int i = 2;
        while (i < m_frame_len) {
            int j = 0;
            if (gb.bit() != 0) {
                j = i + kRleLengths[gb.bits(4)] * 8;
            } else {
                j = i + 8;
            }
            j = std::min(j, m_frame_len);

            const int width = static_cast<int>(gb.bits(4));
            if (width == 0) {
                std::fill(coeffs + i, coeffs + j, 0.0f);
                i = j;
                while (m_bands[k] < static_cast<unsigned>(i)) {
                    q = quant[k++];
                }
            } else if (m_version_2) {
                for (int m = i; m < j; m++) {
                    coeffs[m] = static_cast<float>(gb.bits(width));
                }
                while (i < j) {
                    if (m_bands[k] == static_cast<unsigned>(i)) {
                        q = quant[k++];
                    }
                    if (coeffs[i] > 0) {
                        coeffs[i] *= gb.bit() != 0 ? -q : q;
                    }
                    i++;
                }
            } else {
                while (i < j) {
                    if (m_bands[k] == static_cast<unsigned>(i)) {
                        q = quant[k++];
                    }
                    const auto coeff = static_cast<float>(gb.bits(width));
                    if (coeff != 0) {
                        coeffs[i] = gb.bit() != 0 ? -q * coeff : q * coeff;
                    } else {
                        coeffs[i] = 0.0f;
                    }
                    i++;
                }
            }
            if (k > static_cast<std::size_t>(m_num_bands)) {
                return false;
            }
        }

        if (m_use_dct) {
            coeffs[0] /= 0.5f;
        }
        m_transform.run(coeffs);
    }

    for (std::size_t ch = 0; ch < channels; ch++) {
        const int count = m_overlap_len * static_cast<int>(channels);
        float* out = m_out[ch].data();
        if (!m_first) {
            int j = static_cast<int>(ch);
            for (int i = 0; i < m_overlap_len; i++, j += static_cast<int>(channels)) {
                out[i] = (m_previous[ch][static_cast<std::size_t>(i)] * static_cast<float>(count - j) +
                          out[i] * static_cast<float>(j)) /
                         static_cast<float>(count);
            }
        }
        std::memcpy(m_previous[ch].data(), out + (m_frame_len - m_overlap_len),
                    static_cast<std::size_t>(m_overlap_len) * sizeof(float));
    }
    m_first = false;
    return true;
}

core::Status BinkAudio::decode(std::span<const std::uint8_t> packet, std::vector<std::int16_t>& out, std::size_t max_samples) {
    if (packet.size() < 4) {
        return make_error(ErrorCode::InvalidFormat, "Bink audio: packet too small");
    }
    if (m_channels < 1 || m_channels > kMaxChannels || m_out_channels > kMaxChannels ||
        static_cast<std::size_t>(m_frame_len) > kMaxFrame) {
        return make_error(ErrorCode::InvalidFormat, "Bink audio: unsupported track");
    }
    Bits gb(packet);
    gb.skip(32);  // the size of the decoded samples
    while (gb.left() > 0) {
        if(out.size()>max_samples || static_cast<std::size_t>(m_block_size)>max_samples-out.size())
            return make_error(ErrorCode::InvalidFormat,"Bink audio: sample buffer budget exceeded");
        if (!decode_block(gb, m_channels)) {
            return make_error(ErrorCode::InvalidFormat, "Bink audio: incomplete packet");
        }
        gb.align32();
        // A block's samples: the planar channels of the DCT, the interleaved ones of the RDFT.
        if (m_use_dct) {
            const auto frames = static_cast<std::size_t>(m_frame_len - m_overlap_len);
            for (std::size_t f = 0; f < frames; ++f) {
                for (std::size_t ch = 0; ch < m_channels; ++ch) {
                    out.push_back(to_s16(m_out[ch][f]));
                }
            }
        } else {
            for (std::size_t f = 0; f < static_cast<std::size_t>(m_block_size); ++f) {
                out.push_back(to_s16(m_out[0][f]));
            }
        }
    }
    return core::Ok{};
}

}  // namespace o1602::video::bink
