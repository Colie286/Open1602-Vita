// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/video/webm.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dav1d/dav1d.h>
#include <fstream>
#include <limits>
#include <optional>
#include <opus_multistream.h>
#include <span>
#include <string>
#include <thread>
#include <utility>

namespace o1602::video {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::uint32_t kMaxDimension = 4096;
constexpr std::uint32_t kMaxFrames = 1'000'000;
constexpr std::size_t kMaxTracks = 8;
/// Sound of a whole video, in samples, as for Smacker and Bink (HI-4).
constexpr std::uint64_t kMaxSamples = std::uint64_t{64} << 20;
/// Values the parser reads whole: strings, CodecPrivate.
constexpr std::uint64_t kMaxValue = 1 << 20;
/// The longest Opus packet, 120 ms at 48 kHz.
constexpr int kMaxOpusFrame = 5760;
constexpr std::uint32_t kOpusRate = 48'000;

// Element ids (Matroska, with their length marker).
constexpr std::uint32_t kEbml = 0x1A45DFA3;
constexpr std::uint32_t kDocType = 0x4282;
constexpr std::uint32_t kSegment = 0x18538067;
constexpr std::uint32_t kInfo = 0x1549A966;
constexpr std::uint32_t kTimestampScale = 0x2AD7B1;
constexpr std::uint32_t kTracks = 0x1654AE6B;
constexpr std::uint32_t kTrackEntry = 0xAE;
constexpr std::uint32_t kTrackNumber = 0xD7;
constexpr std::uint32_t kTrackType = 0x83;
constexpr std::uint32_t kCodecId = 0x86;
constexpr std::uint32_t kCodecPrivate = 0x63A2;
constexpr std::uint32_t kDefaultDuration = 0x23E383;
constexpr std::uint32_t kVideo = 0xE0;
constexpr std::uint32_t kPixelWidth = 0xB0;
constexpr std::uint32_t kPixelHeight = 0xBA;
constexpr std::uint32_t kContentEncodings = 0x6D80;
constexpr std::uint32_t kCluster = 0x1F43B675;
constexpr std::uint32_t kTimestamp = 0xE7;
constexpr std::uint32_t kSimpleBlock = 0xA3;
constexpr std::uint32_t kBlockGroup = 0xA0;
constexpr std::uint32_t kBlock = 0xA1;
constexpr std::uint32_t kDiscardPadding = 0x75A2;

constexpr std::uint64_t kUnknownSize = std::numeric_limits<std::uint64_t>::max();

core::Error damaged(const std::string& what) {
    return make_error(ErrorCode::InvalidFormat, "WebM: " + what);
}

/// The top-level elements a cluster of unknown size ends at.
bool ends_cluster(std::uint32_t id) noexcept {
    return id == kCluster || id == 0x1C53BB6B /* Cues */ || id == 0x1254C367 /* Tags */ ||
           id == 0x1941A469 /* Attachments */ || id == 0x1043A770 /* Chapters */ || id == kSegment;
}

}  // namespace

/// The bytes of the file, in memory or read from it as needed.
struct Webm::Source {
    std::vector<std::uint8_t> bytes;  ///< open(): the whole file
    std::ifstream file;               ///< open_file()
    std::uint64_t size = 0;

    /// `n` bytes from `at`; false past the end or on a read error.
    bool read(std::uint64_t at, std::size_t n, std::vector<std::uint8_t>& out) {
        if (at > size || n > size - at) {
            return false;
        }
        out.resize(n);
        if (n == 0) {
            return true;
        }
        if (!bytes.empty() || !file.is_open()) {
            std::memcpy(out.data(), bytes.data() + at, n);
            return true;
        }
        file.clear();
        file.seekg(static_cast<std::streamoff>(at));
        return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n)));
    }
};

struct Webm::Packets {
    std::vector<std::uint8_t> head;  ///< CodecPrivate: the OpusHead
    std::vector<Frame> packets;
    std::vector<std::uint32_t> discard;  ///< per packet: samples at its end that are padding
};

