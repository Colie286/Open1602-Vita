// SPDX-License-Identifier: GPL-3.0-or-later
// Smacker videos (docs/formats/smk.md), with small files built bit by bit, and where the game
// shows them.

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <vector>

#include "../test_framework.hpp"
#include "video_queue.hpp"

#include "o1602/assets/high_scores.hpp"
#include "o1602/core/file.hpp"
#include "o1602/video/smacker.hpp"

using namespace o1602;

namespace {

/// Bits from the lowest bit of each byte up, as the format stores them.
class Bits {
public:
    void bit(std::uint32_t b) {
        if (m_used == 0) {
            m_bytes.push_back(0);
        }
        m_bytes.back() = static_cast<std::uint8_t>(m_bytes.back() | ((b & 1U) << m_used));
        m_used = (m_used + 1) % 8;
    }
    void bits(std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) {
            bit(v >> i);
        }
    }
    /// A byte tree with one leaf (decoding it reads no bits): presence, leaf, closing bit.
    void single_leaf_tree(std::uint8_t value) {
        bit(1);
        bit(0);
        bits(value, 8);
        bit(0);
    }
    /// A byte tree with two leaves: code 0 = a, code 1 = b.
    void two_leaf_tree(std::uint8_t a, std::uint8_t b) {
        bit(1);
        bit(1);
        bit(0);
        bits(a, 8);
        bit(0);
        bits(b, 8);
        bit(0);
    }
    void escapes(std::uint16_t a, std::uint16_t b, std::uint16_t c) {
        bits(a, 16);
        bits(b, 16);
        bits(c, 16);
    }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
    int m_used = 0;
};

void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    for (std::size_t i = 0; i < 4; ++i) {
        v[at + i] = static_cast<std::uint8_t>(x >> (8 * i));
    }
}

struct Frame {
    std::vector<std::uint8_t> palette;             ///< the chunk without its length byte; empty = none
    std::vector<std::vector<std::uint8_t>> audio;  ///< per track 0..; empty = none
    std::vector<std::uint8_t> video;
};

/// A whole file: header, frame table, trees, frames.
std::vector<std::uint8_t> smacker_file(std::uint32_t width, std::uint32_t height, std::int32_t rate,
                                       const std::vector<std::uint8_t>& trees, const std::vector<Frame>& frames,
                                       std::initializer_list<std::uint32_t> tracks = {}) {
    std::vector<std::uint8_t> f(0x68, 0);
    std::memcpy(f.data(), "SMK2", 4);
    put32(f, 4, width);
    put32(f, 8, height);
    put32(f, 12, static_cast<std::uint32_t>(frames.size()));
    put32(f, 16, static_cast<std::uint32_t>(rate));
    put32(f, 52, static_cast<std::uint32_t>(trees.size()));
    for (std::size_t t = 0; t < 4; ++t) {
        put32(f, 56 + t * 4, 4096);
    }
    std::size_t t = 0;
    for (const std::uint32_t a : tracks) {
        put32(f, 72 + 4 * t++, a);
    }
    std::vector<std::vector<std::uint8_t>> data;
    for (const Frame& fr : frames) {
        std::vector<std::uint8_t> d;
        if (!fr.palette.empty()) {
            const std::size_t size = (fr.palette.size() + 1 + 3) / 4 * 4;
            d.push_back(static_cast<std::uint8_t>(size / 4));
            d.insert(d.end(), fr.palette.begin(), fr.palette.end());
            d.resize(size, 0);
        }
        for (const auto& a : fr.audio) {
            if (a.empty()) {
                continue;
            }
            const std::size_t at = d.size();
            d.resize(at + 4);
            put32(d, at, static_cast<std::uint32_t>(a.size() + 4));
            d.insert(d.end(), a.begin(), a.end());
        }
        d.insert(d.end(), fr.video.begin(), fr.video.end());
        d.resize((d.size() + 3) / 4 * 4, 0);
        data.push_back(std::move(d));
    }
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const std::size_t at = f.size();
        f.resize(at + 4);
        put32(f, at, static_cast<std::uint32_t>(data[i].size()) | (i == 0 ? 1U : 0U));
    }
    for (const Frame& fr : frames) {
        std::uint8_t type = fr.palette.empty() ? 0 : 1;
        for (std::size_t k = 0; k < fr.audio.size(); ++k) {
            type = static_cast<std::uint8_t>(type | (fr.audio[k].empty() ? 0U : (2U << k)));
        }
        f.push_back(type);
    }
    f.insert(f.end(), trees.begin(), trees.end());
    for (const auto& d : data) {
        f.insert(f.end(), d.begin(), d.end());
    }
    return f;
}

