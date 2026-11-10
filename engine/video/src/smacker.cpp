// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/video/smacker.hpp"
#include "o1602/core/profile.hpp"
#include <atomic>
#include <chrono>

#include <algorithm>
#include <cstring>
#include <string>
#include <stdexcept>
#include <utility>

namespace o1602::video {

namespace detail {

/// Bits from the lowest bit of each byte up. Reading past the end gives zeros and marks the
/// reader as overrun (the caller then fails instead of reading out of bounds).
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> data) noexcept : m_data(data) {}

    std::uint32_t bit() noexcept {
        const std::size_t byte = m_pos >> 3;
        if (byte >= m_data.size()) {
            m_overrun = true;
            return 0;
        }
        const std::uint32_t v = (static_cast<std::uint32_t>(m_data[byte]) >> (m_pos & 7U)) & 1U;
        ++m_pos;
        return v;
    }
    std::uint32_t bits(int n) noexcept {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            v |= bit() << i;
        }
        return v;
    }
    [[nodiscard]] bool overrun() const noexcept { return m_overrun; }

private:
    std::span<const std::uint8_t> m_data;
    std::size_t m_pos = 0;
    bool m_overrun = false;
};

}  // namespace detail

using core::ErrorCode;
using core::make_error;

using detail::BitReader;

namespace {

constexpr std::size_t kHeaderSize = 0x68;
constexpr std::uint32_t kNode = 0x8000'0000U;
/// Deeper trees than this are damaged data (keeps the recursion bounded).
constexpr int kMaxDepth = 256;
constexpr std::uint32_t kMaxDimension = 4096;
constexpr std::uint32_t kMaxFrames = 1'000'000;
/// Longest frame accepted: one second.
constexpr std::uint64_t kMaxFrameUs = 1'000'000;
/// Sound of a whole video, in samples (HI-4): 128 MiB; the original's longest video has well
/// under 10 M. Within that, no more than the frames last plus a second.
constexpr std::uint64_t kMaxSamples = std::uint64_t{64} << 20;

enum TreeIndex : std::size_t { kMMap = 0, kMClr = 1, kFull = 2, kType = 3 };

/// Chain lengths of the block type descriptors (bits 2..7).
constexpr std::array<std::uint16_t, 64> kChain = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,  17,  18,  19,   20,   21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,  39,  40,  41,   42,   43, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048,
};

/// 6-bit palette components to 8 bits (the format's table is 4c + c/16).
constexpr std::uint8_t widen6(std::uint8_t v) noexcept {
    const auto c = static_cast<std::uint32_t>(v & 0x3FU);
    return static_cast<std::uint8_t>(c * 4 + c / 16);
}

std::uint32_t load_u32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

/// Huffman tree of bytes: leaf = value, node = kNode | index of the 1 branch (the 0 branch
/// follows the node). An absent tree is one leaf of value 0, decoding it reads no bits.
struct ByteTree {
    std::vector<std::uint32_t> nodes{0};

    [[nodiscard]] std::uint32_t decode(BitReader& in) const noexcept {
        std::size_t i = 0;
        while ((nodes[i] & kNode) != 0) {
            i = in.bit() != 0 ? (nodes[i] & ~kNode) : i + 1;
        }
        return nodes[i];
    }
};

bool read_byte_tree(BitReader& in, ByteTree& tree, int depth) {
    if (depth > kMaxDepth || in.overrun() || tree.nodes.size() > 4096) {
        return false;
    }
    if (in.bit() == 0) {
        tree.nodes.push_back(in.bits(8));
        return true;
    }
    const std::size_t node = tree.nodes.size();
    tree.nodes.push_back(kNode);
    if (!read_byte_tree(in, tree, depth + 1)) {
        return false;
    }
    tree.nodes[node] = kNode | static_cast<std::uint32_t>(tree.nodes.size());
    return read_byte_tree(in, tree, depth + 1);
}

/// A byte tree as stored: presence bit, the tree, a closing 0 bit.
bool read_stored_byte_tree(BitReader& in, ByteTree& tree) {
    tree.nodes = {0};
    if (in.bit() == 0) {
        return !in.overrun();
    }
    tree.nodes.clear();
    if (!read_byte_tree(in, tree, 0)) {
        return false;
    }
    in.bit();
    return !in.overrun();
}

}  // namespace

void Smacker::BigTree::reset() noexcept {
    for (const std::size_t r : recent) {
        nodes[r] = 0;
    }
}

namespace {

struct BigTreeReader {
    BitReader& in;
    const ByteTree& low;
    const ByteTree& high;
    std::array<std::uint32_t, 3> escapes{};
    std::array<bool, 3> found{};
    std::size_t limit = 0;