/// dav1d with the picture it gave last.
struct Webm::Picture {
    Dav1dContext* decoder = nullptr;
    Dav1dPicture picture{};
    bool has_picture = false;
    std::vector<std::uint8_t> buffer;  ///< the frame read last

    Picture() = default;
    Picture(const Picture&) = delete;
    Picture& operator=(const Picture&) = delete;
    ~Picture() {
        drop();
        if (decoder != nullptr) {
            dav1d_close(&decoder);
        }
    }
    void drop() {
        if (has_picture) {
            dav1d_picture_unref(&picture);
            has_picture = false;
        }
    }
};

namespace {

/// Reads EBML elements of a Source.
class Reader {
public:
    explicit Reader(Webm::Source& source) : m_source(source) {}

    struct Element {
        std::uint32_t id = 0;
        std::uint64_t data = 0;  ///< where its value starts
        std::uint64_t size = 0;  ///< kUnknownSize: up to its parent's end or the next of its level
        [[nodiscard]] std::uint64_t end(std::uint64_t parent_end) const noexcept {
            return size == kUnknownSize ? parent_end : data + size;
        }
    };

    /// The element header at `at`, or nothing past `end` or on damaged data.
    std::optional<Element> element(std::uint64_t at, std::uint64_t end) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(12, end > at ? end - at : 0));
        if (n < 2 || !m_source.read(at, n, m_buffer)) {
            return std::nullopt;
        }
        std::size_t pos = 0;
        const auto id = vint(pos, true);
        const auto size = vint(pos, false);
        if (!id || !size || *id > 0xFFFFFFFFU) {
            return std::nullopt;
        }
        Element e{static_cast<std::uint32_t>(*id), at + pos, *size};
        if (e.size != kUnknownSize && (e.data > end || e.size > end - e.data)) {
            return std::nullopt;
        }
        return e;
    }

    std::optional<std::uint64_t> uint(const Element& e) {
        if (e.size > 8 || !m_source.read(e.data, static_cast<std::size_t>(e.size), m_buffer)) {
            return std::nullopt;
        }
        std::uint64_t v = 0;
        for (const std::uint8_t b : m_buffer) {
            v = (v << 8) | b;
        }
        return v;
    }

    std::optional<std::vector<std::uint8_t>> bytes(const Element& e) {
        if (e.size > kMaxValue || !m_source.read(e.data, static_cast<std::size_t>(e.size), m_buffer)) {
            return std::nullopt;
        }
        return m_buffer;
    }

    std::optional<std::string> string(const Element& e) {
        auto b = bytes(e);
        if (!b) {
            return std::nullopt;
        }
        std::string s(b->begin(), b->end());
        s.erase(std::find(s.begin(), s.end(), '\0'), s.end());
        return s;
    }

    /// The first bytes of a block: the track and the flags, and how many bytes they took.
    struct BlockHead {
        std::uint64_t track = 0;
        std::uint8_t flags = 0;
        std::size_t length = 0;
    };
    std::optional<BlockHead> block_head(const Element& e) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(e.size, 11));
        if (n < 4 || !m_source.read(e.data, n, m_buffer)) {
            return std::nullopt;
        }
        std::size_t pos = 0;
        const auto track = vint(pos, false);
        if (!track || pos + 3 > n) {
            return std::nullopt;
        }
        return BlockHead{*track, m_buffer[pos + 2], pos + 3};
    }

