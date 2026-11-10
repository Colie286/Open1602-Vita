// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bink 2 video, revisions "g" on: integer transforms (Librempeg's libavcodec/bink2g.h, see
// bink2_video.hpp). The tables and most names are the reference's, so the two can be compared
// line by line.
// Copyright (c) 2014 Konstantin Shishkov
// Copyright (c) 2019 Paul B Mahol
#include <algorithm>
#include <cstring>

#include "bink2_common.hpp"
#include "bink2_video.hpp"
#include "bink_bits.hpp"
#include "bink_idct_neon.hpp"

namespace o1602::video::bink {

namespace {

constexpr std::uint8_t bink2g_scan[64] = {
    0,  8,  1,  2,  9,  16, 24, 17, 10, 3,  4,  11, 18, 25, 32, 40, 33, 26, 19, 12, 5,  6,
    13, 20, 27, 34, 41, 48, 56, 49, 42, 35, 28, 21, 14, 7,  15, 22, 29, 36, 43, 50, 57, 58,
    51, 44, 37, 30, 23, 31, 38, 45, 52, 59, 60, 53, 46, 39, 47, 54, 61, 62, 55, 63,
};

constexpr std::uint16_t bink2g_ac_skip_codes[2][kNumAcSkips] = {
    {
        0x01,
        0x00,
        0x004,
        0x02C,
        0x06C,
        0x0C,
        0x4C,
        0xAC,
        0xEC,
        0x12C,
        0x16C,
        0x1AC,
        0x02,
        0x1C,
    },
    {
        0x01,
        0x04,
        0x00,
        0x08,
        0x02,
        0x32,
        0x0A,
        0x12,
        0x3A,
        0x7A,
        0xFA,
        0x72,
        0x06,
        0x1A,
    },
};

constexpr std::uint8_t bink2g_ac_skip_bits[2][kNumAcSkips] = {
    {1, 3, 4, 9, 9, 7, 7, 9, 8, 9, 9, 9, 2, 5},
    {1, 3, 4, 4, 5, 7, 5, 6, 7, 8, 8, 7, 3, 6},
};

constexpr std::uint8_t bink2g_mv_codes[] = {
    0x01, 0x06, 0x0C, 0x1C, 0x18, 0x38, 0x58, 0x78, 0x68, 0x48, 0x28, 0x08, 0x14, 0x04, 0x02, 0x00,
};

constexpr std::uint8_t bink2g_mv_bits[] = {
    1, 3, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 3, 4,
};

constexpr std::uint8_t bink2g_skips[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 64, 0, 0, 0,
};

constexpr std::uint8_t bink2g_chroma_cbp_pat[16] = {
    0x00, 0x00, 0x00, 0x0F, 0x00, 0x0F, 0x0F, 0x0F, 0x00, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
};

constexpr std::int32_t bink2g_dc_pat[] = {
    1024,  1218,   1448,   1722,   2048,   2435,   2896,   3444,   4096,   4871,   5793,   6889,  8192,
    9742,  11585,  13777,  16384,  19484,  23170,  27555,  32768,  38968,  46341,  55109,  65536, 77936,
    92682, 110218, 131072, 155872, 185364, 220436, 262144, 311744, 370728, 440872, 524288,
};

constexpr std::uint16_t bink2g_luma_intra_qmat[4][64] = {
    {
        1024, 1432, 1506, 1181, 1843,  2025,  5271,  8592,  1313, 1669, 1630, 1672, 2625, 3442, 8023,  12794,
        1076, 1755, 1808, 1950, 3980,  4875,  8813,  11909, 1350, 1868, 2127, 2016, 4725, 4450, 7712,  9637,
        2458, 3103, 4303, 4303, 6963,  6835,  11079, 13365, 3375, 5704, 5052, 6049, 9198, 7232, 10725, 9834,
        5486, 7521, 7797, 7091, 11079, 10016, 13559, 12912, 7279, 7649, 7020, 6097, 9189, 9047, 12661, 13768,
    },
    {
        1218, 1703, 1791, 1405, 2192,  2408,  6268,  10218, 1561, 1985, 1938, 1988, 3122,  4093,  9541,  15215,
        1279, 2087, 2150, 2319, 4733,  5798,  10481, 14162, 1606, 2222, 2530, 2398, 5619,  5292,  9171,  11460,
        2923, 3690, 5117, 5118, 8281,  8128,  13176, 15894, 4014, 6783, 6008, 7194, 10938, 8600,  12755, 11694,
        6524, 8944, 9272, 8433, 13176, 11911, 16125, 15354, 8657, 9096, 8348, 7250, 10927, 10759, 15056, 16373,
    },
    {
        1448, 2025,  2130,  1671,  2607,  2864,  7454,  12151, 1856,  2360,  2305, 2364, 3713,  4867,  11346, 18094,
        1521, 2482,  2557,  2758,  5628,  6894,  12464, 16841, 1909,  2642,  3008, 2852, 6683,  6293,  10906, 13629,
        3476, 4388,  6085,  6086,  9847,  9666,  15668, 18901, 4773,  8066,  7145, 8555, 13007, 10227, 15168, 13907,
        7758, 10637, 11026, 10028, 15668, 14165, 19175, 18259, 10294, 10817, 9927, 8622, 12995, 12794, 17905, 19470,
    },
    {
        1722, 2408,  2533,  1987,  3100,  3406,  8864,  14450, 2208,  2807,  2741,  2811,  4415,  5788,  13493, 21517,
        1809, 2951,  3041,  3280,  6693,  8199,  14822, 20028, 2271,  3142,  3578,  3391,  7947,  7484,  12969, 16207,
        4133, 5218,  7236,  7238,  11711, 11495, 18633, 22478, 5677,  9592,  8497,  10174, 15469, 12162, 18038, 16538,
        9226, 12649, 13112, 11926, 18633, 16845, 22804, 21715, 12242, 12864, 11806, 10254, 15454, 15215, 21293, 23155,
    },
};

constexpr std::uint16_t bink2g_chroma_intra_qmat[4][64] = {
    {
        1024, 1193, 1434, 2203, 5632, 4641, 5916, 6563, 1193, 1622, 1811, 3606, 6563, 5408, 6894, 7649,
        1434, 1811, 3515, 4875, 5916, 4875, 6215, 6894, 2203, 3606, 4875, 3824, 4641, 3824, 4875, 5408,
        5632, 6563, 5916, 4641, 5632, 4641, 5916, 6563, 4641, 5408, 4875, 3824, 4641, 3824, 4875, 5408,
        5916, 6894, 6215, 4875, 5916, 4875, 6215, 6894, 6563, 7649, 6894, 5408, 6563, 5408, 6894, 7649,
    },
    {
        1218, 1419, 1706, 2620, 6698, 5519, 7035, 7805, 1419, 1929, 2153, 4288, 7805, 6432, 8199, 9096,
        1706, 2153, 4180, 5798, 7035, 5798, 7390, 8199, 2620, 4288, 5798, 4548, 5519, 4548, 5798, 6432,
        6698, 7805, 7035, 5519, 6698, 5519, 7035, 7805, 5519, 6432, 5798, 4548, 5519, 4548, 5798, 6432,
        7035, 8199, 7390, 5798, 7035, 5798, 7390, 8199, 7805, 9096, 8199, 6432, 7805, 6432, 8199, 9096,
    },
    {
        1448, 1688, 2028, 3116, 7965, 6563, 8367, 9282, 1688, 2294,  2561, 5099, 9282, 7649, 9750, 10817,
        2028, 2561, 4971, 6894, 8367, 6894, 8789, 9750, 3116, 5099,  6894, 5408, 6563, 5408, 6894, 7649,
        7965, 9282, 8367, 6563, 7965, 6563, 8367, 9282, 6563, 7649,  6894, 5408, 6563, 5408, 6894, 7649,
        8367, 9750, 8789, 6894, 8367, 6894, 8789, 9750, 9282, 10817, 9750, 7649, 9282, 7649, 9750, 10817,
    },
    {
        1722, 2007,  2412,  3706, 9472, 7805, 9950,  11038, 2007,  2729,  3045,  6064, 11038, 9096, 11595, 12864,
        2412, 3045,  5912,  8199, 9950, 8199, 10452, 11595, 3706,  6064,  8199,  6432, 7805,  6432, 8199,  9096,
        9472, 11038, 9950,  7805, 9472, 7805, 9950,  11038, 7805,  9096,  8199,  6432, 7805,  6432, 8199,  9096,
        9950, 11595, 10452, 8199, 9950, 8199, 10452, 11595, 11038, 12864, 11595, 9096, 11038, 9096, 11595, 12864,
    },
};

constexpr std::uint16_t bink2g_inter_qmat[4][64] = {
    {
        1024, 1193, 1076, 844,  1052, 914,  1225, 1492, 1193, 1391, 1254, 983,  1227, 1065, 1463, 1816,
        1076, 1254, 1161, 936,  1195, 1034, 1444, 1741, 844,  983,  936,  811,  1055, 927,  1305, 1584,
        1052, 1227, 1195, 1055, 1451, 1336, 1912, 2354, 914,  1065, 1034, 927,  1336, 1313, 1945, 2486,
        1225, 1463, 1444, 1305, 1912, 1945, 3044, 4039, 1492, 1816, 1741, 1584, 2354, 2486, 4039, 5679,
    },
    {
        1218, 1419, 1279, 1003, 1252, 1087, 1457, 1774, 1419, 1654, 1491, 1169, 1459, 1267, 1739, 2159,
        1279, 1491, 1381, 1113, 1421, 1230, 1717, 2070, 1003, 1169, 1113, 965,  1254, 1103, 1552, 1884,
        1252, 1459, 1421, 1254, 1725, 1589, 2274, 2799, 1087, 1267, 1230, 1103, 1589, 1562, 2313, 2956,
        1457, 1739, 1717, 1552, 2274, 2313, 3620, 4803, 1774, 2159, 2070, 1884, 2799, 2956, 4803, 6753,
    },
    {
        1448, 1688, 1521, 1193, 1488, 1293, 1732, 2110, 1688, 1967, 1773, 1391, 1735, 1507, 2068, 2568,
        1521, 1773, 1642, 1323, 1690, 1462, 2042, 2462, 1193, 1391, 1323, 1147, 1492, 1311, 1845, 2241,
        1488, 1735, 1690, 1492, 2052, 1889, 2704, 3328, 1293, 1507, 1462, 1311, 1889, 1857, 2751, 3515,
        1732, 2068, 2042, 1845, 2704, 2751, 4306, 5712, 2110, 2568, 2462, 2241, 3328, 3515, 5712, 8031,
    },
    {
        1722, 2007, 1809, 1419, 1770, 1537, 2060, 2509, 2007, 2339, 2108, 1654, 2063, 1792, 2460, 3054,
        1809, 2108, 1953, 1574, 2010, 1739, 2428, 2928, 1419, 1654, 1574, 1364, 1774, 1559, 2195, 2664,
        1770, 2063, 2010, 1774, 2440, 2247, 3216, 3958, 1537, 1792, 1739, 1559, 2247, 2209, 3271, 4181,
        2060, 2460, 2428, 2195, 3216, 3271, 5120, 6793, 2509, 3054, 2928, 2664, 3958, 4181, 6793, 9550,
    },
};

constexpr std::uint16_t bink2g_mask[] = {0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff};

const Vlc kAcSkipVlc0(bink2g_ac_skip_codes[0], bink2g_ac_skip_bits[0]);
const Vlc kAcSkipVlc1(bink2g_ac_skip_codes[1], bink2g_ac_skip_bits[1]);
const Vlc kMvVlc(bink2g_mv_codes, bink2g_mv_bits);

inline int idct_mul_a(int val) noexcept {
    return val + (val >> 2);
}
inline int idct_mul_b(int val) noexcept {
    return val >> 1;
}
inline int idct_mul_c(int val) noexcept {
    return val - (val >> 2) - (val >> 4);
}
inline int idct_mul_d(int val) noexcept {
    return val + (val >> 2) - (val >> 4);
}
inline int idct_mul_e(int val) noexcept {
    return val >> 2;
}

void idct_1d(std::int16_t* blk, int step, int shift) noexcept {
    const int tmp00 = blk[3 * step] + blk[5 * step];
    const int tmp01 = blk[3 * step] - blk[5 * step];
    const int tmp02 = idct_mul_a(blk[2 * step]) + idct_mul_b(blk[6 * step]);
    const int tmp03 = idct_mul_b(blk[2 * step]) - idct_mul_a(blk[6 * step]);
    const int tmp0 = (blk[0 * step] + blk[4 * step]) + tmp02;
    const int tmp1 = (blk[0 * step] + blk[4 * step]) - tmp02;
    const int tmp2 = blk[0 * step] - blk[4 * step];
    const int tmp3 = blk[1 * step] + tmp00;
    const int tmp4 = blk[1 * step] - tmp00;
    const int tmp5 = tmp01 + blk[7 * step];
    const int tmp6 = tmp01 - blk[7 * step];
    const int tmp7 = tmp4 + idct_mul_c(tmp6);
    const int tmp8 = idct_mul_c(tmp4) - tmp6;
    const int tmp9 = idct_mul_d(tmp3) + idct_mul_e(tmp5);
    const int tmp10 = idct_mul_e(tmp3) - idct_mul_d(tmp5);
    const int tmp11 = tmp2 + tmp03;
    const int tmp12 = tmp2 - tmp03;

    blk[0 * step] = static_cast<std::int16_t>((tmp0 + tmp9) >> shift);
    blk[1 * step] = static_cast<std::int16_t>((tmp11 + tmp7) >> shift);
    blk[2 * step] = static_cast<std::int16_t>((tmp12 + tmp8) >> shift);
    blk[3 * step] = static_cast<std::int16_t>((tmp1 + tmp10) >> shift);
    blk[4 * step] = static_cast<std::int16_t>((tmp1 - tmp10) >> shift);
    blk[5 * step] = static_cast<std::int16_t>((tmp12 - tmp8) >> shift);
    blk[6 * step] = static_cast<std::int16_t>((tmp11 - tmp7) >> shift);
    blk[7 * step] = static_cast<std::int16_t>((tmp0 - tmp9) >> shift);
}

void idct_scalar(std::int16_t* block) noexcept {
    for (int i = 0; i < 8; i++) {
        idct_1d(block + i, 8, 0);
    }
    for (int i = 0; i < 8; i++) {
        idct_1d(block + i * 8, 1, 6);
    }
}

thread_local bool use_simd=true;
bool simd_verified() noexcept {
#if defined(__ARM_NEON)
    static const bool passed=neon::verify(idct_scalar);return passed;
#else
    return false;
#endif
}
void idct(std::int16_t* block) noexcept {
#if defined(__ARM_NEON)
    if(use_simd&&simd_verified()){neon::idct(block);return;}
#endif
    idct_scalar(block);
}

void idct_put(std::uint8_t* dst, int stride, const std::int16_t* block) noexcept {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            dst[j] = clip_uint8(block[i * 8 + j]);
        }
        dst += stride;
    }
}