    bool read(std::vector<std::uint32_t>& nodes, std::array<std::size_t, 3>& recent, int depth) {
        if (depth > kMaxDepth || in.overrun() || nodes.size() > limit) {
            return false;
        }
        if (in.bit() == 0) {
            std::uint32_t v = low.decode(in) | (high.decode(in) << 8);
            for (std::size_t e = 0; e < 3; ++e) {
                if (v == escapes[e]) {
                    // The leaf of a marker holds a recent value, filled in while decoding.
                    if (!found[e]) {
                        found[e] = true;
                        recent[e] = nodes.size();
                    }
                    v = 0;
                    break;
                }
            }
            nodes.push_back(v);
            return true;
        }
        const std::size_t node = nodes.size();
        nodes.push_back(kNode);
        if (!read(nodes, recent, depth + 1)) {
            return false;
        }
        nodes[node] = kNode | static_cast<std::uint32_t>(nodes.size());
        return read(nodes, recent, depth + 1);
    }
};

/// A 16-bit tree as stored: presence bit, low and high byte trees, three markers, the tree, a
/// closing 0 bit. Markers without a leaf get unreachable slots at the end.
bool read_big_tree(BitReader& in, std::uint32_t size, std::vector<std::uint32_t>& nodes,
                   std::array<std::size_t, 3>& recent) {
    nodes.clear();
    std::array<bool, 3> found{};
    if (in.bit() != 0) {
        ByteTree low;
        ByteTree high;
        if (!read_stored_byte_tree(in, low) || !read_stored_byte_tree(in, high)) {
            return false;
        }
        BigTreeReader reader{in, low, high, {}, {}, 0};
        for (auto& e : reader.escapes) {
            e = in.bits(16);
        }
        // The header's size is in bytes of the original's table (4 per entry); allow some slack.
        reader.limit = std::max<std::size_t>(size / 4 + 16, 16);
        if (!reader.read(nodes, recent, 0)) {
            return false;
        }
        found = reader.found;
        in.bit();
    } else {
        nodes.push_back(0);
    }
    for (std::size_t e = 0; e < 3; ++e) {
        if (!found[e]) {
            recent[e] = nodes.size();
            nodes.push_back(0);
        }
    }
    return !in.overrun();
}

}  // namespace

std::uint32_t Smacker::BigTree::decode(BitReader& in) noexcept {
    std::size_t i = 0;
    while ((nodes[i] & kNode) != 0) {
        i = in.bit() != 0 ? (nodes[i] & ~kNode) : i + 1;
    }
    const std::uint32_t v = nodes[i];
    // A new value moves the recent ones along: the marker leaves always hold the last three.
    if (v != nodes[recent[0]]) {
        nodes[recent[2]] = nodes[recent[1]];
        nodes[recent[1]] = nodes[recent[0]];
        nodes[recent[0]] = v;
    }
    return v;
}

core::Result<Smacker> Smacker::open(std::vector<std::uint8_t> bytes) {
    Smacker s;
    s.m_bytes = std::move(bytes);
    if (auto st = s.parse(s.m_bytes, s.m_bytes.size()); !st) {
        return st.error();
    }
    return s;
}