private:
    /// A variable-length integer at `pos` of the buffer; ids keep their length marker.
    std::optional<std::uint64_t> vint(std::size_t& pos, bool keep_marker) {
        if (pos >= m_buffer.size()) {
            return std::nullopt;
        }
        const std::uint8_t first = m_buffer[pos];
        int length = 1;
        while (length <= 8 && (first & (0x80U >> (length - 1))) == 0) {
            ++length;
        }
        if (length > 8 || pos + static_cast<std::size_t>(length) > m_buffer.size()) {
            return std::nullopt;
        }
        std::uint64_t value = keep_marker ? first : (first & (0xFFU >> length));
        bool all_ones = (first & (0xFFU >> length)) == (0xFFU >> length);
        for (int i = 1; i < length; ++i) {
            const std::uint8_t b = m_buffer[pos + static_cast<std::size_t>(i)];
            value = (value << 8) | b;
            all_ones = all_ones && b == 0xFF;
        }
        pos += static_cast<std::size_t>(length);
        if (!keep_marker && all_ones) {
            return kUnknownSize;
        }
        return value;
    }

    Webm::Source& m_source;
    std::vector<std::uint8_t> m_buffer;
};

struct TrackEntry {
    std::uint64_t number = 0;
    std::uint64_t type = 0;
    std::string codec;
    std::vector<std::uint8_t> priv;
    std::uint64_t default_duration_ns = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    bool encoded = false;  ///< compressed or encrypted blocks
};

/// Signed 8.8 dB of the OpusHead's output gain as a factor.
double opus_gain(std::span<const std::uint8_t> head) {
    const auto q8 = static_cast<std::int16_t>(head[16] | (head[17] << 8));
    return std::pow(10.0, q8 / (20.0 * 256.0));
}

std::uint8_t clamp_byte(std::int32_t v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

}  // namespace

Webm::Webm() : m_source(std::make_unique<Source>()), m_picture(std::make_unique<Picture>()) {}
Webm::Webm(Webm&&) noexcept = default;
Webm& Webm::operator=(Webm&&) noexcept = default;
Webm::~Webm() = default;

core::Result<Webm> Webm::open(std::vector<std::uint8_t> bytes) {
    Webm w;
    w.m_source->size = bytes.size();
    w.m_source->bytes = std::move(bytes);
    if (auto st = w.parse(); !st) {
        return st.error();
    }
    return w;
}

core::Result<Webm> Webm::open_file(const std::filesystem::path& path) {
    Webm w;
    w.m_source->file.open(path, std::ios::binary);
    if (!w.m_source->file) {
        return make_error(ErrorCode::NotFound, "WebM: cannot open " + path.filename().string());
    }
    w.m_source->file.seekg(0, std::ios::end);
    const auto end = w.m_source->file.tellg();
    if (end < 0) {
        return make_error(ErrorCode::IoError, "WebM: cannot determine the file size");
    }
    w.m_source->size = static_cast<std::uint64_t>(end);
    if (auto st = w.parse(); !st) {
        return st.error();
    }
    return w;
}

