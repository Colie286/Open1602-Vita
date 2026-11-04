// SPDX-License-Identifier: GPL-3.0-or-later
// WebM videos with AV1 and Opus (docs/formats/webm.md): the example package's video, made with
// ffmpeg (one second red, one second yellow, 128 x 96 pixels at 10 frames per second, two seconds
// of a stereo tone), and damaged copies of it.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/core/file.hpp"
#include "o1602/video/webm.hpp"

using namespace o1602;
namespace fs = std::filesystem;

namespace {

const fs::path kVideo = fs::path(O1602_EXAMPLE_MODS) / "example" / "videos" / "beacon.webm";

std::vector<std::uint8_t> video_bytes() {
    auto bytes = core::read_file(kVideo);
    REQUIRE(bytes.ok());
    return std::move(*bytes);
}

/// The colour in the middle of an RGBA picture of the video.
std::vector<std::uint8_t> middle(const std::vector<std::uint8_t>& rgba) {
    const std::size_t at = (48 * 128 + 64) * 4;
    return {rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
}

}  // namespace

TEST_CASE("webm: the example video's header, pictures and sound") {
    auto video = video::Webm::open_file(kVideo);
    REQUIRE(video.ok());
    const video::WebmInfo& info = video->info();
    CHECK(info.width == 128);
    CHECK(info.height == 96);
    CHECK(info.frames == 20);
    CHECK(info.frame_us == 100'000);
    REQUIRE(info.tracks.size() == 1);
    CHECK(info.tracks[0].channels == 2);

    std::vector<std::uint8_t> rgba;
    REQUIRE(video->next().ok());
    video->to_rgba(rgba);
    REQUIRE(rgba.size() == std::size_t{128} * 96 * 4);
    const auto red = middle(rgba);
    CHECK(red[0] > 230);
    CHECK(red[1] < 30);
    CHECK(red[2] < 30);
    CHECK(red[3] == 255);
    while (!video->finished()) {
        REQUIRE(video->next().ok());
    }
    CHECK(video->position() == 20);
    CHECK(!video->next().ok());  // past the end
    video->to_rgba(rgba);
    const auto yellow = middle(rgba);
    CHECK(yellow[0] > 230);
    CHECK(yellow[1] > 230);
    CHECK(yellow[2] < 30);

    // Back to the start: red again.
    video->rewind();
    CHECK(video->position() == 0);
    REQUIRE(video->next().ok());
    video->to_rgba(rgba);
    CHECK(middle(rgba)[1] < 30);

    // Two seconds at 48 kHz: the encoder's delay and its padding at the end are cut.
    auto pcm = video->audio(0);
    REQUIRE(pcm.ok());
    CHECK(pcm->rate == 48'000);
    CHECK(pcm->channels == 2);
    CHECK(pcm->frames() == 96'000);
    const auto loudest = std::ranges::max(pcm->samples, {}, [](std::int16_t s) { return s < 0 ? -s : s; });
    CHECK((loudest < 0 ? -loudest : loudest) > 600);  // about 1200: a tone at 3.75 %
    // A track the file does not have is silent.
    auto none = video->audio(1);
    REQUIRE(none.ok());
    CHECK(none->samples.empty());
}

TEST_CASE("webm: a video in memory reads as from its file") {
    auto from_file = video::Webm::open_file(kVideo);
    auto in_memory = video::Webm::open(video_bytes());
    REQUIRE(from_file.ok());
    REQUIRE(in_memory.ok());
    CHECK(in_memory->info().frames == from_file->info().frames);
    std::vector<std::uint8_t> a;
    std::vector<std::uint8_t> b;
    while (!from_file->finished()) {
        REQUIRE(from_file->next().ok());
        REQUIRE(in_memory->next().ok());
        from_file->to_rgba(a);
        in_memory->to_rgba(b);
        CHECK(a == b);
    }
}

TEST_CASE("webm: damaged and foreign files fail without a crash") {
    const std::vector<std::uint8_t> bytes = video_bytes();
    CHECK(!video::Webm::open({}).ok());
    CHECK(!video::Webm::open({0x1A, 0x45, 0xDF, 0xA3}).ok());
    CHECK(!video::Webm::open_file(kVideo.parent_path() / "missing.webm").ok());
    // Another document type.
    std::vector<std::uint8_t> other = bytes;
    const auto doc = std::ranges::search(other, std::vector<std::uint8_t>{'w', 'e', 'b', 'm'});
    REQUIRE(!doc.empty());
    doc[0] = 'x';
    CHECK(!video::Webm::open(std::move(other)).ok());
    // Cut at every length up to a few hundred bytes, then in larger steps: open or decode fails or
    // works on what is there, but never crashes.
    for (std::size_t n = 0; n < bytes.size(); n += n < 400 ? 1 : 97) {
        auto cut =
            video::Webm::open(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)));
        if (!cut.ok()) {
            continue;
        }
        while (!cut->finished() && cut->next().ok()) {
        }
        (void)cut->audio(0);
    }
    // Flipped bytes in the middle of the pictures.
    for (std::size_t at = bytes.size() / 3; at < bytes.size(); at += bytes.size() / 7) {
        std::vector<std::uint8_t> flipped = bytes;
        flipped[at] ^= 0x5A;
        auto video = video::Webm::open(std::move(flipped));
        if (!video.ok()) {
            continue;
        }
        std::vector<std::uint8_t> rgba;
        while (!video->finished() && video->next().ok()) {
            video->to_rgba(rgba);
        }
        (void)video->audio(0);
    }
}