namespace {
constexpr std::uint64_t resident_budget=8*1024*1024;
std::atomic<std::uint64_t> resident_movies{0};
struct ResidentMovie:std::vector<std::uint8_t>{
    explicit ResidentMovie(std::size_t size):std::vector<std::uint8_t>(size){}
    ~ResidentMovie(){core::profile::gauge(core::profile::VideoCompressedBytes,resident_movies.fetch_sub(size())-size());}
};
std::shared_ptr<ResidentMovie> reserve_movie(std::size_t size){
    auto used=resident_movies.load();
    do{if(size>resident_budget-used)return {};}
    while(!resident_movies.compare_exchange_weak(used,used+size));
    try{auto result=std::make_shared<ResidentMovie>(size);core::profile::gauge(core::profile::VideoCompressedBytes,used+size);return result;}
    catch(...){resident_movies.fetch_sub(size);return {};}
}
}
core::Result<Smacker> Smacker::open_shared(std::shared_ptr<const std::vector<std::uint8_t>> bytes){
    if(!bytes)return make_error(ErrorCode::InvalidFormat,"Smacker: missing shared bytes");
    Smacker s;s.m_shared=std::move(bytes);
    if(auto status=s.parse(*s.m_shared,s.m_shared->size());!status)return status.error();
    return s;
}
core::Result<Smacker> Smacker::open_file(const std::filesystem::path& path,std::size_t resident_limit) {
    Smacker s;
    s.m_path=path;
    s.m_file.open(path, std::ios::binary);
    if (!s.m_file) {
        return make_error(ErrorCode::NotFound, "Smacker: cannot open " + path.filename().string());
    }
    s.m_file.seekg(0, std::ios::end);
    const auto end = s.m_file.tellg();
    s.m_file.seekg(0, std::ios::beg);
    if (end < 0) {
        return make_error(ErrorCode::IoError, "Smacker: cannot determine the file size");
    }
    const auto size = static_cast<std::uint64_t>(end);
    // Small event movies share one compressed buffer across picture and audio
    // readers. No repeated memory-card reads of the same interleaved packets.
    if(size&&size<=resident_limit&&size<=resident_budget)if(auto bytes=reserve_movie(static_cast<std::size_t>(size))){
        // Uniquely owned during initialization, shared const after publication.
        if(!s.m_file.read(reinterpret_cast<char*>(bytes->data()),static_cast<std::streamsize>(size)))
            return make_error(ErrorCode::IoError,"Smacker: cannot read buffered movie");
        return open_shared(std::move(bytes));
    }
    // First the header, then as much as its frame table and trees need (no more than the file has).
    std::vector<std::uint8_t> head(static_cast<std::size_t>(std::min<std::uint64_t>(size, kHeaderSize)));
    auto read = [&](std::size_t from) {
        return s.m_file
            .read(reinterpret_cast<char*>(head.data() + from), static_cast<std::streamsize>(head.size() - from))
            .good();
    };
    if (!read(0)) {
        return make_error(ErrorCode::IoError, "Smacker: cannot read the header");
    }
    if (head.size() == kHeaderSize) {
        const std::span<const std::uint8_t> h = head;
        const std::uint32_t frames = std::min(load_u32(h, 12), kMaxFrames + 1U);
        const std::uint64_t physical = std::uint64_t{frames} + ((load_u32(h, 20) & 1U) != 0 ? 1U : 0U);
        const std::uint64_t wanted = std::min(size, kHeaderSize + physical * 5 + load_u32(h, 52));
        head.resize(static_cast<std::size_t>(wanted));
        if (!read(kHeaderSize)) {
            return make_error(ErrorCode::IoError, "Smacker: cannot read the frame table");
        }
    }
    if (auto st = s.parse(head, size); !st) {
        return st.error();
    }
    return s;
}