core::Status Webm::parse() {
    Reader r(*m_source);
    const std::uint64_t file_end = m_source->size;
    // The EBML header names the document type.
    const auto header = r.element(0, file_end);
    if (!header || header->id != kEbml || header->size == kUnknownSize) {
        return damaged("not an EBML file");
    }
    std::string doc_type;
    for (std::uint64_t at = header->data; at < header->end(file_end);) {
        const auto e = r.element(at, header->end(file_end));
        if (!e || e->size == kUnknownSize) {
            return damaged("damaged EBML header");
        }
        if (e->id == kDocType) {
            doc_type = r.string(*e).value_or("");
        }
        at = e->end(file_end);
    }
    if (doc_type != "webm" && doc_type != "matroska") {
        return make_error(ErrorCode::Unsupported, "WebM: document type '" + doc_type + "' is not WebM");
    }
    std::optional<Reader::Element> segment;
    for (std::uint64_t at = header->end(file_end); at < file_end;) {
        const auto e = r.element(at, file_end);
        if (!e) {
            break;
        }
        if (e->id == kSegment) {
            segment = e;
            break;
        }
        if (e->size == kUnknownSize) {
            break;
        }
        at = e->end(file_end);
    }
    if (!segment) {
        return damaged("no segment");
    }

    std::uint64_t scale_ns = 1'000'000;
    std::vector<TrackEntry> entries;
    std::optional<TrackEntry> video;
    std::vector<std::uint64_t> stamps;  ///< of the pictures, in units of scale_ns

    const auto read_track = [&](const Reader::Element& entry) -> core::Status {
        TrackEntry t;
        const std::uint64_t end = entry.end(file_end);
        for (std::uint64_t at = entry.data; at < end;) {
            const auto e = r.element(at, end);
            if (!e || e->size == kUnknownSize) {
                return damaged("damaged track entry");
            }
            switch (e->id) {
                case kTrackNumber:
                    t.number = r.uint(*e).value_or(0);
                    break;
                case kTrackType:
                    t.type = r.uint(*e).value_or(0);
                    break;
                case kCodecId:
                    t.codec = r.string(*e).value_or("");
                    break;
                case kCodecPrivate:
                    t.priv = r.bytes(*e).value_or(std::vector<std::uint8_t>{});
                    break;
                case kDefaultDuration:
                    t.default_duration_ns = r.uint(*e).value_or(0);
                    break;
                case kContentEncodings:
                    t.encoded = true;
                    break;
                case kVideo:
                    for (std::uint64_t v = e->data; v < e->end(end);) {
                        const auto c = r.element(v, e->end(end));
                        if (!c || c->size == kUnknownSize) {
                            return damaged("damaged video settings");
                        }
                        if (c->id == kPixelWidth) {
                            t.width = r.uint(*c).value_or(0);
                        } else if (c->id == kPixelHeight) {
                            t.height = r.uint(*c).value_or(0);
                        }
                        v = c->end(end);
                    }
                    break;
                default:
                    break;
            }
            at = e->end(end);
        }
        entries.push_back(std::move(t));
        return core::Ok{};
    };

    const auto choose_tracks = [&]() -> core::Status {
        for (const TrackEntry& t : entries) {
            if (t.type == 1 && !video) {
                if (t.codec != "V_AV1") {
                    return make_error(ErrorCode::Unsupported, "WebM: the picture is " + t.codec + ", not AV1");
                }
                if (t.encoded) {
                    return make_error(ErrorCode::Unsupported, "WebM: compressed or encrypted pictures");
                }
                if (t.width == 0 || t.height == 0 || t.width > kMaxDimension || t.height > kMaxDimension) {
                    return make_error(ErrorCode::LimitExceeded,
                                      "WebM: the picture must be 1 to 4096 pixels wide and high");
                }
                video = t;
            } else if (t.type == 2 && t.codec == "A_OPUS" && !t.encoded && m_info.tracks.size() < kMaxTracks) {
                if (t.priv.size() < 19 || std::memcmp(t.priv.data(), "OpusHead", 8) != 0 || t.priv[9] == 0) {
                    return damaged("damaged Opus header");
                }
                m_info.tracks.push_back(WebmTrack{t.number, t.priv[9]});
                Packets p;
                p.head = t.priv;
                m_packets.push_back(std::move(p));
            }
        }
        if (!video) {
            return make_error(ErrorCode::Unsupported, "WebM: no picture track");
        }
        m_info.width = static_cast<std::uint32_t>(video->width);
        m_info.height = static_cast<std::uint32_t>(video->height);
        return core::Ok{};
    };

    // A block of a cluster: the picture's place, or a sound packet.
    const auto add_block = [&](const Reader::Element& e, std::int64_t cluster_time,
                               std::uint32_t discard) -> core::Status {
        const auto head = r.block_head(e);
        if (!head) {
            return damaged("damaged block");
        }
        const std::uint8_t lacing = (head->flags >> 1) & 3U;
        const Frame frame{e.data + head->length, static_cast<std::uint32_t>(e.size - head->length)};
        if (e.size - head->length > std::numeric_limits<std::uint32_t>::max()) {
            return damaged("block too large");
        }
        if (head->track == video->number) {
            if (lacing != 0) {
                return make_error(ErrorCode::Unsupported, "WebM: laced pictures");
            }
            if (m_frames.size() >= kMaxFrames) {
                return make_error(ErrorCode::LimitExceeded, "WebM: more than 1,000,000 frames");
            }
            std::vector<std::uint8_t> rel;
            if (!m_source->read(e.data + head->length - 3, 2, rel)) {
                return damaged("damaged block");
            }
            const auto offset = static_cast<std::int16_t>((rel[0] << 8) | rel[1]);
            stamps.push_back(static_cast<std::uint64_t>(std::max<std::int64_t>(0, cluster_time + offset)));
            m_frames.push_back(frame);
            return core::Ok{};
        }
        for (std::size_t i = 0; i < m_info.tracks.size(); ++i) {
            if (m_info.tracks[i].number == head->track) {
                if (lacing != 0) {
                    return make_error(ErrorCode::Unsupported, "WebM: laced sound packets");
                }
                m_packets[i].packets.push_back(frame);
                m_packets[i].discard.push_back(discard);
            }
        }
        return core::Ok{};
    };

    const std::uint64_t segment_end = segment->end(file_end);
    bool tracks_read = false;
    for (std::uint64_t at = segment->data; at < segment_end;) {
        const auto e = r.element(at, segment_end);
        if (!e) {
            break;  // a file cut short keeps the frames before
        }
        if (e->id == kInfo) {
            for (std::uint64_t i = e->data; i < e->end(segment_end);) {
                const auto c = r.element(i, e->end(segment_end));
                if (!c || c->size == kUnknownSize) {
                    return damaged("damaged segment information");
                }
                if (c->id == kTimestampScale) {
                    scale_ns = std::max<std::uint64_t>(1, r.uint(*c).value_or(1'000'000));
                }
                i = c->end(segment_end);
            }
        } else if (e->id == kTracks) {
            for (std::uint64_t i = e->data; i < e->end(segment_end);) {
                const auto c = r.element(i, e->end(segment_end));
                if (!c || c->size == kUnknownSize) {
                    return damaged("damaged track list");
                }
                if (c->id == kTrackEntry) {
                    if (auto st = read_track(*c); !st) {
                        return st;
                    }
                }
                i = c->end(segment_end);
            }
            if (auto st = choose_tracks(); !st) {
                return st;
            }
            tracks_read = true;
        } else if (e->id == kCluster) {
            if (!tracks_read) {
                return damaged("a cluster before the track list");
            }
            std::int64_t time = 0;
            const std::uint64_t end = e->end(segment_end);
            std::uint64_t i = e->data;
            while (i < end) {
                const auto c = r.element(i, end);
                if (!c) {
                    break;
                }
                if (e->size == kUnknownSize && ends_cluster(c->id)) {
                    break;  // the next cluster: this one of unknown size ends here
                }
                if (c->size == kUnknownSize) {
                    return damaged("an element of unknown size in a cluster");
                }
                if (c->id == kTimestamp) {
                    time = static_cast<std::int64_t>(r.uint(*c).value_or(0));
                } else if (c->id == kSimpleBlock) {
                    if (auto st = add_block(*c, time, 0); !st) {
                        return st;
                    }
                } else if (c->id == kBlockGroup) {
                    // The block, and the padding at the end of the last sound packet (in ns).
                    std::optional<Reader::Element> block;
                    std::uint32_t discard = 0;
                    for (std::uint64_t g = c->data; g < c->end(end);) {
                        const auto b = r.element(g, c->end(end));
                        if (!b || b->size == kUnknownSize) {
                            return damaged("damaged block group");
                        }
                        if (b->id == kBlock) {
                            block = b;
                        } else if (b->id == kDiscardPadding && b->size >= 1 && b->size <= 8) {
                            // A signed number of nanoseconds; only a positive one cuts samples.
                            const std::uint64_t ns = r.uint(*b).value_or(0);
                            if ((ns >> (b->size * 8 - 1)) == 0) {
                                discard = static_cast<std::uint32_t>(
                                    std::min<std::uint64_t>(ns / 1000 * kOpusRate / 1'000'000, kMaxOpusFrame));
                            }
                        }
                        g = b->end(end);
                    }
                    if (block) {
                        if (auto st = add_block(*block, time, discard); !st) {
                            return st;
                        }
                    }
                }
                i = c->end(end);
            }
            at = i;
            continue;
        }
        if (e->size == kUnknownSize) {
            return damaged("an element of unknown size in the segment");
        }
        at = e->end(segment_end);
    }
    if (!tracks_read) {
        return damaged("no track list");
    }
    if (m_frames.empty()) {
        return damaged("no pictures");
    }
    m_info.frames = static_cast<std::uint32_t>(m_frames.size());
    // A constant frame rate: the track's frame duration, else the mean distance of the pictures.
    if (video->default_duration_ns != 0) {
        m_info.frame_us = video->default_duration_ns / 1000;
    } else if (stamps.size() > 1) {
        const auto [lo, hi] = std::minmax_element(stamps.begin(), stamps.end());
        m_info.frame_us = (*hi - *lo) * scale_ns / 1000 / (stamps.size() - 1);
    }
    if (m_info.frame_us == 0) {
        m_info.frame_us = 40'000;  // a single picture: 25 frames per second
    }

    Dav1dSettings settings;
    dav1d_default_settings(&settings);
    // One frame in, one picture out: the player asks for them one by one.
    settings.max_frame_delay = 1;
#ifdef O1602_EXPERIMENTAL_VITA
    // The player already owns a decode worker; keep dav1d within that worker's budget.
    settings.n_threads = 1;
#else
    settings.n_threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1U, 4U));