void add_residual(const std::uint8_t* dst, int stride, std::int16_t* block) noexcept {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            block[i * 8 + j] = static_cast<std::int16_t>(block[i * 8 + j] + dst[j]);
        }
        dst += stride;
    }
}

int get_type(Bits& gb, int* lru) noexcept {
    int val = 0;
    switch (gb.unary(1, 3)) {
        case 0:
            val = lru[0];
            break;
        case 1:
            val = lru[1];
            std::swap(lru[0], lru[1]);
            break;
        case 2:
            val = lru[3];
            std::swap(lru[2], lru[3]);
            break;
        case 3:
        default:
            val = lru[2];
            std::swap(lru[1], lru[2]);
            break;
    }
    return val;
}

int decode_dq(Bits& gb) noexcept {
    int dq = gb.unary(1, 4);
    if (dq == 3) {
        dq += static_cast<int>(gb.bit());
    } else if (dq == 4) {
        dq += static_cast<int>(gb.bits(5)) + 1;
    }
    if (dq != 0 && gb.bit() != 0) {
        dq = -dq;
    }
    return dq;
}

unsigned decode_cbp_chroma(Bits& gb, unsigned prev_cbp) noexcept {
    unsigned cbp = (prev_cbp & 0xF0000U) | bink2g_chroma_cbp_pat[prev_cbp & 0xFU];
    if (gb.bit() == 0) {
        cbp = gb.bits(4);
        if (gb.bit() != 0) {
            cbp |= cbp << 16;
        }
    }
    return cbp;
}