core::Status Smacker::parse(std::span<const std::uint8_t> b, std::uint64_t size) {
    if (b.size() < kHeaderSize) {
        return make_error(ErrorCode::InvalidFormat, "Smacker: file too short");
    }
    Smacker& s = *this;
    if (std::memcmp(b.data(), "SMK2", 4) == 0) {
        s.m_info.version = 2;
    } else if (std::memcmp(b.data(), "SMK4", 4) == 0) {
        s.m_info.version = 4;
    } else {
        return make_error(ErrorCode::InvalidFormat, "Smacker: no SMK2/SMK4 signature");
    }
    auto& info = s.m_info;
    info.width = load_u32(b, 4);
    info.height = load_u32(b, 8);
    info.frames = load_u32(b, 12);
    const auto rate = static_cast<std::int32_t>(load_u32(b, 16));
    const std::uint32_t flags = load_u32(b, 20);
    if (info.width == 0 || info.height == 0 || info.width > kMaxDimension || info.height > kMaxDimension ||
        info.frames == 0 || info.frames > kMaxFrames) {
        return make_error(ErrorCode::InvalidFormat, "Smacker: implausible size or frame count");
    }
    // Positive: milliseconds per frame; negative: 1/100 000 s per frame; 0: 10 frames a second.
    // In 64 bits (HI-12): a crafted rate wrapped to 0 in 32 bits.
    std::uint64_t frame_us = 100'000;
    if (rate > 0) {
        frame_us = static_cast<std::uint64_t>(rate) * 1000U;
    } else if (rate < 0) {
        frame_us = static_cast<std::uint64_t>(-static_cast<std::int64_t>(rate)) * 10U;
    }
    if (frame_us == 0 || frame_us > kMaxFrameUs) {
        return make_error(ErrorCode::InvalidFormat, "Smacker: implausible frame rate");
    }
    info.frame_us = static_cast<std::uint32_t>(frame_us);
    info.ring_frame = (flags & 1U) != 0;
    info.interlaced = (flags & 2U) != 0;
    info.doubled = (flags & 4U) != 0;
    for (std::size_t t = 0; t < kSmackerTracks; ++t) {
        const std::uint32_t a = load_u32(b, 72 + t * 4);
        auto& track = info.tracks[t];
        track.compressed = (a & 0x8000'0000U) != 0;
        track.present = (a & 0x4000'0000U) != 0;
        track.bits16 = (a & 0x2000'0000U) != 0;
        track.stereo = (a & 0x1000'0000U) != 0;
        track.rate = a & 0x00FF'FFFFU;
        if ((a & 0x0C00'0000U) != 0 && track.present) {
            // Bink audio inside Smacker (newer files) is not described publicly.
            return make_error(ErrorCode::Unsupported,
                              "Smacker: audio track " + std::to_string(t) + " uses an unknown compression");
        }
    }
    const std::uint32_t trees_size = load_u32(b, 52);
    const std::array<std::uint32_t, 4> tree_sizes{load_u32(b, 56), load_u32(b, 60), load_u32(b, 64), load_u32(b, 68)};

    const std::size_t physical = std::size_t{info.frames} + (info.ring_frame ? 1U : 0U);
    std::size_t at = kHeaderSize;
    if (b.size() < at + physical * 5 + trees_size) {
        return make_error(ErrorCode::InvalidFormat, "Smacker: file too short for its frame table");
    }
    s.m_sizes.resize(physical);
    s.m_types.resize(physical);
    for (std::size_t f = 0; f < physical; ++f) {
        s.m_sizes[f] = load_u32(b, at + f * 4) & ~3U;  // bit 0: key frame, bit 1: unknown
    }
    at += physical * 4;
    std::copy_n(b.begin() + static_cast<std::ptrdiff_t>(at), physical, s.m_types.begin());
    at += physical;

    BitReader trees(b.subspan(at, trees_size));
    for (std::size_t t = 0; t < 4; ++t) {
        if (!read_big_tree(trees, tree_sizes[t], s.m_trees[t].nodes, s.m_trees[t].recent)) {
            return make_error(ErrorCode::InvalidFormat, "Smacker: damaged Huffman table " + std::to_string(t));
        }
    }
    at += trees_size;

    s.m_offsets.resize(physical);
    std::uint64_t offset = at;
    for (std::size_t f = 0; f < physical; ++f) {
        s.m_offsets[f] = offset;
        offset += s.m_sizes[f];
        if (offset > size) {
            return make_error(ErrorCode::InvalidFormat, "Smacker: frame " + std::to_string(f) + " past the end");
        }
    }

    s.m_stride = (info.width + 3) & ~3U;
    s.m_lines = (info.height + 3) & ~3U;
    s.m_frame.assign(std::size_t{s.m_stride} * s.m_lines, 0);
    return core::Ok{};
}

core::Result<std::span<const std::uint8_t>> Smacker::frame_bytes(std::uint32_t frame) const {
    if (!m_file.is_open()) {
        return (m_shared?std::span<const std::uint8_t>(*m_shared):std::span<const std::uint8_t>(m_bytes)).subspan(static_cast<std::size_t>(m_offsets[frame]),
                                                              m_sizes[frame]);
    }
    m_chunk.resize(m_sizes[frame]);
    m_file.clear();
    // Sequential packets are already at this position. Seeking to the same
    // offset discards the stream's read-ahead buffer on Vita for every frame.
    if ((m_file_offset!=m_offsets[frame]&&!m_file.seekg(static_cast<std::streamoff>(m_offsets[frame]))) ||
        !m_file.read(reinterpret_cast<char*>(m_chunk.data()), static_cast<std::streamsize>(m_chunk.size()))) {
        m_file_offset=~std::uint64_t{0};
        return make_error(ErrorCode::IoError, "Smacker: cannot read frame " + std::to_string(frame));
    }
    m_file_offset=m_offsets[frame]+m_chunk.size();
    return std::span<const std::uint8_t>(m_chunk);
}

void Smacker::rewind() {
    m_position = 0;
    std::fill(m_frame.begin(), m_frame.end(), std::uint8_t{0});
    m_palette.fill(0);
    m_palette_changed = false;
}

core::Status Smacker::next() {
    if (finished()) {
        return make_error(ErrorCode::OutOfRange, "Smacker: no more frames");
    }
    const bool measure=core::profile::enabled();
    const auto read_begin=measure?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const auto read = frame_bytes(m_position);
    const auto decode_begin=measure?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    if(measure)m_timing.read_us=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(decode_begin-read_begin).count());
    if (!read) {
        return read.error();
    }
    const std::span<const std::uint8_t> data = *read;
    const std::uint8_t type = m_types[m_position];
    std::size_t at = 0;
    m_palette_changed = false;
    if ((type & 1U) != 0) {
        if (data.empty()) {
            return make_error(ErrorCode::InvalidFormat, "Smacker: palette chunk missing");
        }
        const std::size_t size = std::size_t{data[0]} * 4;
        if (size == 0 || size > data.size()) {
            return make_error(ErrorCode::InvalidFormat, "Smacker: palette chunk past the frame");
        }
        if (auto st = decode_palette(data.subspan(1, size - 1)); !st) {
            return st;
        }
        at += size;
    }
    for (std::size_t t = 0; t < kSmackerTracks; ++t) {
        if ((type & (2U << t)) == 0) {
            continue;
        }
        if (data.size() < at + 4) {
            return make_error(ErrorCode::InvalidFormat, "Smacker: audio chunk past the frame");
        }
        const std::uint32_t size = load_u32(data, at);
        if (size < 4 || size > data.size() - at) {
            return make_error(ErrorCode::InvalidFormat, "Smacker: audio chunk past the frame");
        }
        at += size;
    }
    if (auto st = decode_video(data.subspan(at)); !st) {
        return st;
    }
    ++m_position;
    if(measure)m_timing.decode_us=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-decode_begin).count());
    return core::Ok{};
}

core::Status Smacker::decode_palette(std::span<const std::uint8_t> chunk) {
    // The chunk is padded to whole dwords; an incomplete command at its end is padding.
    const std::array<std::uint8_t, 768> old = m_palette;
    std::size_t i = 0;
    std::size_t entry = 0;
    while (entry < 256 && i < chunk.size()) {
        const std::uint8_t c = chunk[i++];
        if ((c & 0x80U) != 0) {
            // Keep the next entries as they were.
            entry += std::size_t{c & 0x7FU} + 1;
        } else if ((c & 0x40U) != 0) {
            // Copy entries of the old palette from another place.
            if (i >= chunk.size()) {
                break;  // padding at the end of the chunk
            }
            const std::size_t count = std::size_t{c & 0x3FU} + 1;
            std::size_t from = chunk[i++];
            for (std::size_t k = 0; k < count && entry < 256 && from < 256; ++k) {
                std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(from * 3), 3,
                            m_palette.begin() + static_cast<std::ptrdiff_t>(entry * 3));
                ++entry;
                ++from;
            }
        } else {
            // A new colour, three 6-bit components.
            if (i + 2 > chunk.size()) {
                break;
            }
            m_palette[entry * 3 + 0] = widen6(c);
            m_palette[entry * 3 + 1] = widen6(chunk[i]);
            m_palette[entry * 3 + 2] = widen6(chunk[i + 1]);
            i += 2;
            ++entry;
        }
    }
    m_palette_changed = true;
    return core::Ok{};
}

core::Status Smacker::decode_video(std::span<const std::uint8_t> chunk) {
    for (auto& t : m_trees) {
        t.reset();
    }
    BitReader in(chunk);
    const std::uint32_t columns = m_stride / 4;
    const std::size_t blocks = std::size_t{columns} * (m_lines / 4);
    const std::size_t stride = m_stride;
    std::size_t block = 0;
    while (block < blocks) {
        const std::uint32_t descriptor = m_trees[kType].decode(in);
        const std::uint32_t kind = descriptor & 3U;
        std::size_t run = kChain[(descriptor >> 2) & 0x3FU];
        const auto extra = static_cast<std::uint8_t>(descriptor >> 8);

        // SMK4 full blocks start with the bits of a sub-mode: 0 = plain, 10 = doubled, 11 = half.
        int full_mode = 0;
        if (kind == 1 && m_info.version == 4) {
            if (in.bit() != 0) {
                full_mode = in.bit() != 0 ? 2 : 1;
            }
        }
        for (; run > 0 && block < blocks; --run, ++block) {
            const std::size_t bx = (block % columns) * 4;
            const std::size_t by = (block / columns) * 4;
            std::uint8_t* out = m_frame.data() + by * stride + bx;
            switch (kind) {
                case 0: {  // two colours and a map of which pixel has which
                    const std::uint32_t colours = m_trees[kMClr].decode(in);
                    std::uint32_t map = m_trees[kMMap].decode(in);
                    const auto one = static_cast<std::uint8_t>(colours >> 8);
                    const auto zero = static_cast<std::uint8_t>(colours);
                    for (std::size_t y = 0; y < 4; ++y) {
                        for (std::size_t x = 0; x < 4; ++x) {
                            out[y * stride + x] = (map & 1U) != 0 ? one : zero;
                            map >>= 1;
                        }
                    }
                    break;
                }
                case 1:  // every pixel
                    if (full_mode == 1) {
                        // Doubled: each value paints two 2x2 squares.
                        for (std::size_t y = 0; y < 4; y += 2) {
                            const std::uint32_t v = m_trees[kFull].decode(in);
                            const auto left = static_cast<std::uint8_t>(v);
                            const auto right = static_cast<std::uint8_t>(v >> 8);
                            for (std::size_t dy = 0; dy < 2; ++dy) {
                                std::uint8_t* row = out + (y + dy) * stride;
                                row[0] = row[1] = left;
                                row[2] = row[3] = right;
                            }
                        }
                    } else {
                        // Plain: two values per line, the right pair first. Half: every second line,
                        // the line below repeats it.
                        const std::size_t step = full_mode == 2 ? 2 : 1;
                        for (std::size_t y = 0; y < 4; y += step) {
                            std::uint8_t* row = out + y * stride;
                            std::uint32_t v = m_trees[kFull].decode(in);
                            row[2] = static_cast<std::uint8_t>(v);
                            row[3] = static_cast<std::uint8_t>(v >> 8);
                            v = m_trees[kFull].decode(in);
                            row[0] = static_cast<std::uint8_t>(v);
                            row[1] = static_cast<std::uint8_t>(v >> 8);
                            if (step == 2) {
                                std::copy_n(row, 4, row + stride);
                            }
                        }
                    }
                    break;
                case 2:  // unchanged
                    break;
                default:  // one colour
                    for (std::size_t y = 0; y < 4; ++y) {
                        std::fill_n(out + y * stride, 4, extra);
                    }
                    break;
            }
        }
        if (in.overrun()) {
            return make_error(ErrorCode::InvalidFormat,
                              "Smacker: frame " + std::to_string(m_position) + " is cut short");
        }
    }
    return core::Ok{};
}

void Smacker::to_rgba(std::vector<std::uint8_t>& out) const {
    const std::size_t w = m_info.width;
    const std::size_t h = m_info.display_height();
    out.assign(w * h * 4, 0);
    for (std::size_t y = 0; y < h; ++y) {
        std::size_t src_y = y;
        if (m_info.doubled || m_info.interlaced) {
            if (m_info.interlaced && (y & 1U) != 0) {
                for (std::size_t x = 0; x < w; ++x) {
                    out[(y * w + x) * 4 + 3] = 255;
                }
                continue;
            }
            src_y = y / 2;
        }
        const std::uint8_t* src = m_frame.data() + src_y * m_stride;
        std::uint8_t* dst = out.data() + y * w * 4;
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t c = std::size_t{src[x]} * 3;
            dst[x * 4 + 0] = m_palette[c];
            dst[x * 4 + 1] = m_palette[c + 1];
            dst[x * 4 + 2] = m_palette[c + 2];
            dst[x * 4 + 3] = 255;
        }
    }
}