#endif
    settings.frame_size_limit = kMaxDimension * kMaxDimension;
    if (dav1d_open(&m_picture->decoder, &settings) != 0) {
        return make_error(ErrorCode::PlatformError, "WebM: the AV1 decoder could not be started");
    }
    return core::Ok{};
}

core::Status Webm::next() {
    if (finished()) {
        return make_error(ErrorCode::OutOfRange, "WebM: no more frames");
    }
    Picture& p = *m_picture;
    const Frame& f = m_frames[m_position];
    if (!m_source->read(f.offset, f.size, p.buffer)) {
        return damaged("cannot read frame " + std::to_string(m_position));
    }
    ++m_position;
    Dav1dData data{};
    std::uint8_t* bytes = dav1d_data_create(&data, p.buffer.size());
    if (bytes == nullptr) {
        return make_error(ErrorCode::PlatformError, "WebM: out of memory");
    }
    std::memcpy(bytes, p.buffer.data(), p.buffer.size());
    bool got = false;
    // With a frame delay of one, a picture comes after each frame; the bound guards damaged data.
    for (int round = 0; round < 64 && (data.sz > 0 || !got); ++round) {
        if (data.sz > 0) {
            const int sent = dav1d_send_data(p.decoder, &data);
            if (sent < 0 && sent != DAV1D_ERR(EAGAIN)) {
                dav1d_data_unref(&data);
                return damaged("damaged picture in frame " + std::to_string(m_position - 1));
            }
        }
        Dav1dPicture picture{};
        const int res = dav1d_get_picture(p.decoder, &picture);
        if (res == 0) {
            if (picture.p.bpc != 8) {
                dav1d_picture_unref(&picture);
                dav1d_data_unref(&data);
                return make_error(ErrorCode::Unsupported, "WebM: only pictures of 8 bits per component");
            }
            p.drop();
            p.picture = picture;
            p.has_picture = true;
            got = true;
        } else if (res != DAV1D_ERR(EAGAIN)) {
            dav1d_data_unref(&data);
            return damaged("damaged picture in frame " + std::to_string(m_position - 1));
        } else if (data.sz == 0) {
            break;  // the frame holds no picture to show (the last one stays)
        }
    }
    dav1d_data_unref(&data);
    return core::Ok{};
}

