// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/compress.hpp"

#include <algorithm>
#include <array>
#include <limits>

#if defined(O1602_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace o1602::core {

struct ZlibDeflater::State {
#if defined(O1602_HAVE_ZLIB)
    z_stream stream{};
    bool ready=false;
    explicit State(int level):ready(deflateInit(&stream,level)==Z_OK){}
    ~State(){if(ready)deflateEnd(&stream);}
#else
    explicit State(int){}
#endif
    std::vector<std::uint8_t> output;
};
ZlibDeflater::ZlibDeflater(int level):m_state(std::make_unique<State>(level)){}
ZlibDeflater::~ZlibDeflater()=default;
Result<std::span<const std::uint8_t>> ZlibDeflater::encode(std::span<const std::uint8_t> input) {
#if defined(O1602_HAVE_ZLIB)
    auto& s=*m_state; auto& z=s.stream;
    if(!s.ready||input.size()>std::numeric_limits<uInt>::max())return make_error(ErrorCode::OutOfRange,"zlib: encoder unavailable or input too large");
    if(deflateReset(&z)!=Z_OK)return make_error(ErrorCode::InvalidFormat,"zlib: reset failed");
    const auto bound=deflateBound(&z,static_cast<uLong>(input.size()));
    if(bound>std::numeric_limits<uInt>::max())return make_error(ErrorCode::OutOfRange,"zlib: output too large");
    s.output.resize(bound);z.next_in=const_cast<Bytef*>(input.data());z.avail_in=static_cast<uInt>(input.size());
    z.next_out=s.output.data();z.avail_out=static_cast<uInt>(s.output.size());
    if(deflate(&z,Z_FINISH)!=Z_STREAM_END||z.avail_in!=0)return make_error(ErrorCode::InvalidFormat,"zlib: compression failed");
    return std::span<const std::uint8_t>(s.output.data(),z.total_out);
#else
    (void)input;return make_error(ErrorCode::Unsupported,"zlib: not available");
#endif
}

bool has_zlib() noexcept {
#if defined(O1602_HAVE_ZLIB)
    return true;
#else
    return false;
#endif
}

Result<std::vector<std::uint8_t>> zlib_deflate(std::span<const std::uint8_t> data, int level) {
#if defined(O1602_HAVE_ZLIB)
    if (data.size() > std::numeric_limits<uLong>::max()) {
        return make_error(ErrorCode::OutOfRange, "zlib: data too large");
    }
    uLongf size = compressBound(static_cast<uLong>(data.size()));
    std::vector<std::uint8_t> out(size);
    if (compress2(out.data(), &size, data.data(), static_cast<uLong>(data.size()), std::clamp(level, 1, 9)) != Z_OK) {
        return make_error(ErrorCode::InvalidFormat, "zlib: compression failed");
    }
    out.resize(size);
    return out;
#else
    (void)data;
    (void)level;
    return make_error(ErrorCode::Unsupported, "zlib: this build of Open1602 has no zlib");
#endif
}

namespace {

/// The data of a deflate stream, exactly `size` bytes: with zlib's header and checksum
/// (`window_bits` 15) or without (-15, as in a zip archive).
Result<std::vector<std::uint8_t>> inflate_exactly(std::span<const std::uint8_t> stream, std::size_t size,
                                                  [[maybe_unused]] int window_bits) {
#if defined(O1602_HAVE_ZLIB)
    // Deflate packs at most about 1:1032; a size beyond that is a lie of the header.
    constexpr std::size_t kMaxRatio = 1032;
    if (stream.size() > std::numeric_limits<uInt>::max() || size > stream.size() * kMaxRatio + 64) {
        return make_error(ErrorCode::InvalidFormat, "zlib: damaged stream");
    }
    // The output grows with what the stream really yields, so a header that promises much costs
    // memory only once the data delivers it.
    constexpr std::size_t kFirstStep = std::size_t{1} << 20;
    std::vector<std::uint8_t> out(std::min(size, kFirstStep));
    z_stream z{};
    if (inflateInit2(&z, window_bits) != Z_OK) {
        return make_error(ErrorCode::InvalidFormat, "zlib: cannot unpack");
    }
    z.next_in = const_cast<Bytef*>(stream.data());  // zlib's API is not const; it does not write
    z.avail_in = static_cast<uInt>(stream.size());
    int status = Z_OK;
    std::size_t got = 0;
    while (status == Z_OK) {  // Z_BUF_ERROR: cut off, or more data than the header said
        if (got == out.size() && out.size() < size) {
            out.resize(std::min(size, out.size() * 2));
        }
        const std::size_t room = std::min<std::size_t>(out.size() - got, std::numeric_limits<uInt>::max());
        std::uint8_t none = 0;  // zlib wants somewhere to write even when nothing is expected
        z.next_out = out.empty() ? &none : out.data() + got;
        z.avail_out = static_cast<uInt>(room);
        status = inflate(&z, Z_NO_FLUSH);
        got += room - z.avail_out;
    }
    const bool complete = status == Z_STREAM_END && z.avail_in == 0;
    inflateEnd(&z);
    if (!complete || got != size) {
        return make_error(ErrorCode::InvalidFormat, "zlib: damaged stream");
    }
    return out;
#else
    (void)stream;
    (void)size;
    return make_error(ErrorCode::Unsupported, "zlib: this build of Open1602 has no zlib");
#endif
}

}  // namespace

Result<std::vector<std::uint8_t>> zlib_inflate(std::span<const std::uint8_t> stream, std::size_t size) {
    return inflate_exactly(stream, size, 15);
}

Result<std::vector<std::uint8_t>> raw_inflate(std::span<const std::uint8_t> stream, std::size_t size) {
    return inflate_exactly(stream, size, -15);
}

Result<std::vector<std::uint8_t>> zlib_inflate_prefix(std::span<const std::uint8_t> stream, std::size_t size) {
#if defined(O1602_HAVE_ZLIB)
    if (stream.size() > std::numeric_limits<uInt>::max() || size > std::numeric_limits<uInt>::max()) {
        return make_error(ErrorCode::OutOfRange, "zlib: too large");
    }
    std::vector<std::uint8_t> out(size);
    z_stream z{};
    if (inflateInit(&z) != Z_OK) {
        return make_error(ErrorCode::InvalidFormat, "zlib: cannot unpack");
    }
    std::uint8_t none = 0;
    z.next_in = const_cast<Bytef*>(stream.data());  // zlib's API is not const; it does not write
    z.avail_in = static_cast<uInt>(stream.size());
    z.next_out = out.empty() ? &none : out.data();
    z.avail_out = static_cast<uInt>(size);
    int status = Z_OK;
    while (status == Z_OK && z.avail_out > 0) {
        status = inflate(&z, Z_NO_FLUSH);
    }
    const std::size_t got = size - z.avail_out;
    inflateEnd(&z);
    // Z_BUF_ERROR: the output is full, or the input ran out (the head of a longer stream).
    if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
        return make_error(ErrorCode::InvalidFormat, "zlib: damaged stream");
    }
    out.resize(got);
    return out;
#else
    (void)stream;
    (void)size;
    return make_error(ErrorCode::Unsupported, "zlib: this build of Open1602 has no zlib");
#endif
}

namespace {

constexpr std::array<std::uint32_t, 256> make_crc_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
        }
        table[n] = c;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
    std::uint32_t c = 0xFFFFFFFFU;
    for (const std::uint8_t b : data) {
        c = kCrcTable[(c ^ b) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

}  // namespace o1602::core