namespace {

/// Appends one compressed audio chunk (after the length field) to `out` (16-bit samples).
core::Status decode_dpcm(std::span<const std::uint8_t> data, const SmackerTrack& track,
                         std::vector<std::int16_t>& out) {
    if (data.size() < 4) {
        return make_error(ErrorCode::InvalidFormat, "Smacker: audio chunk too short");
    }
    const std::uint32_t unpacked = load_u32(data, 0);
    // One frame of sound: a second of 16-bit stereo at 48 kHz is 192 000 bytes.
    constexpr std::uint32_t kMaxUnpacked = 1U << 20;
    if (unpacked > kMaxUnpacked) {
        return make_error(ErrorCode::InvalidFormat, "Smacker: audio chunk too large");
    }
    BitReader in(data.subspan(4));
    if (in.bit() == 0) {
        return core::Ok{};  // no data in this chunk
    }
    const bool stereo = in.bit() != 0;
    const bool bits16 = in.bit() != 0;
    if (stereo != track.stereo || bits16 != track.bits16) {
        return make_error(ErrorCode::InvalidFormat, "Smacker: audio chunk does not match its track");
    }
    const std::size_t channels = stereo ? 2 : 1;
    std::array<ByteTree, 4> trees;
    for (std::size_t t = 0; t < channels * (bits16 ? 2U : 1U); ++t) {
        if (!read_stored_byte_tree(in, trees[t])) {
            return make_error(ErrorCode::InvalidFormat, "Smacker: damaged audio tree");
        }
    }
    const std::size_t values = unpacked / (bits16 ? 2U : 1U);
    if (values < channels) {
        return core::Ok{};
    }
    // No reserve per chunk: an exact reserve on every frame would copy the whole track each time.
    if (bits16) {
        // Start values: right channel first, high byte first.
        std::array<std::uint16_t, 2> pred{};
        for (std::size_t c = channels; c-- > 0;) {
            const std::uint32_t hi = in.bits(8);
            const std::uint32_t lo = in.bits(8);
            pred[c] = static_cast<std::uint16_t>((hi << 8) | lo);
        }
        for (std::size_t c = 0; c < channels; ++c) {
            out.push_back(static_cast<std::int16_t>(pred[c]));
        }
        for (std::size_t i = channels; i < values && !in.overrun(); ++i) {
            const std::size_t c = i % channels;
            const std::uint32_t lo = trees[c * 2].decode(in);
            const std::uint32_t hi = trees[c * 2 + 1].decode(in);
            pred[c] = static_cast<std::uint16_t>(pred[c] + ((hi << 8) | lo));
            out.push_back(static_cast<std::int16_t>(pred[c]));
        }
    } else {
        std::array<std::uint8_t, 2> pred{};
        for (std::size_t c = channels; c-- > 0;) {
            pred[c] = static_cast<std::uint8_t>(in.bits(8));
        }
        const auto widen = [](std::uint8_t v) { return static_cast<std::int16_t>((static_cast<int>(v) - 128) * 256); };
        for (std::size_t c = 0; c < channels; ++c) {
            out.push_back(widen(pred[c]));
        }
        for (std::size_t i = channels; i < values && !in.overrun(); ++i) {
            const std::size_t c = i % channels;
            pred[c] = static_cast<std::uint8_t>(pred[c] + trees[c].decode(in));
            out.push_back(widen(pred[c]));
        }
    }
    if (in.overrun()) {
        return make_error(ErrorCode::InvalidFormat, "Smacker: audio chunk cut short");
    }
    return core::Ok{};
}

void append_pcm(std::span<const std::uint8_t> data, const SmackerTrack& track, std::vector<std::int16_t>& out) {
    if (track.bits16) {
        for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
            out.push_back(static_cast<std::int16_t>(data[i] | (data[i + 1] << 8)));
        }
    } else {
        for (const std::uint8_t v : data) {
            out.push_back(static_cast<std::int16_t>((static_cast<int>(v) - 128) * 256));
        }
    }
}