int dc_mpred(int a, int b, int c) noexcept {
    return clip(c + b - a, std::min({a, b, c}), std::max({a, b, c}));
}
int dc_mpred2(int a, int b) noexcept {
    return clip(2 * a - b, std::min(a, b), std::max(a, b));
}

/// The AC coefficients of four 8x8 blocks (bit i of `cbp`: block i has some).
int decode_ac(Bits& gb, const std::uint8_t (&scan)[64], std::array<std::array<std::int16_t, 64>, 4>& block,
              unsigned cbp, int q, const std::uint16_t (&qmat)[4][64], std::uint16_t* dering) noexcept {
    const std::uint16_t* qqmat = qmat[q & 3];
    const int factor = 1 << (q >> 2);

    for (auto& b : block) {
        b.fill(0);
    }

    if ((cbp & 0xfU) == 0) {
        if (dering != nullptr) {
            for (int i = 0; i < 4; i++) {
                dering[i] |= 0x6000;
            }
        }
        return 0;
    }

    const Vlc& skip_vlc = (cbp & 0xffff0000U) != 0 ? kAcSkipVlc1 : kAcSkipVlc0;

    for (int i = 0; i < 4; i++, cbp >>= 1) {
        if ((cbp & 1U) == 0) {
            if (dering != nullptr) {
                dering[i] |= 0x6000;
            }
            continue;
        }

        int next = 0;
        int idx = 1;
        int nb_ac = 0;
        while (idx < 64) {
            next--;
            if (next < 1) {
                int skip = skip_vlc.read(gb);
                if (skip < 0) {
                    return -1;
                }
                next = kNextSkips[static_cast<std::size_t>(skip)];
                skip = bink2g_skips[skip];
                if (skip == 11) {
                    skip = static_cast<int>(gb.bits(6));
                }
                idx += skip;
                if (idx >= 64) {
                    break;
                }
            }

            int val = gb.unary(0, 12) + 1;
            if (val > 3) {
                val = static_cast<int>(gb.bits(val - 3)) + (1 << (val - 3)) + 2;
            }
            if (gb.bit() != 0) {
                val = -val;
            }
            block[static_cast<std::size_t>(i)][scan[idx]] =
                static_cast<std::int16_t>(((val * qqmat[scan[idx]] * factor) + 64) >> 7);
            nb_ac++;
            idx++;
        }

        if (dering != nullptr) {
            dering[i] = static_cast<std::uint16_t>(dering[i] | (((nb_ac < 8) + (nb_ac < 4) + (nb_ac < 1)) << 13));
        }
    }
    return 0;
}

bool dc_pair_needs_smoothing(std::uint16_t dc_a, std::uint16_t dc_b) noexcept {
    if ((dc_a | dc_b) < 0x2000) {
        return false;
    }
    if ((dc_a & dc_b) < 0x6000) {
        return true;
    }
    return static_cast<int>(dc_a ^ 0x1000U) - static_cast<int>(dc_b ^ 0x1000U) > 0x20;
}

void dering_w(std::int16_t* a, std::int16_t* b, std::uint16_t dc_a, std::uint16_t dc_b) noexcept {
    if (!dc_pair_needs_smoothing(dc_a, dc_b)) {
        return;
    }
    const std::uint16_t* mask_a = &bink2g_mask[dc_a >> 13];
    const std::uint16_t* mask_b = &bink2g_mask[dc_b >> 13];
    for (int i = 0; i < 8; i++) {
        const auto diff = static_cast<std::int16_t>(b[i] - a[56 + i]);
        const auto diff_4 = static_cast<std::int16_t>((diff + 4) >> 3);
        const auto diff_2 = static_cast<std::int16_t>((diff + 2) >> 2);
        a[48 + i] = static_cast<std::int16_t>(a[48 + i] + (diff_4 & static_cast<std::int16_t>(mask_a[0])));
        a[56 + i] = static_cast<std::int16_t>(a[56 + i] + (diff_2 & static_cast<std::int16_t>(mask_a[1])));
        b[0 + i] = static_cast<std::int16_t>(b[0 + i] - (diff_2 & static_cast<std::int16_t>(mask_b[1])));
        b[8 + i] = static_cast<std::int16_t>(b[8 + i] - (diff_4 & static_cast<std::int16_t>(mask_b[0])));
    }
}

void dering_h(std::int16_t* a, std::int16_t* b, std::uint16_t dc_a, std::uint16_t dc_b) noexcept {
    if (!dc_pair_needs_smoothing(dc_a, dc_b)) {
        return;
    }
    const std::uint16_t* mask_a = &bink2g_mask[dc_a >> 13];
    const std::uint16_t* mask_b = &bink2g_mask[dc_b >> 13];
    for (int i = 0; i < 8; i++) {
        const auto diff = static_cast<std::int16_t>(b[0 + i * 8] - a[7 + i * 8]);
        const auto diff_4 = static_cast<std::int16_t>((diff + 4) >> 3);
        const auto diff_2 = static_cast<std::int16_t>((diff + 2) >> 2);
        a[6 + i * 8] = static_cast<std::int16_t>(a[6 + i * 8] + (diff_4 & static_cast<std::int16_t>(mask_a[0])));
        a[7 + i * 8] = static_cast<std::int16_t>(a[7 + i * 8] + (diff_2 & static_cast<std::int16_t>(mask_a[1])));
        b[0 + i * 8] = static_cast<std::int16_t>(b[0 + i * 8] - (diff_2 & static_cast<std::int16_t>(mask_b[1])));
        b[1 + i * 8] = static_cast<std::int16_t>(b[1 + i * 8] - (diff_4 & static_cast<std::int16_t>(mask_b[0])));
    }
}

void transpose_block(std::int16_t* block) noexcept {
    std::int16_t t[64];
    std::memcpy(t, block, sizeof(t));
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            block[y * 8 + x] = t[x * 8 + y];
        }
    }
}

int ch1_filter(const std::uint8_t* s, int shift) noexcept {
    return (3 * s[0] + s[1] + (1 << shift) / 2) >> shift;
}
int ch2_filter(const std::uint8_t* s, int shift) noexcept {
    return (2 * s[0] + 2 * s[1] + (1 << shift) / 2) >> shift;
}
int ch3_filter(const std::uint8_t* s, int shift) noexcept {
    return (s[0] + 3 * s[1] + (1 << shift) / 2) >> shift;
}
template <typename T>
int cv1_filter(const T* s, int i, int shift) noexcept {
    return (3 * s[0] + s[i] + (1 << shift) / 2) >> shift;
}
template <typename T>
int cv2_filter(const T* s, int i, int shift) noexcept {
    return (2 * s[0] + 2 * s[i] + (1 << shift) / 2) >> shift;
}
template <typename T>
int cv3_filter(const T* s, int i, int shift) noexcept {
    return (s[0] + 3 * s[i] + (1 << shift) / 2) >> shift;
}