void Webm::rewind() {
    if (m_picture->decoder != nullptr) {
        dav1d_flush(m_picture->decoder);
    }
    m_position = 0;
}

void Webm::to_rgba(std::vector<std::uint8_t>& out) const {
    const std::size_t w = m_info.width;
    const std::size_t h = m_info.height;
    out.assign(w * h * 4, 0);
    for (std::size_t i = 3; i < out.size(); i += 4) {
        out[i] = 255;
    }
    const Picture& p = *m_picture;
    if (!p.has_picture) {
        return;
    }
    const Dav1dPicture& pic = p.picture;
    const std::size_t pw = std::min<std::size_t>(w, static_cast<std::size_t>(pic.p.w));
    const std::size_t ph = std::min<std::size_t>(h, static_cast<std::size_t>(pic.p.h));
    const bool mono = pic.p.layout == DAV1D_PIXEL_LAYOUT_I400;
    const int sx = pic.p.layout == DAV1D_PIXEL_LAYOUT_I420 || pic.p.layout == DAV1D_PIXEL_LAYOUT_I422 ? 1 : 0;
    const int sy = pic.p.layout == DAV1D_PIXEL_LAYOUT_I420 ? 1 : 0;
    const bool full = pic.seq_hdr != nullptr && pic.seq_hdr->color_range != 0;
    Dav1dMatrixCoefficients matrix = pic.seq_hdr != nullptr ? pic.seq_hdr->mtrx : DAV1D_MC_UNKNOWN;
    if (matrix != DAV1D_MC_BT709 && matrix != DAV1D_MC_BT601 && matrix != DAV1D_MC_BT470BG &&
        matrix != DAV1D_MC_BT2020_NCL) {
        matrix = h >= 720 ? DAV1D_MC_BT709 : DAV1D_MC_BT601;
    }
    // R = Y + rv V, G = Y - gu U - gv V, B = Y + bu U, in 1/65536, for full range; video range
    // stretches them by 255/224 (chroma) and Y by 255/219.
    double kr = 0.299;
    double kb = 0.114;
    if (matrix == DAV1D_MC_BT709) {
        kr = 0.2126;
        kb = 0.0722;
    } else if (matrix == DAV1D_MC_BT2020_NCL) {
        kr = 0.2627;
        kb = 0.0593;
    }
    const double kg = 1.0 - kr - kb;
    const double cs = full ? 1.0 : 255.0 / 224.0;
    const double ys = full ? 1.0 : 255.0 / 219.0;
    const auto fix = [](double v) { return static_cast<std::int32_t>(std::lround(v * 65536.0)); };
    const std::int32_t y_mul = fix(ys);
    const std::int32_t rv = fix(2.0 * (1.0 - kr) * cs);
    const std::int32_t bu = fix(2.0 * (1.0 - kb) * cs);
    const std::int32_t gu = fix(2.0 * (1.0 - kb) * kb / kg * cs);
    const std::int32_t gv = fix(2.0 * (1.0 - kr) * kr / kg * cs);
    const std::int32_t y_off = full ? 0 : 16;
    const auto* py = static_cast<const std::uint8_t*>(pic.data[0]);
    const auto* pu = static_cast<const std::uint8_t*>(pic.data[1]);
    const auto* pv = static_cast<const std::uint8_t*>(pic.data[2]);
    const std::ptrdiff_t ys_stride = pic.stride[0];
    const std::ptrdiff_t uv_stride = pic.stride[1];
    for (std::size_t y = 0; y < ph; ++y) {
        const std::uint8_t* row_y = py + static_cast<std::ptrdiff_t>(y) * ys_stride;
        const std::uint8_t* row_u = mono ? nullptr : pu + static_cast<std::ptrdiff_t>(y >> sy) * uv_stride;
        const std::uint8_t* row_v = mono ? nullptr : pv + static_cast<std::ptrdiff_t>(y >> sy) * uv_stride;
        std::uint8_t* o = out.data() + y * w * 4;
        for (std::size_t x = 0; x < pw; ++x, o += 4) {
            const std::int32_t lum = (static_cast<std::int32_t>(row_y[x]) - y_off) * y_mul;
            if (mono) {
                o[0] = o[1] = o[2] = clamp_byte((lum + 32768) >> 16);
                continue;
            }
            const std::int32_t u = static_cast<std::int32_t>(row_u[x >> sx]) - 128;
            const std::int32_t v = static_cast<std::int32_t>(row_v[x >> sx]) - 128;
            o[0] = clamp_byte((lum + rv * v + 32768) >> 16);
            o[1] = clamp_byte((lum - gu * u - gv * v + 32768) >> 16);
            o[2] = clamp_byte((lum + bu * u + 32768) >> 16);
        }
    }
}

