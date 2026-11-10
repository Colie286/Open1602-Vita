// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound files of the game directory, decoded once and kept (SAMPLES are small; music is decoded
// when its track starts and not kept).
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "o1602/assets/game_directory.hpp"
#include "o1602/audio/wav.hpp"

namespace o1602::audio {

class SoundBank {
public:
    explicit SoundBank(const assets::GameDirectory& game);
    ~SoundBank();
    SoundBank(const SoundBank&)=delete;
    SoundBank& operator=(const SoundBank&)=delete;
    bool enable_background();
    static void test_read_delay(unsigned ms);
    /// Pending means the original caller must retain its event and retry. One
    /// bounded worker owns decoding; the game thread never waits for that read.
    std::shared_ptr<const Pcm> request(std::string_view relative,bool& pending);

    /// The sound at a path relative to the game directory (case does not matter), decoded and kept;
    /// nullptr if it is missing or cannot be decoded (once reported in `last_error`).
    std::shared_ptr<const Pcm> get(std::string_view relative);
    /// Decodes a file without keeping it (music).
    [[nodiscard]] std::shared_ptr<const Pcm> load(std::string_view relative) const;
    /// Where a sound is (case does not matter); nothing if it is missing (reported in `last_error`).
    [[nodiscard]] std::optional<std::filesystem::path> find(std::string_view relative) const;
    /// Reads and decodes a file; touches no bank, so another thread may call it.
    [[nodiscard]] static core::Result<Pcm> decode_file(const std::filesystem::path& path);
    [[nodiscard]] const std::string& last_error() const noexcept { return m_error; }

private:
    struct Async;
    std::unique_ptr<Async> m_async;
    const assets::GameDirectory* m_game;
    std::unordered_map<std::string, std::shared_ptr<const Pcm>> m_cache;
    mutable std::string m_error;
};

}  // namespace o1602::audio
