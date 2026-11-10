// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from the scalar transform in bink2_video_g.cpp (Librempeg bink2g.h).
// Copyright (c) 2014 Konstantin Shishkov
// Copyright (c) 2019 Paul B Mahol
#pragma once
// Integer Bink 2 transform, four independent columns per NEON vector.
// Widen before arithmetic and narrow without saturation, exactly as the scalar
// decoder's int -> int16_t assignments do, including intermediate wrapping.
#if defined(__ARM_NEON)
#include <arm_neon.h>
#include <cstdint>
namespace o1602::video::bink::neon {
inline void transpose(std::int16_t* b) noexcept {
    const auto a=vtrnq_s16(vld1q_s16(b),vld1q_s16(b+8));
    const auto c=vtrnq_s16(vld1q_s16(b+16),vld1q_s16(b+24));
    const auto d=vtrnq_s16(vld1q_s16(b+32),vld1q_s16(b+40));
    const auto e=vtrnq_s16(vld1q_s16(b+48),vld1q_s16(b+56));
    const auto f=vtrnq_s32(vreinterpretq_s32_s16(a.val[0]),vreinterpretq_s32_s16(c.val[0]));
    const auto g=vtrnq_s32(vreinterpretq_s32_s16(a.val[1]),vreinterpretq_s32_s16(c.val[1]));
    const auto h=vtrnq_s32(vreinterpretq_s32_s16(d.val[0]),vreinterpretq_s32_s16(e.val[0]));
    const auto i=vtrnq_s32(vreinterpretq_s32_s16(d.val[1]),vreinterpretq_s32_s16(e.val[1]));
    const int16x8_t top[]={vreinterpretq_s16_s32(f.val[0]),vreinterpretq_s16_s32(g.val[0]),vreinterpretq_s16_s32(f.val[1]),vreinterpretq_s16_s32(g.val[1])};
    const int16x8_t bottom[]={vreinterpretq_s16_s32(h.val[0]),vreinterpretq_s16_s32(i.val[0]),vreinterpretq_s16_s32(h.val[1]),vreinterpretq_s16_s32(i.val[1])};
    for(int n=0;n<4;++n){vst1q_s16(b+8*n,vcombine_s16(vget_low_s16(top[n]),vget_low_s16(bottom[n])));
        vst1q_s16(b+8*(n+4),vcombine_s16(vget_high_s16(top[n]),vget_high_s16(bottom[n])));}
}
template<int Shift> inline void columns(std::int16_t* b) noexcept {
    for(int column=0;column<8;column+=4){
        int32x4_t x[8];for(int n=0;n<8;++n)x[n]=vmovl_s16(vld1_s16(b+8*n+column));
        const auto a=[](int32x4_t v){return vaddq_s32(v,vshrq_n_s32(v,2));};
        const auto c=[](int32x4_t v){return vsubq_s32(vsubq_s32(v,vshrq_n_s32(v,2)),vshrq_n_s32(v,4));};
        const auto d=[](int32x4_t v){return vsubq_s32(vaddq_s32(v,vshrq_n_s32(v,2)),vshrq_n_s32(v,4));};
        const auto t00=vaddq_s32(x[3],x[5]),t01=vsubq_s32(x[3],x[5]);
        const auto t02=vaddq_s32(a(x[2]),vshrq_n_s32(x[6],1)),t03=vsubq_s32(vshrq_n_s32(x[2],1),a(x[6]));
        const auto sum=vaddq_s32(x[0],x[4]);
        const auto t0=vaddq_s32(sum,t02),t1=vsubq_s32(sum,t02),t2=vsubq_s32(x[0],x[4]);
        const auto t3=vaddq_s32(x[1],t00),t4=vsubq_s32(x[1],t00),t5=vaddq_s32(t01,x[7]),t6=vsubq_s32(t01,x[7]);
        const auto t7=vaddq_s32(t4,c(t6)),t8=vsubq_s32(c(t4),t6);
        const auto t9=vaddq_s32(d(t3),vshrq_n_s32(t5,2)),t10=vsubq_s32(vshrq_n_s32(t3,2),d(t5));
        const auto t11=vaddq_s32(t2,t03),t12=vsubq_s32(t2,t03);
        const int32x4_t result[]={vaddq_s32(t0,t9),vaddq_s32(t11,t7),vaddq_s32(t12,t8),vaddq_s32(t1,t10),
            vsubq_s32(t1,t10),vsubq_s32(t12,t8),vsubq_s32(t11,t7),vsubq_s32(t0,t9)};
        for(int n=0;n<8;++n){
            if constexpr(Shift==0)vst1_s16(b+8*n+column,vmovn_s32(result[n]));
            else vst1_s16(b+8*n+column,vmovn_s32(vshrq_n_s32(result[n],Shift)));
        }
    }
}
inline void idct(std::int16_t* block) noexcept {columns<0>(block);transpose(block);columns<6>(block);transpose(block);}
inline bool verify(void (*reference)(std::int16_t*)) noexcept {
    std::uint32_t rng=0x160215;
    for(unsigned test=0;test<194;++test){
        alignas(16) std::int16_t a[64]{},b[64]{};
        for(unsigned n=0;n<64;++n){rng=rng*1664525U+1013904223U;
            a[n]=test<64?(test==n?32767:0):test<128?(test-64==n?-32768:0):static_cast<std::int16_t>(rng>>16);b[n]=a[n];}
        reference(a);idct(b);
        for(unsigned n=0;n<64;++n)if(a[n]!=b[n])return false;
    }
    return true;
}
}
#endif
