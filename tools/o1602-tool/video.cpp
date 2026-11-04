// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool: Smacker, Bink 2 and WebM videos (header, frames as PNG, a sound track as WAVE).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "tool.hpp"

#include "o1602/core/file.hpp"
#include "o1602/core/png_writer.hpp"
#include "o1602/video/bink.hpp"
#include "o1602/video/smacker.hpp"
#include "o1602/video/webm.hpp"

namespace o1602::tool {

namespace {

/// 16-bit PCM as a RIFF WAVE file.
std::vector<std::uint8_t> wave_bytes(const audio::Pcm& pcm) {
    std::vector<std::uint8_t> out;
    const auto put32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    const auto put16 = [&](std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto data = static_cast<std::uint32_t>(pcm.samples.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put32(36 + data);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(16);
    put16(1);
    put16(pcm.channels);
    put32(pcm.rate);
    put32(pcm.rate * pcm.channels * 2U);
    put16(pcm.channels * 2U);
    put16(16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(data);
    for (const std::int16_t v : pcm.samples) {
        put16(static_cast<std::uint16_t>(v));
    }
    return out;
}

/// Decodes every frame of a Bink 2 or WebM video, writes frames as PNG (--png DIR, --every N) and a
/// sound track as WAVE (--wav N, the second positional argument), and says how long it took.
template <typename Video>
int decode(Video& video, std::uint32_t width, std::uint32_t height, std::uint32_t frames, const Args& args,
           std::optional<std::size_t> wav_track) {
    const auto png_dir = args.options.find("--png");
    const std::size_t every = std::max<std::size_t>(args.number("--every").value_or(1), 1);
    std::vector<std::uint8_t> rgba;
    double slowest_ms = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!video.finished()) {
        const std::uint32_t frame = video.position();
        const auto before = std::chrono::steady_clock::now();
        if (auto st = video.next(); !st) {
            return fail(st.error());
        }
        slowest_ms = std::max(
            slowest_ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count());
        if (png_dir != args.options.end() && frame % every == 0) {
            video.to_rgba(rgba);
            char name[32];
            std::snprintf(name, sizeof name, "frame%04u.png", frame);
            if (auto st = core::write_png_rgba(fs::path(png_dir->second) / name, width, height, rgba); !st) {
                return fail(st.error());
            }
        }
    }
    const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("decoded %u frames in %.0f ms (%.2f ms a frame, slowest %.2f ms)\n", frames, total_ms,
                total_ms / frames, slowest_ms);
    if (const auto it = args.options.find("--wav"); it != args.options.end()) {
        if (args.positional.size() < 2) {
            return usage();
        }
        auto pcm = video.audio(*wav_track);
        if (!pcm) {
            return fail(pcm.error());
        }
        std::printf("track: %zu frames, %.2f s\n", pcm->frames(), static_cast<double>(pcm->duration_ms()) / 1000.0);
        if (auto st = core::write_file(fs::path(args.positional[1]), wave_bytes(*pcm)); !st) {
            return fail(st.error());
        }
    }
    return 0;
}

}  // namespace

int cmd_smk(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    auto bytes = core::read_file(fs::path(args.positional[0]));
    if (!bytes) {
        return fail(bytes.error());
    }
    auto video = video::Smacker::open(std::move(*bytes));
    if (!video) {
        return fail(video.error());
    }
    const auto& info = video->info();
    std::printf("SMK%u %ux%u, %u frames, %.3f fps%s%s%s\n", info.version, info.width, info.height, info.frames,
                1e6 / static_cast<double>(info.frame_us), info.ring_frame ? ", ring frame" : "",
                info.interlaced ? ", interlaced" : "", info.doubled ? ", doubled" : "");
    for (std::size_t t = 0; t < video::kSmackerTracks; ++t) {
        const auto& track = info.tracks[t];
        if (track.present) {
            std::printf("track %zu: %u Hz, %s, %d-bit%s\n", t, track.rate, track.stereo ? "stereo" : "mono",
                        track.bits16 ? 16 : 8, track.compressed ? ", compressed" : "");
        }
    }
    const auto png_dir = args.options.find("--png");
    const std::size_t every = std::max<std::size_t>(args.number("--every").value_or(1), 1);
    std::vector<std::uint8_t> rgba;
    std::size_t palettes = 0;
    while (!video->finished()) {
        const std::uint32_t frame = video->position();
        if (auto st = video->next(); !st) {
            return fail(st.error());
        }
        palettes += video->palette_changed() ? 1U : 0U;
        if (png_dir != args.options.end() && frame % every == 0) {
            video->to_rgba(rgba);
            char name[32];
            std::snprintf(name, sizeof name, "frame%04u.png", frame);
            if (auto st =
                    core::write_png_rgba(fs::path(png_dir->second) / name, info.width, info.display_height(), rgba);
                !st) {
                return fail(st.error());
            }
        }
    }
    std::printf("decoded %u frames, %zu palette changes\n", info.frames, palettes);
    if (const auto it = args.options.find("--wav"); it != args.options.end()) {
        if (args.positional.size() < 2) {
            return usage();
        }
        auto pcm = video->audio(*args.number("--wav"));  // main() checked that it is a number
        if (!pcm) {
            return fail(pcm.error());
        }
        std::printf("track: %zu frames, %.2f s\n", pcm->frames(), static_cast<double>(pcm->duration_ms()) / 1000.0);
        if (auto st = core::write_file(fs::path(args.positional[1]), wave_bytes(*pcm)); !st) {
            return fail(st.error());
        }
    }
    return 0;
}

int cmd_bk2(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    auto video = video::Bink::open_file(fs::path(args.positional[0]));
    if (!video) {
        return fail(video.error());
    }
    const auto& info = video->info();
    std::printf("KB2%c %ux%u, %u frames, %.3f fps, video flags 0x%X\n", info.revision, info.width, info.height,
                info.frames, static_cast<double>(info.fps_num) / static_cast<double>(info.fps_den), info.video_flags);
    for (std::size_t t = 0; t < info.tracks.size(); ++t) {
        const auto& track = info.tracks[t];
        std::printf("track %zu: %u Hz, %s, %s, id %u\n", t, track.rate, track.stereo ? "stereo" : "mono",
                    track.dct ? "DCT" : "RDFT", track.id);
    }
    // A track the video does not have fails before the frames are decoded.
    const auto wav_track = args.number("--wav");  // main() checked that it is a number
    if (wav_track && *wav_track >= info.tracks.size()) {
        std::fprintf(stderr, "error: the video has %zu audio track%s\n", info.tracks.size(),
                     info.tracks.size() == 1 ? "" : "s");
        return 1;
    }
    return decode(*video, info.width, info.height, info.frames, args, wav_track);
}

int cmd_webm(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    auto video = video::Webm::open_file(fs::path(args.positional[0]));
    if (!video) {
        return fail(video.error());
    }
    const auto& info = video->info();
    std::printf("WebM %ux%u, %u frames, %.3f fps\n", info.width, info.height, info.frames,
                1e6 / static_cast<double>(info.frame_us));
    for (std::size_t t = 0; t < info.tracks.size(); ++t) {
        std::printf("track %zu: Opus, %u channel%s\n", t, static_cast<unsigned>(info.tracks[t].channels),
                    info.tracks[t].channels == 1 ? "" : "s");
    }
    const auto wav_track = args.number("--wav");  // main() checked that it is a number
    if (wav_track && *wav_track >= info.tracks.size()) {
        std::fprintf(stderr, "error: the video has %zu audio track%s\n", info.tracks.size(),
                     info.tracks.size() == 1 ? "" : "s");
        return 1;
    }
    return decode(*video, info.width, info.height, info.frames, args, wav_track);
}

}  // namespace o1602::tool