/// All four trees absent but the block types: one value, read with no bits.
std::vector<std::uint8_t> constant_type_trees(std::uint16_t descriptor) {
    Bits b;
    b.bit(0);  // pixel maps
    b.bit(0);  // mono colours
    b.bit(0);  // full blocks
    b.bit(1);
    b.single_leaf_tree(static_cast<std::uint8_t>(descriptor));
    b.single_leaf_tree(static_cast<std::uint8_t>(descriptor >> 8));
    b.escapes(0xFFF0, 0xFFF1, 0xFFF2);
    b.bit(0);  // the tree: one leaf
    b.bit(0);  // closing bit
    return b.bytes();
}

std::vector<std::uint8_t> new_colour(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return {r, g, b};
}

}  // namespace

TEST_CASE("smacker: header, frame rate, flags and tracks") {
    auto file =
        smacker_file(8, 4, -6666, constant_type_trees(0x0007), {Frame{}, Frame{}}, {0xD000'5622U, 0xC000'5622U});
    auto video = video::Smacker::open(file);
    REQUIRE(video.ok());
    const auto& info = video->info();
    CHECK(info.version == 2);
    CHECK(info.width == 8);
    CHECK(info.height == 4);
    CHECK(info.frames == 2);
    CHECK(info.frame_us == 66660);  // -6666: 1/100 000 s per frame, 15 frames a second
    CHECK(info.tracks[0].present);
    CHECK(info.tracks[0].compressed);
    CHECK(info.tracks[0].stereo);
    CHECK(!(info.tracks[0].bits16));
    CHECK(info.tracks[0].rate == 22050);
    CHECK(!(info.tracks[1].stereo));
    CHECK(!(info.tracks[2].present));

    put32(file, 16, 100);  // positive: milliseconds
    CHECK(video::Smacker::open(file)->info().frame_us == 100000);
    put32(file, 16, 0);  // 0: ten frames a second
    CHECK(video::Smacker::open(file)->info().frame_us == 100000);
    put32(file, 20, 4);  // doubled lines
    auto doubled = video::Smacker::open(file);
    REQUIRE(doubled.ok());
    CHECK(doubled->info().display_height() == 8);
}

TEST_CASE("smacker: damaged files fail instead of crashing") {
    auto file = smacker_file(8, 4, -6666, constant_type_trees(0x0007), {Frame{}});
    CHECK(!(video::Smacker::open({}).ok()));
    auto bad = file;
    bad[0] = 'X';
    CHECK(!(video::Smacker::open(bad).ok()));
    for (std::size_t cut = 0; cut < file.size(); cut += 7) {
        std::vector<std::uint8_t> part(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(cut));
        auto v = video::Smacker::open(part);
        if (v.ok()) {
            (void)v->next();
        }
    }
    auto empty_trees = file;
    put32(empty_trees, 52, 0);  // tables missing: the first read runs out of bits
    CHECK(!(video::Smacker::open(empty_trees).ok()));
}