/// One 8x8 chroma block from the frame before, at quarter-pel `mode` (bits 0..1 across, 2..3 down).
void c_mc(std::uint8_t* dst, int stride, const std::uint8_t* src, int sstride, int width, int height, int mv_x,
          int mv_y, int mode) noexcept {
    // Interior motion vectors can read the immutable reference plane directly.
    // Keep the original edge extension for vectors outside the picture.
    if(mode==0&&mv_x>=0&&mv_y>=0&&mv_x<=width-8&&mv_y<=height-8){
        copy_block(dst,src+mv_y*sstride+mv_x,stride,sstride,8,8);return;
    }
    std::uint8_t mtemp[9 * 9];
    std::uint16_t temp[8 * 9];
    const std::uint8_t* msrc;
    int source_stride;
    if(mv_x>=0&&mv_y>=0&&mv_x<=width-9&&mv_y<=height-9){
        msrc=src+mv_y*sstride+mv_x;source_stride=sstride;
    }else{
        for(int y=0;y<9;++y)for(int x=0;x<9;++x)
            mtemp[y*9+x]=src[clip(mv_x+x,0,width-1)+clip(mv_y+y,0,height-1)*sstride];
        msrc=mtemp;source_stride=9;
    }
    const int h = mode & 3;  // 0: whole pel, 1..3: the filters across
    const int v = mode >> 2;
    if (mode == 0) {
        copy_block(dst, msrc, stride, source_stride, 8, 8);
        return;
    }
    const auto across = [h](const std::uint8_t* s, int shift) {
        return h == 1 ? ch1_filter(s, shift) : h == 2 ? ch2_filter(s, shift) : ch3_filter(s, shift);
    };
    if (v == 0) {
        for (int j = 0; j < 8; j++) {
            for (int i = 0; i < 8; i++) {
                dst[i] = clip_uint8(across(msrc + i, 2));
            }
            dst += stride;
            msrc += source_stride;
        }
        return;
    }
    const auto down = [v](const auto* s, int i, int shift) {
        return v == 1 ? cv1_filter(s, i, shift) : v == 2 ? cv2_filter(s, i, shift) : cv3_filter(s, i, shift);
    };
    if (h == 0) {
        for (int j = 0; j < 8; j++) {
            for (int i = 0; i < 8; i++) {
                dst[i * stride] = clip_uint8(down(msrc + i * source_stride, source_stride, 2));
            }
            dst += 1;
            msrc += 1;
        }
        return;
    }
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 8; j++) {
            temp[i * 8 + j] = static_cast<std::uint16_t>(across(msrc + j, 0));
        }
        msrc += source_stride;
    }
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 8; i++) {
            dst[i] = clip_uint8(down(temp + j * 8 + i, 8, 4));
        }
        dst += stride;
    }
}

/// One 16x16 luma block from the frame before, at half-pel `mode` (bit 0 across, bit 1 down).
void y_mc(std::uint8_t* dst, int stride, const std::uint8_t* src, int sstride, int width, int height, int mv_x,
          int mv_y, int mode) noexcept {
    if(mode==0&&mv_x>=0&&mv_y>=0&&mv_x<=width-16&&mv_y<=height-16){
        copy_block(dst,src+mv_y*sstride+mv_x,stride,sstride,16,16);return;
    }
    std::uint8_t mtemp[21 * 21];
    const std::uint8_t* msrc;
    int source_stride;
    if(mv_x>=2&&mv_y>=2&&mv_x<=width-19&&mv_y<=height-19){
        msrc=src+mv_y*sstride+mv_x;source_stride=sstride;
    }else{
        for(int y=0;y<21;++y)for(int x=0;x<21;++x)
            mtemp[y*21+x]=src[clip(mv_x+x-2,0,width-1)+clip(mv_y+y-2,0,height-1)*sstride];
        msrc=mtemp+21*2+2;source_stride=21;
    }

    if (mode == 0) {
        copy_block(dst, msrc, stride, source_stride, 16, 16);
    } else if (mode == 1) {
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                dst[i] = clip_uint8(lh_filter(msrc + i));
            }
            dst += stride;
            msrc += source_stride;
        }
    } else if (mode == 2) {
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                dst[i * stride] = clip_uint8(lv_filter(msrc + i * source_stride, source_stride));
            }
            dst += 1;
            msrc += 1;
        }
    } else if (mode == 3) {
        std::int16_t temp[21 * 16];
        msrc -= 2 * source_stride;
        for (int i = 0; i < 21; i++) {
            for (int j = 0; j < 16; j++) {
                temp[i * 16 + j] = static_cast<std::int16_t>(lh_filter(msrc + j));
            }
            msrc += source_stride;
        }
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                dst[i] = clip_uint8(lv_filter(temp + (j + 2) * 16 + i, 16));
            }
            dst += stride;
        }
    }
}

int average_block(const std::uint8_t* src, int stride) noexcept {
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        const int avg_a = (src[i + 0 * stride] + src[i + 1 * stride] + 1) >> 1;
        const int avg_b = (src[i + 2 * stride] + src[i + 3 * stride] + 1) >> 1;
        const int avg_c = (src[i + 4 * stride] + src[i + 5 * stride] + 1) >> 1;
        const int avg_d = (src[i + 6 * stride] + src[i + 7 * stride] + 1) >> 1;
        const int avg_e = (avg_a + avg_b + 1) >> 1;
        const int avg_f = (avg_c + avg_d + 1) >> 1;
        const int avg_g = (avg_e + avg_f + 1) >> 1;
        sum += avg_g;
    }
    return sum;
}

void average_chroma(int x, const std::uint8_t* src, int stride, std::array<int, 16>& dc) noexcept {
    for (int i = 0; i < 4; i++) {
        const int bx = i & 1;
        const int by = i >> 1;
        dc[static_cast<std::size_t>(i)] = average_block(src + x + bx * 8 + (by * 8) * stride, stride);
    }
}

void average_luma(int x, const std::uint8_t* src, int stride, std::array<int, 16>& dc) noexcept {
    for (int i = 0; i < 16; i++) {
        const int at = kLumaRepos[static_cast<std::size_t>(i)];
        const int bx = at & 3;
        const int by = at >> 2;
        dc[static_cast<std::size_t>(i)] = average_block(src + x + bx * 8 + (by * 8) * stride, stride);
    }
}

}  // namespace

unsigned Bink2Video::g_decode_cbp_luma(unsigned prev_cbp) {
    Bits& gb = *m_gb;
    unsigned ones = 0;
    for (int i = 0; i < 16; i++) {
        if ((prev_cbp & (1U << i)) != 0) {
            ones += 1;
        }
    }

    unsigned cbp = 0;
    unsigned mask = 0;
    if (ones > 7) {
        ones = 16 - ones;
        mask = 0xFFFF;
    }

    if (gb.bit() == 0) {
        if (ones < 4) {
            for (int j = 0; j < 16; j += 4) {
                if (gb.bit() == 0) {
                    cbp |= gb.bits(4) << j;
                }
            }
        } else {
            cbp = gb.bits(16);
        }
    }

    cbp ^= mask;
    if ((m_frame_flags & 0x40000) == 0 || cbp != 0) {
        if (gb.bit() != 0) {
            cbp |= cbp << 16;
        }
    }
    return cbp;
}