// Read by the audio prefetch worker alone, independently of the picture decoder.
class SmackerSamples final: public audio::SampleSource {
    using Read=std::function<core::Result<std::vector<std::int16_t>>(std::uint32_t)>;
    Read m_read;
    std::size_t m_frames;
    std::uint32_t m_packets;
    std::uint8_t m_channels;
    mutable std::uint32_t m_next=0;
    mutable std::size_t m_first=0;
    mutable std::vector<std::int16_t> m_samples;
public:
    SmackerSamples(Read read,std::size_t frames,std::uint32_t packets,std::uint8_t channels)
        :m_read(std::move(read)),m_frames(frames),m_packets(packets),m_channels(channels){}
    std::size_t frames() const noexcept override {return m_frames;}
    std::size_t buffered_bytes() const noexcept override {return m_samples.capacity()*sizeof(std::int16_t);}
    std::int16_t sample(std::size_t frame,std::uint8_t channel) const override {
        if(frame>=m_frames||channel>=m_channels)return 0;
        if(frame<m_first){m_next=0;m_first=0;m_samples.clear();}
        while(frame>=m_first+m_samples.size()/m_channels){
            m_first+=m_samples.size()/m_channels;m_samples.clear();
            if(m_next>=m_packets)return 0;
            auto chunk=m_read(m_next++);
            if(!chunk)throw std::runtime_error(chunk.error().message);
            if(chunk->size()/m_channels>m_frames-m_first)throw std::runtime_error("Smacker: more sound than the video lasts");
            m_samples=std::move(*chunk);
        }
        return m_samples[(frame-m_first)*m_channels+channel];
    }
};

}  // namespace