TEST_CASE("smacker: solid blocks in chains and the palette") {
    // Solid (3), chain index 1 (two blocks), colour 0x2A.
    Frame first;
    first.palette = new_colour(0x3F, 0x00, 0x10);  // entry 0
    first.palette.push_back(0x80 | 40);            // keep 41 entries
    auto c42 = new_colour(0x10, 0x20, 0x30);       // entry 42
    first.palette.insert(first.palette.end(), c42.begin(), c42.end());
    auto file = smacker_file(8, 4, -6666, constant_type_trees(0x2A07), {first, Frame{}});
    auto video = video::Smacker::open(file);
    REQUIRE(video.ok());
    REQUIRE(video->next().ok());
    CHECK(video->palette_changed());
    CHECK(video->stride() == 8);
    for (const std::uint8_t p : video->pixels()) {
        CHECK(p == 0x2A);
    }
    const auto& pal = video->palette();
    CHECK(pal[0] == 255);
    CHECK(pal[1] == 0);
    CHECK(pal[2] == 65);  // 6 bits widened: 4c + c/16
    CHECK(pal[42 * 3 + 0] == 65);
    CHECK(pal[42 * 3 + 1] == 130);
    CHECK(pal[42 * 3 + 2] == 195);
    std::vector<std::uint8_t> rgba;
    video->to_rgba(rgba);
    REQUIRE(rgba.size() == 8 * 4 * 4);
    CHECK(rgba[0] == 65);
    CHECK(rgba[3] == 255);

    REQUIRE(video->next().ok());
    CHECK(!(video->palette_changed()));
    CHECK(video->palette()[0] == 255);
    CHECK(video->finished());
    CHECK(!(video->next().ok()));
    video->rewind();
    CHECK(video->position() == 0);
    CHECK(video->pixels()[0] == 0);
}

TEST_CASE("smacker: palette entries copied from the previous palette") {
    Frame first;
    first.palette = new_colour(0x3F, 0x00, 0x00);
    auto green = new_colour(0x00, 0x3F, 0x00);
    first.palette.insert(first.palette.end(), green.begin(), green.end());
    Frame second;
    // Entry 0 stays, entries 1 and 2 copy old entries 0 and 1 (red, green), entry 3 is new.
    second.palette = {0x80, 0x41, 0x00};
    auto blue = new_colour(0x00, 0x00, 0x3F);
    second.palette.insert(second.palette.end(), blue.begin(), blue.end());
    auto file = smacker_file(4, 4, -6666, constant_type_trees(0x0002), {first, second});
    auto video = video::Smacker::open(file);
    REQUIRE(video.ok());
    REQUIRE(video->next().ok());
    REQUIRE(video->next().ok());
    const auto& pal = video->palette();
    CHECK(pal[0] == 255);
    CHECK(pal[3] == 255);
    CHECK(pal[4] == 0);
    CHECK(pal[6 + 1] == 255);
    CHECK(pal[9 + 2] == 255);
}