void Bink2Video::g_predict_dc(bool is_luma, int mindc, int maxdc, int flags, const std::array<int, 16>& tdc) {
    const auto left = static_cast<std::size_t>(std::max(m_mb_pos - 1, 0));
    const auto here = static_cast<std::size_t>(m_mb_pos);
    const auto comp = static_cast<std::size_t>(m_comp);
    const std::array<int, 16>& LTdc = m_prev_idc[left].dc[comp];
    const std::array<int, 16>& Tdc = m_prev_idc[here].dc[comp];
    const std::array<int, 16>& Ldc = m_current_idc[left].dc[comp];
    std::array<int, 16>& dc = m_current_idc[here].dc[comp];
    const auto c = [mindc, maxdc](int v) { return clip(v, mindc, maxdc); };

    if (is_luma && (flags & 0x20) != 0 && (flags & 0x80) != 0) {
        dc[0] = c(tdc[0]);
        dc[1] = c(dc[0] + tdc[1]);
        dc[2] = c(dc_mpred2(dc[0], dc[1]) + tdc[2]);
        dc[3] = c(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3]);
        dc[4] = c(dc_mpred2(dc[1], dc[3]) + tdc[4]);
        dc[5] = c(dc[4] + tdc[5]);
        dc[6] = c(dc_mpred(dc[1], dc[3], dc[4]) + tdc[6]);
        dc[7] = c(dc_mpred(dc[4], dc[6], dc[5]) + tdc[7]);
        dc[8] = c(dc_mpred2(dc[2], dc[3]) + tdc[8]);
        dc[9] = c(dc_mpred(dc[2], dc[8], dc[3]) + tdc[9]);
        dc[10] = c(dc_mpred2(dc[8], dc[9]) + tdc[10]);
        dc[11] = c(dc_mpred(dc[8], dc[10], dc[9]) + tdc[11]);
        dc[12] = c(dc_mpred(dc[3], dc[9], dc[6]) + tdc[12]);
        dc[13] = c(dc_mpred(dc[6], dc[12], dc[7]) + tdc[13]);
        dc[14] = c(dc_mpred(dc[9], dc[11], dc[12]) + tdc[14]);
        dc[15] = c(dc_mpred(dc[12], dc[14], dc[13]) + tdc[15]);
    } else if (is_luma && (flags & 0x80) != 0) {
        dc[0] = c(dc_mpred2(Ldc[5], Ldc[7]) + tdc[0]);
        dc[1] = c(dc[0] + tdc[1]);
        dc[2] = c(dc_mpred(Ldc[5], Ldc[7], dc[0]) + tdc[2]);
        dc[3] = c(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3]);
        dc[4] = c(dc_mpred2(dc[1], dc[3]) + tdc[4]);
        dc[5] = c(dc[4] + tdc[5]);
        dc[6] = c(dc_mpred(dc[1], dc[3], dc[4]) + tdc[6]);
        dc[7] = c(dc_mpred(dc[4], dc[6], dc[5]) + tdc[7]);
        dc[8] = c(dc_mpred(Ldc[7], Ldc[13], dc[2]) + tdc[8]);
        dc[9] = c(dc_mpred(dc[2], dc[8], dc[3]) + tdc[9]);
        dc[10] = c(dc_mpred(Ldc[13], Ldc[15], dc[8]) + tdc[10]);
        dc[11] = c(dc_mpred(dc[8], dc[10], dc[9]) + tdc[11]);
        dc[12] = c(dc_mpred(dc[3], dc[9], dc[6]) + tdc[12]);
        dc[13] = c(dc_mpred(dc[6], dc[12], dc[7]) + tdc[13]);
        dc[14] = c(dc_mpred(dc[9], dc[11], dc[12]) + tdc[14]);
        dc[15] = c(dc_mpred(dc[12], dc[14], dc[13]) + tdc[15]);
    } else if (is_luma && (flags & 0x20) != 0) {
        dc[0] = c(dc_mpred2(Tdc[10], Tdc[11]) + tdc[0]);
        dc[1] = c(dc_mpred(Tdc[10], dc[0], Tdc[11]) + tdc[1]);
        dc[2] = c(dc_mpred2(dc[0], dc[1]) + tdc[2]);
        dc[3] = c(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3]);
        dc[4] = c(dc_mpred(Tdc[11], dc[1], Tdc[14]) + tdc[4]);
        dc[5] = c(dc_mpred(Tdc[14], dc[4], Tdc[15]) + tdc[5]);
        dc[6] = c(dc_mpred(dc[1], dc[3], dc[4]) + tdc[6]);
        dc[7] = c(dc_mpred(dc[4], dc[6], dc[5]) + tdc[7]);
        dc[8] = c(dc_mpred2(dc[2], dc[3]) + tdc[8]);
        dc[9] = c(dc_mpred(dc[2], dc[8], dc[3]) + tdc[9]);
        dc[10] = c(dc_mpred2(dc[8], dc[9]) + tdc[10]);
        dc[11] = c(dc_mpred(dc[8], dc[10], dc[9]) + tdc[11]);
        dc[12] = c(dc_mpred(dc[3], dc[9], dc[6]) + tdc[12]);
        dc[13] = c(dc_mpred(dc[6], dc[12], dc[7]) + tdc[13]);
        dc[14] = c(dc_mpred(dc[9], dc[11], dc[12]) + tdc[14]);
        dc[15] = c(dc_mpred(dc[12], dc[14], dc[13]) + tdc[15]);
    } else if (is_luma) {
        dc[0] = c(dc_mpred(LTdc[15], Ldc[5], Tdc[10]) + tdc[0]);
        dc[1] = c(dc_mpred(Tdc[10], dc[0], Tdc[11]) + tdc[1]);
        dc[2] = c(dc_mpred(Ldc[5], Ldc[7], dc[0]) + tdc[2]);
        dc[3] = c(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3]);
        dc[4] = c(dc_mpred(Tdc[11], dc[1], Tdc[14]) + tdc[4]);
        dc[5] = c(dc_mpred(Tdc[14], dc[4], Tdc[15]) + tdc[5]);
        dc[6] = c(dc_mpred(dc[1], dc[3], dc[4]) + tdc[6]);
        dc[7] = c(dc_mpred(dc[4], dc[6], dc[5]) + tdc[7]);
        dc[8] = c(dc_mpred(Ldc[7], Ldc[13], dc[2]) + tdc[8]);
        dc[9] = c(dc_mpred(dc[2], dc[8], dc[3]) + tdc[9]);
        dc[10] = c(dc_mpred(Ldc[13], Ldc[15], dc[8]) + tdc[10]);
        dc[11] = c(dc_mpred(dc[8], dc[10], dc[9]) + tdc[11]);
        dc[12] = c(dc_mpred(dc[3], dc[9], dc[6]) + tdc[12]);
        dc[13] = c(dc_mpred(dc[6], dc[12], dc[7]) + tdc[13]);
        dc[14] = c(dc_mpred(dc[9], dc[11], dc[12]) + tdc[14]);
        dc[15] = c(dc_mpred(dc[12], dc[14], dc[13]) + tdc[15]);
    } else if ((flags & 0x20) != 0 && (flags & 0x80) != 0) {
        dc[0] = c(tdc[0]);
        dc[1] = c(dc[0] + tdc[1]);
        dc[2] = c(dc_mpred2(dc[0], dc[1]) + tdc[2]);
        dc[3] = c(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3]);
    } else if ((flags & 0x80) != 0) {
        dc[0] = c(dc_mpred2(Ldc[1], Ldc[3]) + tdc[0]);
        dc[1] = c(dc[0] + tdc[1]);
        dc[2] = c(dc_mpred(Ldc[1], Ldc[3], dc[0]) + tdc[2]);
        dc[3] = c(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3]);
    } else if ((flags & 0x20) != 0) {
        dc[0] = c(dc_mpred2(Tdc[2], Tdc[3]) + tdc[0]);
        dc[1] = c(dc_mpred(Tdc[2], dc[0], Tdc[3]) + tdc[1]);
        dc[2] = c(dc_mpred2(dc[0], dc[1]) + tdc[2]);
        dc[3] = c(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3]);
    } else {
        dc[0] = c(dc_mpred(LTdc[3], Ldc[1], Tdc[2]) + tdc[0]);
        dc[1] = c(dc_mpred(Tdc[2], dc[0], Tdc[3]) + tdc[1]);
        dc[2] = c(dc_mpred(Ldc[1], Ldc[3], dc[0]) + tdc[2]);
        dc[3] = c(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3]);
    }
}