core::Result<audio::Pcm> Smacker::stream_audio(std::size_t track) const {
    if(track>=kSmackerTracks)return make_error(ErrorCode::OutOfRange,"Smacker: no audio track");
    if(m_path.empty()&&!m_shared)return audio(track);
    const auto t=m_info.tracks[track];audio::Pcm pcm;pcm.rate=t.rate;pcm.channels=t.stereo?2:1;
    if(!t.present)return pcm;
    auto opened=m_shared?open_shared(m_shared):open_file(m_path);if(!opened)return opened.error();
    auto reader=std::make_shared<Smacker>(std::move(*opened));
    const auto limit=std::min(kMaxSamples,(std::uint64_t(m_info.frames)*m_info.frame_us/1000+1000)*t.rate*pcm.channels/1000);
    auto read=[reader,track,t](std::uint32_t frame)->core::Result<std::vector<std::int16_t>> {
        auto bytes=reader->frame_bytes(frame);if(!bytes)return bytes.error();
        const auto data=*bytes;const auto type=reader->m_types[frame];std::size_t at=0;
        if((type&1U)&&!data.empty())at=std::size_t(data[0])*4;
        std::vector<std::int16_t> samples;
        for(std::size_t k=0;k<kSmackerTracks;++k){
            if(!(type&(2U<<k)))continue;
            if(at>data.size()||data.size()-at<4)return make_error(ErrorCode::InvalidFormat,"Smacker: audio chunk past the frame");
            const auto size=load_u32(data,at);
            if(size<4||size>data.size()-at)return make_error(ErrorCode::InvalidFormat,"Smacker: audio chunk past the frame");
            if(k==track){
                const auto chunk=data.subspan(at+4,size-4);
                if(chunk.size()>(1U<<20))return make_error(ErrorCode::InvalidFormat,"Smacker: audio packet too large");
                if(t.compressed){if(auto st=decode_dpcm(chunk,t,samples);!st)return st.error();}
                else append_pcm(chunk,t,samples);
                if(samples.size()%(t.stereo?2:1))return make_error(ErrorCode::InvalidFormat,"Smacker: incomplete stereo audio packet");
            }
            at+=size;
        }
        return samples;
    };
    pcm.source=std::make_shared<SmackerSamples>(std::move(read),std::size_t(limit/pcm.channels),m_info.frames,pcm.channels);
    return pcm;
}

