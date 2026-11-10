// SPDX-License-Identifier: GPL-3.0-or-later
//
// The picture of Bink 2 videos ("KB2g" to "KB2n"), as the History Edition of Anno 1602 ships them.
// Ported from Librempeg's Bink 2 decoder (libavcodec/bink2.c, bink2f.h, bink2g.h; GPL-3.0-or-later,
// Copyright (c) 2014 Konstantin Shishkov, Copyright (c) 2019 Paul B Mahol), which was written from
// Konstantin Shishkov's analysis of the format. Only its half for the revisions "g" on (integer
// transforms) is here: the History Edition's videos are all "KB2j". The older revisions ("KB2a"
// to "KB2f", floating point transforms) are refused.
//
// The frames are YUV 4:2:0 (and an alpha plane if the file has one) in blocks of 32 x 32 pixels;
// a frame other than a key frame builds on the one before.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::video::bink {

class Bits;

/// One plane of a picture: `stride` bytes per line; the lines go down to whole blocks.
struct Plane {
    std::vector<std::uint8_t> data;
    std::int32_t stride = 0;
};

class Bink2Video {
public:
    /// Bit 20 of `flags` (the header's video flags): an alpha plane; bits 0..1: the slices of
    /// revisions "h" on.
    static constexpr std::uint32_t kFlagAlpha = 0x0010'0000U;

    Bink2Video(std::int32_t width, std::int32_t height, char revision, std::uint32_t flags);

    ~Bink2Video();
    bool parallel_decode(bool enabled,std::function<void()> setup);

    /// Decodes one frame's picture. A frame that is not a key frame needs the one before it; after
    /// a failure the picture is undefined until the next key frame.
    core::Status decode(std::span<const std::uint8_t> packet, bool key_frame);

    /// Y, U, V and (with an alpha plane) A of the last decoded frame.
    [[nodiscard]] const Plane& plane(std::size_t i) const noexcept { return m_current[i]; }
    [[nodiscard]] bool has_alpha() const noexcept { return m_has_alpha; }
    [[nodiscard]] std::int32_t width() const noexcept { return m_width; }
    [[nodiscard]] std::int32_t height() const noexcept { return m_height; }

private:
    Bink2Video(std::int32_t width,std::int32_t height,char revision,std::uint32_t flags,bool allocate_planes);
    struct SliceWorker;
    std::unique_ptr<SliceWorker> m_slice_worker;
    enum BlockType : int { kIntra = 0, kSkip = 1, kMotion = 2, kResidue = 3 };

    struct QuantPredict {
        std::int8_t intra_q = 0;
        std::int8_t inter_q = 0;
    };
    struct DciPredict {
        std::array<std::array<int, 16>, 4> dc{};
        int block_type = 0;
    };
    struct MVectors {
        std::array<std::array<int, 2>, 4> v{};
        int nb_vectors = 0;
    };

    /// The planes of a frame being decoded (`dst`) and of the one before (`src`), as the slices
    /// walk down them.
    struct Planes {
        std::array<std::uint8_t*, 4> dst{};
        std::array<std::int32_t, 4> stride{};
        std::array<std::uint8_t*, 4> src{};
        std::array<std::int32_t, 4> sstride{};
    };

    // bink2_video_g.cpp
    int g_decode_slice(Planes& p, bool key_frame, int start, int end);
    unsigned g_decode_cbp_luma(unsigned prev_cbp);
    void g_predict_dc(bool is_luma, int mindc, int maxdc, int flags, const std::array<int, 16>& tdc);
    void g_decode_dc(bool is_luma, int q, int mindc, int maxdc, int flags);
    int g_decode_intra_luma(unsigned& prev_cbp, int q, std::uint8_t* dst, int stride, int flags);
    int g_decode_intra_chroma(unsigned& prev_cbp, int q, std::uint8_t* dst, int stride, int flags);
    int g_decode_inter_luma(unsigned& prev_cbp, int q, std::uint8_t* dst, int stride);
    int g_decode_inter_chroma(unsigned& prev_cbp, int q, std::uint8_t* dst, int stride);
    void g_smooth_edges(const std::array<std::uint16_t, 4>& dering);
    void g_predict_mv(int flags, MVectors mv);
    int g_decode_mv(MVectors& mv);
    int g_update_intra_q(std::int8_t& intra_q, int dq, int flags);
    int g_update_inter_q(std::int8_t& inter_q, int dq, int flags);
    void g_mcompensate_luma(int x, int y, std::uint8_t* dst, int stride, const std::uint8_t* src, int sstride,
                            int width, int height);
    void g_mcompensate_chroma(int x, int y, std::uint8_t* dst, int stride, const std::uint8_t* src, int sstride,
                              int width, int height);

    std::int32_t m_width;
    std::int32_t m_height;
    char m_revision;
    std::uint32_t m_flags;
    bool m_has_alpha;
    bool m_have_last = false;  ///< a frame was decoded, the next may build on it

    std::array<Plane, 4> m_current;
    std::array<Plane, 4> m_last;

    Bits* m_gb = nullptr;  ///< the frame's bits while decode() runs
    std::uint32_t m_frame_flags = 0;
    int m_comp = 0;
    int m_mb_pos = 0;

    std::array<std::array<std::int16_t, 64>, 4> m_iblock{};

    std::vector<QuantPredict> m_q[2];  ///< current row, row above (swapped per row)
    std::vector<DciPredict> m_idc[2];
    std::vector<MVectors> m_mv[2];
    QuantPredict* m_current_q = nullptr;
    QuantPredict* m_prev_q = nullptr;
    DciPredict* m_current_idc = nullptr;
    DciPredict* m_prev_idc = nullptr;
    MVectors* m_current_mv = nullptr;
    MVectors* m_prev_mv = nullptr;
    std::vector<std::uint8_t> m_row_cbp;
    std::vector<std::uint8_t> m_col_cbp;
};

}  // namespace o1602::video::bink