void Bink2Video::g_decode_dc(bool is_luma, int q, int mindc, int maxdc, int flags) {
    Bits& gb = *m_gb;
    std::array<int, 16> tdc{};

    q = std::max(q, 8);

    if (gb.bit() != 0) {
        const int num_dc = is_luma ? 16 : 4;
        const int pat = bink2g_dc_pat[q];

        for (int i = 0; i < num_dc; i++) {
            int cnt = gb.unary(0, 12);
            if (cnt > 3) {
                cnt = (1 << (cnt - 3)) + static_cast<int>(gb.bits(cnt - 3)) + 2;
            }
            if (cnt != 0) {
                cnt = (cnt * pat + 0x200) >> 10;
                if (gb.bit() != 0) {
                    cnt = -cnt;
                }
            }
            tdc[static_cast<std::size_t>(i)] = cnt;
        }
    }

    if ((flags & 0x20) != 0 && (flags & 0x80) != 0 && mindc >= 0) {
        tdc[0] += 1024;
    }

    g_predict_dc(is_luma, mindc, maxdc, flags, tdc);
}

void Bink2Video::g_smooth_edges(const std::array<std::uint16_t, 4>& dering) {
    for (auto& b : m_iblock) {
        transpose_block(b.data());
    }
    if ((m_frame_flags & 0x44000) == 0) {
        dering_h(m_iblock[0].data(), m_iblock[1].data(), dering[0], dering[1]);
        dering_h(m_iblock[2].data(), m_iblock[3].data(), dering[2], dering[3]);
    }
    if ((m_frame_flags & 0x48000) == 0) {
        dering_w(m_iblock[0].data(), m_iblock[2].data(), dering[0], dering[2]);
        dering_w(m_iblock[1].data(), m_iblock[3].data(), dering[1], dering[3]);
    }
}

int Bink2Video::g_decode_intra_luma(unsigned& prev_cbp, int q, std::uint8_t* dst, int stride, int flags) {
    const std::array<int, 16>& dc = m_current_idc[m_mb_pos].dc[static_cast<std::size_t>(m_comp)];
    const unsigned cbp = prev_cbp = g_decode_cbp_luma(prev_cbp);

    g_decode_dc(true, q, 0, 2047, flags);

    for (int i = 0; i < 4; i++) {
        const std::array<std::uint16_t, 4> dering{};
        if (decode_ac(*m_gb, bink2g_scan, m_iblock, cbp >> (4 * i), q, bink2g_luma_intra_qmat, nullptr) < 0) {
            return -1;
        }
        for (std::size_t j = 0; j < 4; j++) {
            m_iblock[j][0] = static_cast<std::int16_t>(dc[static_cast<std::size_t>(i) * 4 + j] * 8 + 32);
            idct(m_iblock[j].data());
        }
        g_smooth_edges(dering);
        for (std::size_t j = 0; j < 4; j++) {
            const int at = kLumaRepos[static_cast<std::size_t>(i) * 4 + j];
            idct_put(dst + (at & 3) * 8 + (at >> 2) * 8 * stride, stride, m_iblock[j].data());
        }
    }
    return 0;
}

int Bink2Video::g_decode_intra_chroma(unsigned& prev_cbp, int q, std::uint8_t* dst, int stride, int flags) {
    const std::array<int, 16>& dc = m_current_idc[m_mb_pos].dc[static_cast<std::size_t>(m_comp)];
    const std::array<std::uint16_t, 4> dering{};
    const unsigned cbp = prev_cbp = decode_cbp_chroma(*m_gb, prev_cbp);

    g_decode_dc(false, q, 0, 2047, flags);

    if (decode_ac(*m_gb, bink2g_scan, m_iblock, cbp, q, bink2g_chroma_intra_qmat, nullptr) < 0) {
        return -1;
    }
    for (std::size_t j = 0; j < 4; j++) {
        m_iblock[j][0] = static_cast<std::int16_t>(dc[j] * 8 + 32);
        idct(m_iblock[j].data());
    }
    g_smooth_edges(dering);
    for (std::size_t j = 0; j < 4; j++) {
        idct_put(dst + static_cast<int>(j & 1) * 8 + static_cast<int>(j >> 1) * 8 * stride, stride, m_iblock[j].data());
    }
    return 0;
}

int Bink2Video::g_decode_inter_luma(unsigned& prev_cbp, int q, std::uint8_t* dst, int stride) {
    const int clipdc = (m_frame_flags & 0x40000) != 0 ? 2047 : 1023;
    const std::array<int, 16>& dc = m_current_idc[m_mb_pos].dc[static_cast<std::size_t>(m_comp)];
    const unsigned cbp = prev_cbp = g_decode_cbp_luma(prev_cbp);

    g_decode_dc(true, q, -clipdc, clipdc, 0xA8);

    for (int i = 0; i < 4; i++) {
        std::array<std::uint16_t, 4> dering{};
        if (decode_ac(*m_gb, bink2g_scan, m_iblock, cbp >> (4 * i), q, bink2g_inter_qmat, dering.data()) < 0) {
            return -1;
        }
        for (std::size_t j = 0; j < 4; j++) {
            dering[j] = static_cast<std::uint16_t>(dering[j] | (dc[static_cast<std::size_t>(i) * 4 + j] & 0x1FFF));
        }
        for (std::size_t j = 0; j < 4; j++) {
            m_iblock[j][0] = static_cast<std::int16_t>(dc[static_cast<std::size_t>(i) * 4 + j] * 8 + 32);
            idct(m_iblock[j].data());
        }
        g_smooth_edges(dering);
        for (std::size_t j = 0; j < 4; j++) {
            const int at = kLumaRepos[static_cast<std::size_t>(i) * 4 + j];
            std::uint8_t* d = dst + (at & 3) * 8 + (at >> 2) * 8 * stride;
            add_residual(d, stride, m_iblock[j].data());
            idct_put(d, stride, m_iblock[j].data());
        }
    }
    return 0;
}

int Bink2Video::g_decode_inter_chroma(unsigned& prev_cbp, int q, std::uint8_t* dst, int stride) {
    const int clipdc = (m_frame_flags & 0x40000) != 0 ? 2047 : 1023;
    const std::array<int, 16>& dc = m_current_idc[m_mb_pos].dc[static_cast<std::size_t>(m_comp)];
    std::array<std::uint16_t, 4> dering{};
    const unsigned cbp = prev_cbp = decode_cbp_chroma(*m_gb, prev_cbp);

    g_decode_dc(false, q, -clipdc, clipdc, 0xA8);

    if (decode_ac(*m_gb, bink2g_scan, m_iblock, cbp, q, bink2g_inter_qmat, dering.data()) < 0) {
        return -1;
    }
    for (std::size_t j = 0; j < 4; j++) {
        dering[j] = static_cast<std::uint16_t>(dering[j] | (dc[j] & 0x1FFF));
    }
    for (std::size_t j = 0; j < 4; j++) {
        m_iblock[j][0] = static_cast<std::int16_t>(dc[j] * 8 + 32);
        idct(m_iblock[j].data());
    }
    g_smooth_edges(dering);
    for (std::size_t j = 0; j < 4; j++) {
        std::uint8_t* d = dst + static_cast<int>(j & 1) * 8 + static_cast<int>(j >> 1) * 8 * stride;
        add_residual(d, stride, m_iblock[j].data());
        idct_put(d, stride, m_iblock[j].data());
    }
    return 0;
}

