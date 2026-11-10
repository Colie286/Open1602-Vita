// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bink 2 video: the frame and its slices (Librempeg's libavcodec/bink2.c, see bink2_video.hpp).
// Copyright (c) 2014 Konstantin Shishkov
// Copyright (c) 2019 Paul B Mahol
#include "bink2_video.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <exception>
#include "o1602/video/bink.hpp"

#include "bink2_common.hpp"
#include "bink_bits.hpp"

namespace o1602::video::bink {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::array<std::uint8_t, 4> kKb2hNumSlices = {2, 3, 4, 8};

/// Flags of the block rows or columns (only skipped here: the decoder does not use them).
void get_block_flags(Bits& gb, int offset, int size, std::uint8_t* dst) {
    int v = 0;
    int mode = 0;
    unsigned flag = 0;

    if (gb.bit() == 0) {
        int j = 0;
        for (; j < size >> 3; j++) {
            dst[j] = static_cast<std::uint8_t>(gb.bits(8));
        }
        dst[j] = static_cast<std::uint8_t>(gb.bits(size & 7));
        return;
    }

    int flags_left = size;
    while (flags_left > 0) {
        const int cache = offset;
        if (gb.bit() == 0) {
            if (mode == 3) {
                flag ^= 1;
            } else {
                flag = gb.bit();
            }
            mode = 2;
            int nv = 0;
            if (flags_left < 5) {
                nv = static_cast<int>(gb.bits(flags_left - 1));
                nv <<= (offset + 1) & 0x1f;
                offset += flags_left;
                flags_left = 0;
            } else {
                nv = static_cast<int>(gb.bits(4) << ((offset + 1) & 0x1f));
                offset += 5;
                flags_left -= 5;
            }
            v |= static_cast<int>(flag << (cache & 0x1f)) | nv;
            if (offset >= 8) {
                *dst++ = static_cast<std::uint8_t>(v & 0xff);
                v >>= 8;
                offset -= 8;
            }
        } else {
            const int bits = flags_left < 4 ? 2 : flags_left < 16 ? 4 : 5;
            int nb_coded = bits + 1;
            if (mode == 3) {
                flag ^= 1;
            } else {
                nb_coded++;
                flag = gb.bit();
            }
            nb_coded = std::min(nb_coded, flags_left);
            flags_left -= nb_coded;
            if (flags_left > 0) {
                const int temp = static_cast<int>(gb.bits(bits));
                flags_left -= temp;
                nb_coded += temp;
                mode = temp == (1 << bits) - 1 ? 1 : 3;
            }

            // (flag << 31) >> 31 of an unsigned: the flag itself, as the reference has it.
            const int temp = static_cast<int>(flag & 1U);
            while (nb_coded > 8) {
                v |= temp << (cache & 0x1f);
                *dst++ = static_cast<std::uint8_t>(v & 0xff);
                v >>= 8;
                nb_coded -= 8;
            }
            if (nb_coded > 0) {
                offset += nb_coded;
                v |= static_cast<int>(((1U << (nb_coded & 0x1f)) - 1U) & static_cast<unsigned>(temp)) << (cache & 0x1f);
                if (offset >= 8) {
                    *dst++ = static_cast<std::uint8_t>(v & 0xff);
                    v >>= 8;
                    offset -= 8;
                }
            }
        }
    }

    if (offset != 0) {
        *dst = static_cast<std::uint8_t>(v);
    }
}

std::uint32_t load_u32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

}  // namespace