TEST_CASE("smacker: mono and full blocks, recent values of the 16-bit trees") {
    Bits t;
    // Pixel maps: one value, 0x8421 (one pixel per line, on the diagonal from top left).
    t.bit(1);
    t.single_leaf_tree(0x21);
    t.single_leaf_tree(0x84);
    t.escapes(0xFFF0, 0xFFF1, 0xFFF2);
    t.bit(0);
    t.bit(0);
    // Mono colours: one value, 0x1234 (1 bits: 0x12, 0 bits: 0x34).
    t.bit(1);
    t.single_leaf_tree(0x34);
    t.single_leaf_tree(0x12);
    t.escapes(0xFFF0, 0xFFF1, 0xFFF2);
    t.bit(0);
    t.bit(0);
    // Full blocks: code 0 = the leaf of marker 0 (stored as 0x0000, holds the most recent value),
    // code 1 = 0xBBAA.
    t.bit(1);
    t.two_leaf_tree(0x00, 0xAA);
    t.two_leaf_tree(0x00, 0xBB);
    t.escapes(0x0000, 0xFFF1, 0xFFF2);
    t.bit(1);  // node
    t.bit(0);  // leaf: low code 0, high code 0 -> 0x0000 = marker 0
    t.bit(0);
    t.bit(0);
    t.bit(0);  // leaf: low code 1, high code 1 -> 0xBBAA
    t.bit(1);
    t.bit(1);
    t.bit(0);  // closing bit
    // Block types: code 0 = mono, one block; code 1 = full, one block.
    t.bit(1);
    t.two_leaf_tree(0x00, 0x01);
    t.bit(0);  // high bytes: absent, 0
    t.escapes(0xFFF0, 0xFFF1, 0xFFF2);
    t.bit(1);
    t.bit(0);
    t.bit(0);
    t.bit(0);
    t.bit(1);
    t.bit(0);

    Bits v;
    v.bit(0);  // block 0: mono (colours and map read no bits)
    v.bit(1);  // block 1: full
    v.bit(1);  // line 0, right pair: 0xBBAA
    v.bit(0);  // line 0, left pair: the recent value, 0xBBAA
    for (int i = 0; i < 6; ++i) {
        v.bit(0);  // the other lines: still 0xBBAA
    }
    Frame frame;
    frame.video = v.bytes();
    auto file = smacker_file(8, 4, -6666, t.bytes(), {frame, frame});
    auto video = video::Smacker::open(file);
    REQUIRE(video.ok());
    REQUIRE(video->next().ok());
    const auto px = video->pixels();
    // Mono block: bit i of the map is pixel i, line by line.
    const std::uint8_t expected_mono[4][4] = {
        {0x12, 0x34, 0x34, 0x34}, {0x34, 0x12, 0x34, 0x34}, {0x34, 0x34, 0x12, 0x34}, {0x34, 0x34, 0x34, 0x12}};
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 4; ++x) {
            CHECK(px[y * 8 + x] == expected_mono[y][x]);
        }
        CHECK(px[y * 8 + 4] == 0xAA);
        CHECK(px[y * 8 + 5] == 0xBB);
        CHECK(px[y * 8 + 6] == 0xAA);
        CHECK(px[y * 8 + 7] == 0xBB);
    }
    // Each frame starts with empty recent values: the marker leaf gives 0 before 0xBBAA comes.
    Bits v2;
    v2.bit(1);
    v2.bit(0);  // marker leaf, nothing recent yet: 0x0000 to the right pair
    v2.bit(1);  // 0xBBAA to the left pair
    for (int i = 0; i < 6; ++i) {
        v2.bit(0);  // recent: 0xBBAA
    }
    v2.bit(0);  // block 1: mono
    Frame f2;
    f2.video = v2.bytes();
    auto file2 = smacker_file(8, 4, -6666, t.bytes(), {frame, f2});
    auto video2 = video::Smacker::open(file2);
    REQUIRE(video2.ok());
    REQUIRE(video2->next().ok());
    REQUIRE(video2->next().ok());
    const auto px2 = video2->pixels();
    CHECK(px2[0] == 0xAA);
    CHECK(px2[1] == 0xBB);
    CHECK(px2[2] == 0x00);
    CHECK(px2[3] == 0x00);
    CHECK(px2[8 + 2] == 0xAA);
    CHECK(px2[4] == 0x12);
}