void Bink2Video::g_predict_mv(int flags, MVectors mv) {
    const auto left = static_cast<std::size_t>(std::max(m_mb_pos - 1, 0));
    const auto here = static_cast<std::size_t>(m_mb_pos);
    auto& c_mv = m_current_mv[here].v;
    const auto& l_mv = m_current_mv[left].v;
    const auto& lt_mv = m_prev_mv[left].v;
    const auto& t_mv = m_prev_mv[here].v;

    if (mv.nb_vectors == 1) {
        if ((flags & 0x80) != 0) {
            if ((flags & 0x20) == 0) {
                mv.v[0][0] += mid_pred(l_mv[0][0], l_mv[1][0], l_mv[3][0]);
                mv.v[0][1] += mid_pred(l_mv[0][1], l_mv[1][1], l_mv[3][1]);
            }
        } else {
            if ((flags & 0x20) == 0) {
                mv.v[0][0] += mid_pred(lt_mv[3][0], t_mv[2][0], l_mv[1][0]);
                mv.v[0][1] += mid_pred(lt_mv[3][1], t_mv[2][1], l_mv[1][1]);
            } else {
                mv.v[0][0] += mid_pred(t_mv[0][0], t_mv[2][0], t_mv[3][0]);
                mv.v[0][1] += mid_pred(t_mv[0][1], t_mv[2][1], t_mv[3][1]);
            }
        }
        for (auto& v : c_mv) {
            v = mv.v[0];
        }
        return;
    }

    if ((flags & 0x80) == 0) {
        if ((flags & 0x20) != 0) {
            for (std::size_t k = 0; k < 2; ++k) {
                c_mv[0][k] = mv.v[0][k] + mid_pred(t_mv[0][k], t_mv[2][k], t_mv[3][k]);
                c_mv[1][k] = mv.v[1][k] + mid_pred(t_mv[2][k], t_mv[3][k], c_mv[0][k]);
                c_mv[2][k] = mv.v[2][k] + mid_pred(t_mv[2][k], c_mv[0][k], c_mv[1][k]);
                c_mv[3][k] = mv.v[3][k] + mid_pred(c_mv[0][k], c_mv[1][k], c_mv[2][k]);
            }
        } else {
            for (std::size_t k = 0; k < 2; ++k) {
                c_mv[0][k] = mv.v[0][k] + mid_pred(t_mv[2][k], lt_mv[3][k], l_mv[1][k]);
                c_mv[1][k] = mv.v[1][k] + mid_pred(t_mv[2][k], t_mv[3][k], c_mv[0][k]);
                c_mv[2][k] = mv.v[2][k] + mid_pred(l_mv[1][k], l_mv[3][k], c_mv[0][k]);
                c_mv[3][k] = mv.v[3][k] + mid_pred(c_mv[0][k], c_mv[1][k], c_mv[2][k]);
            }
        }
    } else {
        if ((flags & 0x20) != 0) {
            for (std::size_t k = 0; k < 2; ++k) {
                c_mv[0][k] = mv.v[0][k];
                c_mv[1][k] = mv.v[1][k] + mv.v[0][k];
                c_mv[2][k] = mv.v[2][k] + mv.v[0][k];
                c_mv[3][k] = mv.v[3][k] + mid_pred(c_mv[0][k], c_mv[1][k], c_mv[2][k]);
            }
        } else {
            for (std::size_t k = 0; k < 2; ++k) {
                c_mv[0][k] = mv.v[0][k] + mid_pred(l_mv[0][k], l_mv[1][k], l_mv[3][k]);
                c_mv[2][k] = mv.v[2][k] + mid_pred(l_mv[1][k], l_mv[3][k], c_mv[0][k]);
                c_mv[1][k] = mv.v[1][k] + mid_pred(l_mv[1][k], c_mv[0][k], c_mv[2][k]);
                c_mv[3][k] = mv.v[3][k] + mid_pred(c_mv[0][k], c_mv[1][k], c_mv[2][k]);
            }
        }
    }
}

int Bink2Video::g_decode_mv(MVectors& mv) {
    Bits& gb = *m_gb;
    const int num_mvs = gb.bit() != 0 ? 1 : 4;
    mv.nb_vectors = num_mvs;

    for (std::size_t i = 0; i < 2; i++) {
        for (std::size_t j = 0; j < static_cast<std::size_t>(num_mvs); j++) {
            int val = kMvVlc.read(gb);
            if (val < 0) {
                return -1;
            }
            if (val >= 8 && val != 15) {
                val = val - 15;
            }
            if (val == 15) {
                const int bits = gb.unary(1, 12) + 4;
                val = static_cast<int>(gb.bits(bits)) + (1 << bits) - 1;
                if ((val & 1) != 0) {
                    val = (-(val >> 1)) - 1;
                } else {
                    val = val >> 1;
                }
            }
            mv.v[j][i] = val;
        }
    }
    return 0;
}

int Bink2Video::g_update_intra_q(std::int8_t& intra_q, int dq, int flags) {
    int q = 0;
    if ((flags & 0x20) != 0 && (flags & 0x80) != 0) {
        q = 16 + dq;
    } else if ((flags & 0x80) != 0) {
        q = m_current_q[m_mb_pos - 1].intra_q + dq;
    } else if ((flags & 0x20) != 0) {
        q = m_prev_q[m_mb_pos].intra_q + dq;
    } else {
        q = mid_pred<int>(m_prev_q[m_mb_pos].intra_q, m_current_q[m_mb_pos - 1].intra_q,
                          m_prev_q[m_mb_pos - 1].intra_q) +
            dq;
    }
    intra_q = static_cast<std::int8_t>(q);
    return intra_q < 0 || intra_q >= static_cast<int>(std::size(bink2g_dc_pat)) ? -1 : 0;
}

int Bink2Video::g_update_inter_q(std::int8_t& inter_q, int dq, int flags) {
    int q = 0;
    if ((flags & 0x20) != 0 && (flags & 0x80) != 0) {
        q = 16 + dq;
    } else if ((flags & 0x80) != 0) {
        q = m_current_q[m_mb_pos - 1].inter_q + dq;
    } else if ((flags & 0x20) != 0) {
        q = m_prev_q[m_mb_pos].inter_q + dq;
    } else {
        q = mid_pred<int>(m_prev_q[m_mb_pos].inter_q, m_current_q[m_mb_pos - 1].inter_q,
                          m_prev_q[m_mb_pos - 1].inter_q) +
            dq;
    }
    inter_q = static_cast<std::int8_t>(q);
    return inter_q < 0 || inter_q >= static_cast<int>(std::size(bink2g_dc_pat)) ? -1 : 0;
}

void Bink2Video::g_mcompensate_chroma(int x, int y, std::uint8_t* dst, int stride, const std::uint8_t* src, int sstride,
                                      int width, int height) {
    const auto& mv = m_current_mv[m_mb_pos].v;
    for (int k = 0; k < 4; ++k) {
        const auto& v = mv[static_cast<std::size_t>(k)];
        const int bx = (k & 1) * 8;
        const int by = (k >> 1) * 8;
        const int mv_x = (v[0] >> 2) + x + bx;
        const int mv_y = (v[1] >> 2) + y + by;
        const int mode = (v[0] & 3) | ((v[1] & 3) << 2);
        c_mc(dst + x + bx + by * stride, stride, src, sstride, width, height, mv_x, mv_y, mode);
    }
}

void Bink2Video::g_mcompensate_luma(int x, int y, std::uint8_t* dst, int stride, const std::uint8_t* src, int sstride,
                                    int width, int height) {
    const auto& mv = m_current_mv[m_mb_pos].v;
    for (int k = 0; k < 4; ++k) {
        const auto& v = mv[static_cast<std::size_t>(k)];
        const int bx = (k & 1) * 16;
        const int by = (k >> 1) * 16;
        const int mv_x = (v[0] >> 1) + x + bx;
        const int mv_y = (v[1] >> 1) + y + by;
        const int mode = (v[0] & 1) | ((v[1] & 1) << 1);
        y_mc(dst + x + bx + by * stride, stride, src, sstride, width, height, mv_x, mv_y, mode);
    }
}