struct Bink2Video::SliceWorker {
    Bink2Video scratch;
    std::mutex mutex;
    std::condition_variable wake,done;
    bool quit=false,pending=false,finished=true,simd=true,key=false;
    std::span<const std::uint8_t> packet;
    std::size_t bit=0;
    std::uint32_t flags=0;
    Planes planes;
    int start=0,end=0,result=0;
    std::exception_ptr error;
    std::thread thread;
    SliceWorker(const Bink2Video& owner,std::function<void()> setup)
        :scratch(owner.m_width,owner.m_height,owner.m_revision,owner.m_flags,false),
         thread([this,setup=std::move(setup)] {
            try{if(setup)setup();}catch(...){} // decoder remains usable without platform tuning
            std::unique_lock lock(mutex);
            for(;;){
                wake.wait(lock,[&]{return quit||pending;});if(quit)return;
                pending=false;lock.unlock();
                try{
                    bink_simd(simd);Bits gb(packet);gb.seek(bit);
                    scratch.m_gb=&gb;scratch.m_frame_flags=flags;
                    result=scratch.g_decode_slice(planes,key,start,end);scratch.m_gb=nullptr;
                }catch(...){error=std::current_exception();result=-1;}
                lock.lock();finished=true;done.notify_one();
            }
        }){}
    ~SliceWorker(){
        {std::lock_guard lock(mutex);quit=true;}wake.notify_one();thread.join();
    }
    void submit(std::span<const std::uint8_t> bytes,std::size_t offset,std::uint32_t frame_flags,
                const Planes& output,bool key_frame,int from,int to){
        std::lock_guard lock(mutex);packet=bytes;bit=offset;flags=frame_flags;planes=output;
        key=key_frame;start=from;end=to;simd=bink_simd_enabled();error=nullptr;finished=false;pending=true;wake.notify_one();
    }
    int finish(){
        std::unique_lock lock(mutex);done.wait(lock,[&]{return finished;});
        if(error)std::rethrow_exception(error);return result;
    }
};
Bink2Video::~Bink2Video()=default;
bool Bink2Video::parallel_decode(bool enabled,std::function<void()> setup){
    if(!enabled){m_slice_worker.reset();return false;}
    if(m_slice_worker)return true;
    if(m_revision!='g'&&kKb2hNumSlices[m_flags&3]!=2)return false;
    try{m_slice_worker=std::make_unique<SliceWorker>(*this,std::move(setup));return true;}
    catch(const std::exception&){return false;}
}
Bink2Video::Bink2Video(std::int32_t width,std::int32_t height,char revision,std::uint32_t flags)
    :Bink2Video(width,height,revision,flags,true){}
Bink2Video::Bink2Video(std::int32_t width, std::int32_t height, char revision, std::uint32_t flags,bool allocate_planes)
    : m_width(width), m_height(height), m_revision(revision), m_flags(flags), m_has_alpha((flags & kFlagAlpha) != 0) {
    // The planes go to whole blocks of 32 pixels (chroma 16), one block more to the right and
    // below so that a block written at the edge never leaves them.
    const std::int32_t aw = (width + 31) & ~31;
    const std::int32_t ah = (height + 31) & ~31;
    for (std::size_t i = 0; allocate_planes && i < 4; ++i) {
        const bool chroma = i == 1 || i == 2;
        if (i == 3 && !m_has_alpha) {
            continue;
        }
        const std::int32_t w = chroma ? aw / 2 : aw;
        const std::int32_t h = chroma ? ah / 2 : ah;
        for (Plane* p : {&m_current[i], &m_last[i]}) {
            p->stride = w + 32;
            p->data.assign(static_cast<std::size_t>(p->stride) * static_cast<std::size_t>(h + 32),
                           static_cast<std::uint8_t>(i == 3 ? 255 : 0));
        }
    }
    const auto mbs = static_cast<std::size_t>((width + 31) / 32);
    for (std::size_t k = 0; k < 2; ++k) {
        m_q[k].assign(mbs, {});
        m_idc[k].assign(mbs, {});
        m_mv[k].assign(mbs, {});
    }
    m_current_q = m_q[0].data();
    m_prev_q = m_q[1].data();
    m_current_idc = m_idc[0].data();
    m_prev_idc = m_idc[1].data();
    m_current_mv = m_mv[0].data();
    m_prev_mv = m_mv[1].data();
    m_row_cbp.assign(static_cast<std::size_t>((((height + 31) >> 3) + 7) >> 3) + 8, 0);
    m_col_cbp.assign(static_cast<std::size_t>((((width + 31) >> 3) + 7) >> 3) + 8, 0);
}