core::Result<audio::Pcm> Smacker::audio(std::size_t track, const std::function<bool(std::uint32_t,std::uint32_t)>& progress) const {
    if (track >= kSmackerTracks) {
        return make_error(ErrorCode::OutOfRange, "Smacker: no audio track " + std::to_string(track));
    }
    const SmackerTrack& t = m_info.tracks[track];
    audio::Pcm pcm;
    pcm.rate = t.rate;
    pcm.channels = t.stereo ? 2 : 1;
    if (!t.present) {
        return pcm;
    }
    // What the frames can hold (HI-4): absent Huffman trees read no bits, so a chunk of a few bytes
    // may claim 1 MiB of sound; a total cap keeps a small file from asking for gigabytes.
    const std::uint64_t channels = t.stereo ? 2U : 1U;
    const std::uint64_t limit = std::min(
        kMaxSamples, (std::uint64_t{m_info.frames} * m_info.frame_us / 1000U + 1000U) * t.rate * channels / 1000U);
    for (std::uint32_t f = 0; f < m_info.frames; ++f) {
        if(progress&&!progress(f,m_info.frames))return make_error(ErrorCode::Unsupported,"video audio cancelled");
        const auto read = frame_bytes(f);
        if (!read) {
            return read.error();
        }
        const std::span<const std::uint8_t> data = *read;
        const std::uint8_t type = m_types[f];
        std::size_t at = 0;
        if ((type & 1U) != 0 && !data.empty()) {
            at += std::size_t{data[0]} * 4;
        }
        for (std::size_t k = 0; k < kSmackerTracks; ++k) {
            if ((type & (2U << k)) == 0) {
                continue;
            }
            if (at + 4 > data.size()) {
                return make_error(ErrorCode::InvalidFormat, "Smacker: audio chunk past the frame");
            }
            const std::uint32_t size = load_u32(data, at);
            if (size < 4 || size > data.size() - at) {
                return make_error(ErrorCode::InvalidFormat, "Smacker: audio chunk past the frame");
            }
            if (k == track) {
                const auto chunk = data.subspan(at + 4, size - 4);
                const std::uint64_t claimed =
                    t.compressed ? (chunk.size() >= 4 ? load_u32(chunk, 0) : 0U) : std::uint64_t{chunk.size()};
                if (pcm.samples.size() + claimed / (t.bits16 ? 2U : 1U) > limit) {
                    return make_error(ErrorCode::InvalidFormat, "Smacker: more sound than the video lasts");
                }
                if (t.compressed) {
                    if (auto st = decode_dpcm(chunk, t, pcm.samples); !st) {
                        return st.error();
                    }
                } else {
                    append_pcm(chunk, t, pcm.samples);
                }
            }
            at += size;
        }
    }
    pcm.samples.resize(pcm.samples.size() - pcm.samples.size() % pcm.channels);
    return pcm;
}

}  // namespace o1602::video