core::Result<audio::Pcm> Webm::audio(std::size_t track) const {
    audio::Pcm pcm;
    pcm.rate = kOpusRate;
    if (track >= m_packets.size()) {
        return pcm;
    }
    const Packets& p = m_packets[track];
    const std::span<const std::uint8_t> head(p.head);
    const int channels = head[9];
    const auto pre_skip = static_cast<std::uint32_t>(head[10] | (head[11] << 8));
    const std::uint8_t family = head[18];
    int streams = 1;
    int coupled = channels > 1 ? 1 : 0;
    std::array<unsigned char, 255> mapping{0, 1};
    if (family != 0) {
        if (head.size() < 21 + static_cast<std::size_t>(channels)) {
            return damaged("damaged Opus channel mapping");
        }
        streams = head[19];
        coupled = head[20];
        std::copy_n(head.begin() + 21, channels, mapping.begin());
    } else if (channels > 2) {
        return damaged("Opus without a channel mapping for more than two channels");
    }
    int error = 0;
    OpusMSDecoder* decoder = opus_multistream_decoder_create(static_cast<opus_int32>(kOpusRate), channels, streams,
                                                             coupled, mapping.data(), &error);
    if (decoder == nullptr || error != OPUS_OK) {
        return damaged("the Opus header is not usable");
    }
    const double gain = opus_gain(head);
    const auto out_channels = static_cast<std::uint8_t>(std::min(channels, 2));
    pcm.channels = out_channels;
    std::vector<float> buffer(static_cast<std::size_t>(kMaxOpusFrame) * static_cast<std::size_t>(channels));
    std::vector<std::uint8_t> packet;
    std::uint64_t skip = pre_skip;
    for (std::size_t k = 0; k < p.packets.size(); ++k) {
        const Frame& f = p.packets[k];
        if (!m_source->read(f.offset, f.size, packet)) {
            opus_multistream_decoder_destroy(decoder);
            return damaged("cannot read a sound packet");
        }
        const int n = opus_multistream_decode_float(decoder, packet.data(), static_cast<opus_int32>(packet.size()),
                                                    buffer.data(), kMaxOpusFrame, 0);
        if (n < 0) {
            opus_multistream_decoder_destroy(decoder);
            return damaged("damaged sound packet");
        }
        const int keep = n - static_cast<int>(std::min<std::uint32_t>(p.discard[k], static_cast<std::uint32_t>(n)));
        for (int i = 0; i < keep; ++i) {
            if (skip != 0) {
                --skip;
                continue;
            }
            if (pcm.samples.size() >= kMaxSamples) {
                opus_multistream_decoder_destroy(decoder);
                return make_error(ErrorCode::LimitExceeded, "WebM: the sound is too long");
            }
            for (int c = 0; c < out_channels; ++c) {
                const double s =
                    static_cast<double>(buffer[static_cast<std::size_t>(i * channels + c)]) * gain * 32767.0;
                pcm.samples.push_back(static_cast<std::int16_t>(std::clamp(std::lround(s), -32768L, 32767L)));
            }
        }
    }
    opus_multistream_decoder_destroy(decoder);
    return pcm;
}

}  // namespace o1602::video
