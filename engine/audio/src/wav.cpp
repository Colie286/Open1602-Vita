// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/audio/wav.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <limits>
#include <optional>
#include <chrono>
#include <thread>
#include <atomic>
#include "o1602/core/profile.hpp"

#include "o1602/core/byte_reader.hpp"

namespace o1602::audio {

using core::ErrorCode;
using core::make_error;

namespace {

std::atomic<unsigned> stream_test_delay{0};
constexpr std::uint16_t kFormatPcm = 1;

struct Format {
    std::uint16_t tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits = 0;
};

bool tag_is(std::span<const std::uint8_t> bytes, std::size_t at, const char* tag) {
    return bytes.size() >= at + 4 && std::memcmp(bytes.data() + at, tag, 4) == 0;
}

core::Result<Pcm> decode_pcm(const Format& f, std::span<const std::uint8_t> data) {
    Pcm pcm;
    pcm.rate = f.rate;
    pcm.channels = static_cast<std::uint8_t>(f.channels);
    if (f.bits == 8) {
        pcm.samples.resize(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            pcm.samples[i] = static_cast<std::int16_t>((static_cast<std::int32_t>(data[i]) - 128) * 256);
        }
    } else if (f.bits == 16) {
        pcm.samples.resize(data.size() / 2);
        for (std::size_t i = 0; i < pcm.samples.size(); ++i) {
            pcm.samples[i] = static_cast<std::int16_t>(data[2 * i] | (data[2 * i + 1] << 8));
        }
    } else {
        return make_error(ErrorCode::Unsupported, "WAVE: " + std::to_string(f.bits) + "-bit PCM");
    }
    // Whole frames only.
    pcm.samples.resize(pcm.samples.size() - pcm.samples.size() % pcm.channels);
    return pcm;
}

// Step and index tables of IMA ADPCM (Intel/DVI), as Maxsound.dll keeps them.
constexpr std::array<std::int32_t, 89> kStep = {
    7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
    31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
    130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
    544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
    2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::array<std::int32_t, 16> kIndexStep = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

std::int16_t music_step(std::uint32_t n,std::int32_t& predictor,std::int32_t& index) {
    const auto step=kStep[static_cast<std::size_t>(index)];
    index=std::clamp(index+kIndexStep[n],0,88);
    std::int32_t diff=(((n&4U)?4*step:0)+((n&2U)?2*step:0)+((n&1U)?step:0))>>2;
    if(n&8U)diff=-diff;
    predictor=std::clamp(predictor+diff,-32768,32767);
    return static_cast<std::int16_t>(predictor);
}

/// The music of MUSIC8 (docs/formats/wav.md): one byte per stereo frame, high nibble left, IMA
/// steps without the rounding term of the reference code, one stream from the start of the file.
Pcm decode_music4(const Format& f, std::span<const std::uint8_t> data) {
    Pcm pcm;
    pcm.rate = f.rate;
    pcm.channels = 2;
    pcm.samples.resize(data.size() * 2);
    std::array<std::int32_t, 2> predictor{};
    std::array<std::int32_t, 2> index{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        for (std::size_t c = 0; c < 2; ++c) {
            const std::uint32_t n = c == 0 ? data[i] >> 4 : data[i] & 0x0FU;
            pcm.samples[2 * i + c] = music_step(n,predictor[c],index[c]);
        }
    }
    return pcm;
}

}  // namespace

namespace {
class WaveSource final:public SampleSource {
    static constexpr std::size_t kFrames=8192;
    std::FILE* m_file;
    Format m_format;
    std::string m_path;
    long m_data;
    std::size_t m_frames;
    mutable std::size_t m_first=0,m_count=0,m_next=0,m_previous=std::numeric_limits<std::size_t>::max();
    mutable std::array<std::int16_t,2> m_last{};
    mutable std::array<std::int32_t,2> m_predictor{},m_index{};
    mutable std::vector<std::uint8_t> m_encoded;
    mutable std::vector<std::int16_t> m_decoded;
    bool block() const {
        if(m_count) {
            m_previous=m_first+m_count-1;
            for(std::size_t c=0;c<m_format.channels;++c)m_last[c]=m_decoded[(m_count-1)*m_format.channels+c];
        }
        m_first=m_next;
        const std::size_t count=std::min(kFrames,m_frames-m_next);
        const std::size_t stride=m_format.bits==4?1:std::size_t(m_format.channels)*(m_format.bits/8);
        const auto start=std::chrono::steady_clock::now();
        const auto got=[&]{core::profile::Scope io(core::profile::FileIO);if(m_next>=8*kFrames){if(auto ms=stream_test_delay.exchange(0))std::this_thread::sleep_for(std::chrono::milliseconds(ms));}return std::fread(m_encoded.data(),1,count*stride,m_file);}();
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        if(us>10000)std::fprintf(stderr,"MUSIC_READ path=%s frame=%llu bytes=%u us=%llu\n",m_path.c_str(),
            (unsigned long long)m_next,unsigned(got),(unsigned long long)us);
        m_count=got/stride;
        for(std::size_t i=0;i<m_count;++i)for(std::size_t c=0;c<m_format.channels;++c) {
            std::int16_t value=0;
            if(m_format.bits==4)value=music_step(c==0?m_encoded[i]>>4:m_encoded[i]&15U,m_predictor[c],m_index[c]);
            else if(m_format.bits==8)value=static_cast<std::int16_t>((int(m_encoded[i*stride+c])-128)*256);
            else {const auto at=i*stride+c*2;value=static_cast<std::int16_t>(m_encoded[at]|(unsigned(m_encoded[at+1])<<8));}
            m_decoded[i*m_format.channels+c]=value;
        }
        m_next+=m_count;return m_count!=0;
    }
public:
    WaveSource(std::FILE* file,Format format,long data,std::size_t frames,std::string path):m_file(file),m_format(format),m_path(std::move(path)),m_data(data),m_frames(frames),
        m_encoded(kFrames*(format.bits==4?1:std::size_t(format.channels)*(format.bits/8))),m_decoded(kFrames*format.channels) {std::fseek(m_file,m_data,SEEK_SET);}
    ~WaveSource() override {std::fclose(m_file);}
    std::size_t frames() const noexcept override{return m_frames;}
    std::size_t buffered_bytes() const noexcept override{return m_encoded.capacity()+m_decoded.capacity()*sizeof(std::int16_t);}
    std::int16_t sample(std::size_t frame,std::uint8_t channel) const override {
        if(frame>=m_frames||channel>=m_format.channels)return 0;
        if(frame==m_previous)return m_last[channel];
        if(frame<m_first||frame>=m_first+m_count) {
            if(m_format.bits!=4) {
                m_next=frame/kFrames*kFrames;
                const auto stride=std::size_t(m_format.channels)*(m_format.bits/8);
                if(std::fseek(m_file,m_data+static_cast<long>(m_next*stride),SEEK_SET)!=0)return 0;
            } else if(frame<m_first) {
                m_predictor={};m_index={};m_next=0;
                if(std::fseek(m_file,m_data,SEEK_SET)!=0)return 0;
            }
            do {if(!block())return 0;}while(frame>=m_first+m_count);
        }
        return m_decoded[(frame-m_first)*m_format.channels+channel];
    }
};
}

void test_stream_delay(unsigned ms){stream_test_delay.store(std::min(ms,1000U));}

core::Result<Pcm> stream_wav(const std::filesystem::path& path) {
#ifdef _WIN32
    std::unique_ptr<std::FILE,decltype(&std::fclose)> file(_wfopen(path.c_str(),L"rb"),&std::fclose);
#else
    std::unique_ptr<std::FILE,decltype(&std::fclose)> file(std::fopen(path.c_str(),"rb"),&std::fclose);
#endif
    if(!file)return make_error(ErrorCode::IoError,"cannot open streamed WAVE");
    std::fseek(file.get(),0,SEEK_END);const long length=std::ftell(file.get());std::rewind(file.get());
    std::array<std::uint8_t,16> head{};
    if(length<12||std::fread(head.data(),1,12,file.get())!=12||!tag_is(head,0,"RIFF")||!tag_is(head,8,"WAVE"))return make_error(ErrorCode::InvalidFormat,"not a streamed RIFF WAVE file");
    std::optional<Format> format;long data=0;std::size_t bytes=0;
    for(long at=12;at<=length-8;) {
        if(std::fseek(file.get(),at,SEEK_SET)!=0||std::fread(head.data(),1,8,file.get())!=8)break;
        const std::uint32_t size=std::uint32_t(head[4])|(std::uint32_t(head[5])<<8)|(std::uint32_t(head[6])<<16)|(std::uint32_t(head[7])<<24);
        const auto available=static_cast<std::size_t>(length-at-8);
        const auto count=std::min<std::size_t>(size,available);
        if(tag_is(head,0,"fmt ")&&count>=16) {
            if(std::fread(head.data(),1,16,file.get())!=16)break;
            core::ByteReader reader(head);Format f;
            f.tag=reader.u16().value_or(0);f.channels=reader.u16().value_or(0);f.rate=reader.u32().value_or(0);
            reader.skip(4);f.block_align=reader.u16().value_or(0);f.bits=reader.u16().value_or(0);format=f;
        } else if(tag_is(head,0,"data")){data=at+8;bytes=count;}
        if(count>=available)break;
        at+=8+static_cast<long>(count+(count&1U));
    }
    if(!format||!data||format->tag!=1||!format->rate||format->channels<1||format->channels>2||
       (format->bits!=8&&format->bits!=16&&!(format->bits==4&&format->channels==2)))return make_error(ErrorCode::Unsupported,"unsupported streamed WAVE format");
    Pcm pcm;pcm.rate=format->rate;pcm.channels=static_cast<std::uint8_t>(format->channels);
    const auto stride=format->bits==4?1:std::size_t(format->channels)*(format->bits/8);
    auto source=std::make_shared<WaveSource>(file.get(),*format,data,bytes/stride,path.string());
    file.release();pcm.source=std::move(source);return pcm;
}

core::Result<Pcm> decode_wav(std::span<const std::uint8_t> bytes) {
    if (!tag_is(bytes, 0, "RIFF") || !tag_is(bytes, 8, "WAVE")) {
        return make_error(ErrorCode::InvalidFormat, "not a RIFF WAVE file");
    }
    core::ByteReader r(bytes);
    r.seek(12);
    std::optional<Format> format;
    std::optional<std::span<const std::uint8_t>> data;
    while (r.remaining() >= 8) {
        const std::size_t at = r.offset();
        r.skip(4);
        const auto size = r.u32();
        if (!size) {
            break;
        }
        // A data chunk running past the end (some writers leave the size open) is cut to the file.
        const std::size_t n = std::min<std::size_t>(*size, r.remaining());
        const auto body = bytes.subspan(r.offset(), n);
        if (tag_is(bytes, at, "fmt ") && n >= 16) {
            core::ByteReader fr(body);
            Format f;
            f.tag = fr.u16().value_or(0);
            f.channels = fr.u16().value_or(0);
            f.rate = fr.u32().value_or(0);
            fr.skip(4);  // bytes per second
            f.block_align = fr.u16().value_or(0);
            f.bits = fr.u16().value_or(0);
            format = f;
        } else if (tag_is(bytes, at, "data")) {
            data = body;
        }
        r.skip(n + (n & 1U));  // chunks are padded to even sizes
    }
    if (!format || !data) {
        return make_error(ErrorCode::InvalidFormat, "WAVE without fmt or data chunk");
    }
    if (format->channels == 0 || format->channels > 2 || format->rate == 0) {
        return make_error(ErrorCode::Unsupported, "WAVE: unsupported channels or rate");
    }
    if (format->tag == kFormatPcm && format->bits == 4 && format->channels == 2) {
        return decode_music4(*format, *data);
    }
    if (format->tag == kFormatPcm) {
        return decode_pcm(*format, *data);
    }
    return make_error(ErrorCode::Unsupported,
                      "WAVE: encoding " + std::to_string(format->tag) + ", " + std::to_string(format->bits) + " bit");
}

}  // namespace o1602::audio