int Bink2Video::g_decode_slice(Planes& p, bool key_frame, int start, int end) {
    Bits& gb = *m_gb;
    const bool has_alpha = m_has_alpha && (m_frame_flags & 0x80000) == 0;
    // Motion compensation reaches across the width of whole macroblocks, but only down to the real
    // height (as the block flags count them): with the real width the German credits of the
    // History Edition (432 pixels) smear at the right edge against the Königsedition's frames, with
    // the rounded height 295 (352 x 216) differs from Librempeg, which matches 93 of the 94 videos
    // bit for bit otherwise.
    const int w = (m_width + 31) & ~31;
    const int h = m_height;
    const auto mbs = static_cast<std::size_t>((w + 31) / 32);
    auto& dst = p.dst;
    const auto& stride = p.stride;
    const auto& src = p.src;
    const auto& sstride = p.sstride;

    if (m_has_alpha && !has_alpha) {
        const auto alpha = static_cast<std::uint8_t>(m_frame_flags >> 24);
        for (int y = 0; y < end - start; y++) {
            std::memset(dst[3] + y * stride[3], alpha, static_cast<std::size_t>(w));
        }
    }

    std::fill(m_prev_q, m_prev_q + mbs, QuantPredict{});
    std::fill(m_prev_mv, m_prev_mv + mbs, MVectors{});

    for (int y = start; y < end; y += 32) {
        int types_lru[4] = {kMotion, kResidue, kSkip, kIntra};
        unsigned y_cbp_intra = 0;
        unsigned u_cbp_intra = 0;
        unsigned v_cbp_intra = 0;
        unsigned a_cbp_intra = 0;
        unsigned y_cbp_inter = 0;
        unsigned u_cbp_inter = 0;
        unsigned v_cbp_inter = 0;
        unsigned a_cbp_inter = 0;

        std::fill(m_current_q, m_current_q + mbs, QuantPredict{});
        std::fill(m_current_mv, m_current_mv + mbs, MVectors{});

        for (int x = 0; x < w; x += 32) {
            const int type = key_frame ? kIntra : get_type(gb, types_lru);
            std::int8_t& intra_q = m_current_q[x / 32].intra_q;
            std::int8_t& inter_q = m_current_q[x / 32].inter_q;
            MVectors mv{};

            m_mb_pos = x / 32;
            m_current_idc[m_mb_pos].block_type = type;
            int flags = 0;
            if (y == start) {
                flags |= 0x80;
            }
            if (x == 0) {
                flags |= 0x20;
            }
            if ((x & 63) != 0) {
                flags |= 0x200;
            }
            if (x + 32 < w) {
                flags |= 0x40;
            }
            switch (type) {
                case kIntra:
                    g_predict_mv(flags, mv);
                    if (g_update_inter_q(inter_q, 0, flags) < 0) {
                        return -1;
                    }
                    if (g_update_intra_q(intra_q, decode_dq(gb), flags) < 0) {
                        return -1;
                    }
                    m_comp = 0;
                    if (g_decode_intra_luma(y_cbp_intra, intra_q, dst[0] + x, stride[0], flags) < 0) {
                        return -1;
                    }
                    m_comp = 1;
                    if (g_decode_intra_chroma(u_cbp_intra, intra_q, dst[2] + x / 2, stride[2], flags) < 0) {
                        return -1;
                    }
                    m_comp = 2;
                    if (g_decode_intra_chroma(v_cbp_intra, intra_q, dst[1] + x / 2, stride[1], flags) < 0) {
                        return -1;
                    }
                    if (has_alpha) {
                        m_comp = 3;
                        if (g_decode_intra_luma(a_cbp_intra, intra_q, dst[3] + x, stride[3], flags) < 0) {
                            return -1;
                        }
                    }
                    break;
                case kSkip:
                    if (g_update_intra_q(intra_q, 0, flags) < 0 || g_update_inter_q(inter_q, 0, flags) < 0) {
                        return -1;
                    }
                    copy_block(dst[0] + x, src[0] + x + sstride[0] * y, stride[0], sstride[0], 32, 32);
                    copy_block(dst[1] + x / 2, src[1] + x / 2 + sstride[1] * (y / 2), stride[1], sstride[1], 16, 16);
                    copy_block(dst[2] + x / 2, src[2] + x / 2 + sstride[2] * (y / 2), stride[2], sstride[2], 16, 16);
                    if (has_alpha) {
                        copy_block(dst[3] + x, src[3] + x + sstride[3] * y, stride[3], sstride[3], 32, 32);
                    }
                    break;
                case kMotion:
                case kResidue:
                    if (g_update_intra_q(intra_q, 0, flags) < 0) {
                        return -1;
                    }
                    if (type == kMotion && g_update_inter_q(inter_q, 0, flags) < 0) {
                        return -1;
                    }
                    if (g_decode_mv(mv) < 0) {
                        return -1;
                    }
                    g_predict_mv(flags, mv);
                    if (type == kResidue && g_update_inter_q(inter_q, decode_dq(gb), flags) < 0) {
                        return -1;
                    }
                    m_comp = 0;
                    g_mcompensate_luma(x, y, dst[0], stride[0], src[0], sstride[0], w, h);
                    m_comp = 1;
                    g_mcompensate_chroma(x / 2, y / 2, dst[2], stride[2], src[2], sstride[2], w / 2, h / 2);
                    m_comp = 2;
                    g_mcompensate_chroma(x / 2, y / 2, dst[1], stride[1], src[1], sstride[1], w / 2, h / 2);
                    if (has_alpha) {
                        m_comp = 3;
                        g_mcompensate_luma(x, y, dst[3], stride[3], src[3], sstride[3], w, h);
                    }
                    if (type == kMotion) {
                        break;
                    }
                    m_comp = 0;
                    if (g_decode_inter_luma(y_cbp_inter, inter_q, dst[0] + x, stride[0]) < 0) {
                        return -1;
                    }
                    if (gb.bit() != 0) {
                        m_comp = 1;
                        if (g_decode_inter_chroma(u_cbp_inter, inter_q, dst[2] + x / 2, stride[2]) < 0) {
                            return -1;
                        }
                        m_comp = 2;
                        if (g_decode_inter_chroma(v_cbp_inter, inter_q, dst[1] + x / 2, stride[1]) < 0) {
                            return -1;
                        }
                    } else {
                        u_cbp_inter = 0;
                        v_cbp_inter = 0;
                    }
                    if (has_alpha) {
                        m_comp = 3;
                        if (g_decode_inter_luma(a_cbp_inter, inter_q, dst[3] + x, stride[3]) < 0) {
                            return -1;
                        }
                    }
                    break;
                default:
                    return -1;
            }

            if (m_current_idc[m_mb_pos].block_type != kIntra) {
                auto& dc = m_current_idc[m_mb_pos].dc;
                average_luma(x, dst[0], stride[0], dc[0]);
                average_chroma(x / 2, dst[2], stride[2], dc[1]);
                average_chroma(x / 2, dst[1], stride[1], dc[2]);
                if (has_alpha) {
                    average_luma(x, dst[3], stride[3], dc[3]);
                }
            }
        }

        dst[0] += stride[0] * 32;
        dst[1] += stride[1] * 16;
        dst[2] += stride[2] * 16;
        if (dst[3] != nullptr) {
            dst[3] += stride[3] * 32;
        }

        std::swap(m_current_mv, m_prev_mv);
        std::swap(m_current_q, m_prev_q);
        std::swap(m_current_idc, m_prev_idc);
    }
    return 0;
}

}  // namespace o1602::video::bink

namespace o1602::video {
void bink_simd(bool enabled) noexcept {bink::use_simd=enabled;}
bool bink_simd_enabled() noexcept {return bink::use_simd;}
bool bink_simd_verified() noexcept {return bink::simd_verified();}
}
