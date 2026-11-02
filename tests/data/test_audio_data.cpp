// SPDX-License-Identifier: GPL-3.0-or-later
// Sounds of the original data.

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/audio/game_audio.hpp"

using namespace o1602;

TEST_CASE("data: the sounds the .cod files name (Bausample, Shotsample, Hitsample, AnimSmp)") {
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        SKIP("ANNO1602_DIR not set");
    }
    auto buildings = assets::BuildingTable::load(*game);
    auto figures = assets::FigureTable::load(*game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    std::size_t kontor = 0;
    std::size_t silent = 0;
    for (const assets::BuildingType& b : buildings->types()) {
        kontor += b.build_sound == 37 ? 1U : 0U;  // WAV_KONTOR: Kontors and the pirates' Kontor
        silent += b.build_sound != 0 && !audio::sample_file(b.build_sound) ? 1U : 0U;
    }
    CHECK(kontor >= 2);
    // Silent numbers the data names: the pyramid only (WAV_PYRAMIDE).
    CHECK(silent >= 1);
    // Cannon towers and gunners: WAV_SHOTKAN1 with two variants.
    bool cannon = false;
    for (std::uint32_t n = 0; n < 256; ++n) {
        const assets::FigureType* f = figures->get(n);
        cannon = cannon || (f != nullptr && f->shot_sound.sample == 1 && f->shot_sound.variants == 2);
    }
    CHECK(cannon);
    // The birds (VOGELSND 72): one of WAV_VOGEL1..6 per cycle.
    const assets::FigureType* birds = figures->get(72);
    REQUIRE(birds != nullptr);
    const auto sounding = std::find_if(birds->anims.begin(), birds->anims.end(),
                                       [](const assets::FigureAnim& a) { return a.sound != 0; });
    REQUIRE(sounding != birds->anims.end());
    CHECK(sounding->sound == 27);
    CHECK(sounding->sound_range == 6);
    CHECK(sounding->sound_chance == 6);
    // Sound files: decode every effect the game loads, when the folder is there.
    if (game->resolve("SAMPLES")) {
        audio::SoundBank bank(*game);
        for (audio::SampleId id = 0; id < audio::kSampleCount; ++id) {
            if (const auto file = audio::sample_file(id)) {
                const auto pcm = bank.get(*file);
                CHECK(pcm != nullptr);
                if (pcm == nullptr) {
                    std::printf("  %s: %s\n", file->c_str(), bank.last_error().c_str());
                }
            }
        }
    }
    // The music: the theme decodes without drifting (the reference IMA code would).
    if (game->resolve("MUSIC8/ANNO1602 Theme.wav")) {
        audio::SoundBank bank(*game);
        const auto theme = bank.load("MUSIC8/ANNO1602 Theme.wav");
        REQUIRE(theme != nullptr);
        CHECK(theme->channels == 2);
        CHECK(theme->duration_ms() > 100'000);
        std::size_t clipped = 0;
        for (const std::int16_t v : theme->samples) {
            clipped += v == 32767 || v == -32768 ? 1U : 0U;
        }
        CHECK(clipped < 100);
        CHECK(std::abs(theme->samples[theme->samples.size() - 2]) < 2000);
    }
    if (game->resolve("Speech8/282.wav")) {
        audio::SoundBank bank(*game);
        CHECK(bank.load("Speech8/282.wav") != nullptr);
    }
}