TEST_CASE("smacker: audio tracks, 8-bit DPCM, 16-bit stereo and plain PCM") {
    Bits a;
    a.bit(1);                     // data
    a.bit(0);                     // mono
    a.bit(0);                     // 8-bit
    a.two_leaf_tree(0x01, 0xFF);  // +1, -1
    a.bits(0x80, 8);              // start value
    a.bit(0);
    a.bit(0);
    a.bit(1);
    std::vector<std::uint8_t> mono = {4, 0, 0, 0};  // unpacked length: 4 bytes
    mono.insert(mono.end(), a.bytes().begin(), a.bytes().end());

    Bits s;
    s.bit(1);
    s.bit(1);  // stereo
    s.bit(1);  // 16-bit
    for (int i = 0; i < 4; ++i) {
        s.bit(0);  // all trees absent: deltas 0
    }
    s.bits(0x12, 8);  // right channel, high byte first
    s.bits(0x34, 8);
    s.bits(0xFF, 8);  // left channel
    s.bits(0xFE, 8);
    std::vector<std::uint8_t> stereo = {8, 0, 0, 0};  // two frames
    stereo.insert(stereo.end(), s.bytes().begin(), s.bytes().end());

    std::vector<std::uint8_t> plain = {0x80, 0xC0, 0x40};

    Frame f;
    f.audio = {mono, stereo, plain};
    auto file =
        smacker_file(4, 4, -6666, constant_type_trees(0x0002), {f, f}, {0xC000'5622U, 0xF000'5622U, 0x4000'5622U});
    auto video = video::Smacker::open(file);
    REQUIRE(video.ok());
    auto t0 = video->audio(0);
    REQUIRE(t0.ok());
    CHECK(t0->channels == 1);
    CHECK(t0->rate == 22050);
    REQUIRE(t0->samples.size() == 8);
    CHECK(t0->samples[0] == 0);  // 0x80
    CHECK(t0->samples[1] == 256);
    CHECK(t0->samples[2] == 512);
    CHECK(t0->samples[3] == 256);
    auto t1 = video->audio(1);
    REQUIRE(t1.ok());
    CHECK(t1->channels == 2);
    REQUIRE(t1->samples.size() == 8);
    CHECK(t1->samples[0] == static_cast<std::int16_t>(0xFFFE));  // left
    CHECK(t1->samples[1] == 0x1234);                             // right
    CHECK(t1->samples[2] == static_cast<std::int16_t>(0xFFFE));
    CHECK(t1->samples[3] == 0x1234);
    auto t2 = video->audio(2);
    REQUIRE(t2.ok());
    REQUIRE(t2->samples.size() == 6);
    CHECK(t2->samples[0] == 0);
    CHECK(t2->samples[1] == 64 * 256);
    CHECK(t2->samples[2] == -64 * 256);
    auto t3 = video->audio(3);
    REQUIRE(t3.ok());
    CHECK(t3->samples.empty());
    CHECK(!(video->audio(7).ok()));
    // The frames still decode with the audio chunks in front.
    REQUIRE(video->next().ok());
}

TEST_CASE("smacker: a few bytes cannot claim more sound than the video lasts (HI-4)") {
    // Mono 8-bit, both trees absent: each chunk decodes 1 MiB of samples from no bits at all.
    Bits a;
    a.bit(1);
    a.bit(0);
    a.bit(0);
    a.bit(0);
    a.bits(0x80, 8);
    std::vector<std::uint8_t> bomb = {0, 0, 0x10, 0};  // unpacked length: 1 MiB
    bomb.insert(bomb.end(), a.bytes().begin(), a.bytes().end());
    Frame f;
    f.audio = {bomb};
    const auto file = smacker_file(4, 4, -6666, constant_type_trees(0x0002), {f, f, f}, {0xC000'5622U});
    auto video = video::Smacker::open(file);
    REQUIRE(video.ok());
    CHECK(!video->audio(0).ok());  // three frames of 1/15 s hold about 26 000 samples, not 3 M
    // A chunk of the length a frame lasts goes through.
    std::vector<std::uint8_t> fits = {0xBE, 0x05, 0, 0};  // 1470: 22050 Hz / 15 frames a second
    fits.insert(fits.end(), a.bytes().begin(), a.bytes().end());
    f.audio = {fits};
    auto ok = video::Smacker::open(smacker_file(4, 4, -6666, constant_type_trees(0x0002), {f, f, f}, {0xC000'5622U}));
    REQUIRE(ok.ok());
    const auto pcm = ok->audio(0);
    REQUIRE(pcm.ok());
    CHECK(pcm->samples.size() == 3 * 1470);
}

TEST_CASE("smacker: a frame rate that wrapped to nothing in 32 bits is refused (HI-12)") {
    auto file = smacker_file(8, 4, -6666, constant_type_trees(0x0007), {Frame{}});
    put32(file, 16, 536870912U);  // * 1000 wrapped to 0
    CHECK(!video::Smacker::open(file).ok());
    put32(file, 16, 0x8000'0000U);  // INT32_MIN
    CHECK(!video::Smacker::open(file).ok());
    put32(file, 16, 1001);  // over a second per frame
    CHECK(!video::Smacker::open(file).ok());
    put32(file, 16, 1000);
    CHECK(video::Smacker::open(file).ok());
}

TEST_CASE("smacker: from a file the frames are read one by one, with the same pictures and sound (FB-14)") {
    namespace fs = std::filesystem;
    Frame first;
    first.palette = new_colour(0x3F, 0x00, 0x10);
    first.audio = {{0x80, 0xC0, 0x40}};
    Frame second;
    second.audio = {{0x40, 0x80}};
    const auto bytes = smacker_file(8, 4, -6666, constant_type_trees(0x2A07), {first, second}, {0x4000'5622U});
    const fs::path dir = test::temp_path("o1602_test_smacker");
    std::error_code ec;
    fs::remove_all(dir, ec);
    REQUIRE(fs::create_directories(dir));
    REQUIRE(core::write_file(dir / "video.smk", bytes).ok());

    auto memory = video::Smacker::open(bytes);
    auto file = video::Smacker::open_file(dir / "video.smk");
    REQUIRE(memory.ok());
    REQUIRE(file.ok());
    CHECK(file->info().frames == 2);
    // Twice through, the second time after rewind() (a looping video).
    for (int pass = 0; pass < 2; ++pass) {
        while (!memory->finished()) {
            REQUIRE(memory->next().ok());
            REQUIRE(file->next().ok());
            CHECK(file->palette() == memory->palette());
            CHECK(std::equal(file->pixels().begin(), file->pixels().end(), memory->pixels().begin(),
                             memory->pixels().end()));
        }
        CHECK(file->finished());
        memory->rewind();
        file->rewind();
    }
    auto sound = file->audio(0);
    REQUIRE(sound.ok());
    CHECK(sound->samples == memory->audio(0)->samples);
    CHECK(sound->samples.size() == 5);

    // Cut short on the disk: refused when opened, as from memory.
    for (std::size_t cut : {std::size_t{0}, std::size_t{0x40}, std::size_t{0x68}, bytes.size() - 1}) {
        const std::vector<std::uint8_t> part(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
        REQUIRE(core::write_file(dir / "short.smk", part).ok());
        CHECK(!video::Smacker::open_file(dir / "short.smk").ok());
        CHECK(!video::Smacker::open(part).ok());
    }
    CHECK(!video::Smacker::open_file(dir / "missing.smk").ok());
    fs::remove_all(dir, ec);
}

TEST_CASE("videos: messages in the minimap, the rest and messages without a minimap on the whole screen") {
    using app::video_plays_in_minimap;
    for (const std::uint16_t message : {std::uint16_t{1}, std::uint16_t{63}, std::uint16_t{1999}}) {
        CHECK(video_plays_in_minimap(message, false, false, true));
        // The main menu (--video N from there) or no panel: no minimap, so not a sound without picture.
        CHECK(!video_plays_in_minimap(message, false, false, false));
    }
    // The credits, the defeats and the missions fulfilled.
    for (const std::uint16_t full :
         {std::uint16_t{10}, std::uint16_t{998}, std::uint16_t{1010}, std::uint16_t{2000}, std::uint16_t{2020},
          std::uint16_t{2080}, std::uint16_t{2120}, std::uint16_t{2500}, std::uint16_t{4000}}) {
        CHECK(!video_plays_in_minimap(full, false, false, true));
    }
    CHECK(!video_plays_in_minimap(6, true, false, true));   // the pause loops
    CHECK(!video_plays_in_minimap(58, false, true, true));  // the intro
    // A package's message videos play as the original's messages do.
    CHECK(video_plays_in_minimap(app::kFirstPackageVideo, false, false, true));
    CHECK(video_plays_in_minimap(app::kFirstPackageVideo + 1023, false, false, true));
}

namespace {

app::VideoRequest queued(std::uint16_t number, bool intro = false) {
    app::VideoRequest r;
    r.number = number;
    r.intro = intro;
    return r;
}

std::vector<std::uint16_t> drain(app::VideoQueue& queue) {
    std::vector<std::uint16_t> out;
    while (const auto r = queue.take_next()) {
        out.push_back(r->number);
    }
    return out;
}

}  // namespace

TEST_CASE("videos: the logos and the intro queue, any other request replaces what waits") {
    app::VideoQueue queue;
    // The start of the original: 8 -> 9 -> 58, none stops the one before.
    CHECK(!queue.request(queued(8, true)));
    CHECK(!queue.request(queued(9, true)));
    CHECK(!queue.request(queued(58, true)));
    CHECK((drain(queue) == std::vector<std::uint16_t>{8, 9, 58}));
    CHECK(!queue.waiting());

    // The latest request wins and stops the running one.
    CHECK(!queue.request(queued(8, true)));
    CHECK(!queue.request(queued(9, true)));
    CHECK(queue.request(queued(63)));
    CHECK(queue.request(queued(998)));
    CHECK((drain(queue) == std::vector<std::uint16_t>{998}));
}

TEST_CASE("videos: the end of the campaigns follows the success video") {
    app::VideoQueue queue;
    CHECK(queue.request(queued(2510)));
    app::VideoRequest end = queued(assets::kCampaignsEndVideo);
    end.follows = true;
    CHECK(!queue.request(end));  // stops nothing
    CHECK((drain(queue) == std::vector<std::uint16_t>{2510, 2990}));
}

TEST_CASE("videos: Escape in a logo skips the whole start, in a message only that video") {
    app::VideoQueue queue;
    for (const std::uint16_t n : {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{58}}) {
        (void)queue.request(queued(n, true));
    }
    queue.started(*queue.take_next(), false);
    queue.skip_start();
    CHECK(!queue.waiting());

    (void)queue.request(queued(63));
    (void)queue.request(queued(58, true));  // the intro from the menu after a message
    queue.started(*queue.take_next(), true);
    queue.skip_start();  // the message is no part of the start
    CHECK((drain(queue) == std::vector<std::uint16_t>{58}));
}

TEST_CASE("videos: where a video plays is decided when it starts") {
    app::VideoQueue queue;
    queue.started(queued(63), true);
    CHECK(queue.now().in_minimap);
    queue.started(queued(63), false);  // the main menu: no minimap
    CHECK(!queue.now().in_minimap);

    app::VideoRequest credits = queued(10);
    credits.in_frame = true;
    queue.started(credits, true);
    CHECK(queue.now().in_frame);
    CHECK(!queue.now().in_minimap);

    app::VideoRequest pause = queued(app::kPauseVideo);
    pause.loop = true;
    pause.tracks = 2;
    queue.started(pause, true);
    CHECK(!queue.now().in_minimap);
    CHECK(queue.now().loop);
    CHECK(queue.now().tracks == 2);
}

TEST_CASE("videos: the pause follows the game") {
    app::PauseVideo pause;
    CHECK(pause.toggle(false));
    CHECK(pause.on());
    // The command has not arrived yet: the game still runs, the pause stays.
    CHECK(!pause.ended_elsewhere(false));
    CHECK(pause.on());
    CHECK(!pause.ended_elsewhere(true));
    // F3 or another player resumed the game: the pause (and its video) ends.
    CHECK(pause.ended_elsewhere(false));
    CHECK(!pause.on());
    CHECK(!pause.ended_elsewhere(false));

    // The key in a game paused another way resumes it and shows no pause video.
    CHECK(!pause.toggle(true));
    CHECK(pause.toggle(false));
    CHECK(!pause.toggle(true));  // pressed again: off
    pause.reset();
    CHECK(!pause.on());
}