core::Status Bink2Video::decode(std::span<const std::uint8_t> packet, bool key_frame) {
    if (m_revision < 'g') {
        return make_error(ErrorCode::InvalidFormat,
                          std::string("Bink 2: revision ") + m_revision + " is not supported");
    }
    if (!key_frame && !m_have_last) {
        return make_error(ErrorCode::InvalidFormat, "Bink 2: a frame before the first key frame");
    }
    if (packet.size() < 4) {
        return make_error(ErrorCode::InvalidFormat, "Bink 2: frame too short");
    }
    std::swap(m_current, m_last);
    Bits gb(packet);
    m_gb = &gb;
    m_frame_flags = load_u32(packet, 0);

    const int height_a = (m_height + 31) & ~31;
    int num_slices = 0;
    std::array<int, 8> slice_height{};
    if (m_revision == 'g') {
        if (height_a < 128) {
            num_slices = 1;
        } else {
            num_slices = 2;
            slice_height[0] = (m_height / 2 + 16) & ~31;
        }
    } else {
        num_slices = kKb2hNumSlices[m_flags & 3];
        int start = 0;
        int end = height_a + 32 * num_slices - 1;
        for (int i = 0; i < num_slices - 1; i++) {
            start += ((end - start) / (num_slices - i)) & ~31;
            end -= 32;
            slice_height[static_cast<std::size_t>(i)] = start;
        }
    }
    slice_height[static_cast<std::size_t>(num_slices - 1)] = height_a;
    if (packet.size() < 4 + 4 * static_cast<std::size_t>(num_slices - 1)) {
        m_gb = nullptr;
        return make_error(ErrorCode::InvalidFormat, "Bink 2: frame too short for its slices");
    }

    gb.skip(32 + 32 * (num_slices - 1));

    std::fill(m_row_cbp.begin(), m_row_cbp.end(), std::uint8_t{0});
    std::fill(m_col_cbp.begin(), m_col_cbp.end(), std::uint8_t{0});
    if ((m_frame_flags & 0x10000) != 0) {
        if ((m_frame_flags & 0x8000) == 0) {
            get_block_flags(gb, 1, (((m_height + 15) & ~15) >> 3) - 1, m_row_cbp.data());
        }
        if ((m_frame_flags & 0x4000) == 0) {
            // The columns of the width rounded up to whole macroblocks, the rows of the height to 16
            // pixels: the History Edition's German credits (432 x 352) have 55 column flags, its
            // 352 x 240 videos 29 row flags. Librempeg counts the columns as the rows and breaks on
            // the credits at their first frame with block flags.
            get_block_flags(gb, 1, (((m_width + 31) & ~31) >> 3) - 1, m_col_cbp.data());
        }
    }

    Planes p;
    for (std::size_t i = 0; i < 4; ++i) {
        p.dst[i] = m_current[i].data.empty() ? nullptr : m_current[i].data.data();
        p.stride[i] = m_current[i].stride;
        p.src[i] = m_last[i].data.empty() ? nullptr : m_last[i].data.data();
        p.sstride[i] = m_last[i].stride;
    }

    if(m_slice_worker&&num_slices==2){
        const auto offset=load_u32(packet,4);
        if(offset<8||offset>packet.size()){
            m_gb=nullptr;m_have_last=false;return make_error(ErrorCode::InvalidFormat,"Bink 2: damaged slice offset");
        }
        Planes other=p;
        const int middle=slice_height[0];
        for(std::size_t k=0;k<4;++k)if(other.dst[k])
            other.dst[k]+=(k==1||k==2?middle/2:middle)*other.stride[k];
        m_slice_worker->submit(packet,std::size_t(offset)*8,m_frame_flags,other,key_frame,middle,height_a);
        int first=-1,second=-1;std::exception_ptr error;
        try{first=g_decode_slice(p,key_frame,0,middle);}catch(...){error=std::current_exception();}
        try{second=m_slice_worker->finish();}catch(...){if(!error)error=std::current_exception();}
        m_gb=nullptr;
        if(error){m_have_last=false;std::rethrow_exception(error);}
        if(first<0||second<0){m_have_last=false;return make_error(ErrorCode::InvalidFormat,"Bink 2: damaged parallel slice");}
        m_have_last=true;return core::Ok{};
    }

    for (int i = 0; i < num_slices; i++) {
        const std::size_t off =
            i == num_slices - 1 ? packet.size() : load_u32(packet, 4 + static_cast<std::size_t>(i) * 4);
        const int start = i != 0 ? slice_height[static_cast<std::size_t>(i - 1)] : 0;
        const int end = slice_height[static_cast<std::size_t>(i)];
        const int ret = g_decode_slice(p, key_frame, start, end);
        if (ret < 0 || off > packet.size()) {
            m_gb = nullptr;
            m_have_last = false;
            return make_error(ErrorCode::InvalidFormat, "Bink 2: damaged slice " + std::to_string(i));
        }
        // The next slice starts where the frame's table says, whatever this one read.
        gb.seek(off * 8);

        const auto sh = static_cast<std::size_t>(end);
        for (std::size_t k = 0; k < 4; ++k) {
            if (p.dst[k] == nullptr) {
                continue;
            }
            const std::size_t lines = k == 1 || k == 2 ? sh / 2 : sh;
            p.dst[k] = m_current[k].data.data() + lines * static_cast<std::size_t>(p.stride[k]);
        }
    }
    m_gb = nullptr;
    m_have_last = true;
    return core::Ok{};
}

}  // namespace o1602::video::bink
