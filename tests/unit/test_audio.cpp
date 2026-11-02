// SPDX-License-Identifier: GPL-3.0-or-later
// Sound files and the mixer (docs/formats/wav.md).

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/package_sounds.hpp"
#include "o1602/audio/game_audio.hpp"
#include "o1602/audio/mixer.hpp"
#include "o1602/audio/package_music.hpp"
#include "o1602/audio/sound_file.hpp"
#include "o1602/audio/wav.hpp"
#include "o1602/core/file.hpp"

using namespace o1602;

namespace {

void put16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    put16(v, static_cast<std::uint16_t>(x));
    put16(v, static_cast<std::uint16_t>(x >> 16));
}
void tag(std::vector<std::uint8_t>& v, const char* t) {
    v.insert(v.end(), t, t + 4);
}

/// A WAVE file with the given fmt fields and data.
std::vector<std::uint8_t> wave(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                               const std::vector<std::uint8_t>& data, std::uint16_t block_align = 0) {
    std::vector<std::uint8_t> v;
    tag(v, "RIFF");
    put32(v, static_cast<std::uint32_t>(4 + 8 + 16 + 8 + data.size()));
    tag(v, "WAVE");
    tag(v, "fmt ");
    put32(v, 16);
    put16(v, format);
    put16(v, channels);
    put32(v, rate);
    put32(v, rate * channels * bits / 8);
    put16(v, block_align != 0 ? block_align : static_cast<std::uint16_t>(channels * bits / 8));
    put16(v, bits);
    tag(v, "data");
    put32(v, static_cast<std::uint32_t>(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

std::shared_ptr<audio::Pcm> constant(std::uint32_t rate, std::size_t frames, std::int16_t value) {
    auto p = std::make_shared<audio::Pcm>();
    p->rate = rate;
    p->channels = 1;
    p->samples.assign(frames, value);
    return p;
}

}  // namespace

TEST_CASE("audio: 8-bit PCM as in SAMPLES and Speech8") {
    const auto bytes = wave(1, 1, 22050, 8, {128, 255, 0, 192});
    auto pcm = audio::decode_wav(bytes);
    REQUIRE(pcm.ok());
    CHECK(pcm->rate == 22050);
    CHECK(pcm->channels == 1);
    REQUIRE(pcm->samples.size() == 4);
    CHECK(pcm->samples[0] == 0);
    CHECK(pcm->samples[1] == 127 * 256);
    CHECK(pcm->samples[2] == -128 * 256);
    CHECK(pcm->samples[3] == 64 * 256);
    CHECK(pcm->duration_ms() == 0);
}

TEST_CASE("audio: damaged and unknown files fail without crashing") {
    CHECK(!audio::decode_wav(std::vector<std::uint8_t>{1, 2, 3}).ok());
    auto bytes = wave(1, 1, 22050, 8, {128, 128});
    bytes.resize(30);  // cut inside the fmt chunk
    CHECK(!audio::decode_wav(bytes).ok());
    CHECK(!audio::decode_wav(wave(0x55, 2, 22050, 16, {0, 0, 0, 0})).ok());  // MP3
    CHECK(!audio::decode_wav(wave(1, 7, 22050, 8, {0})).ok());
}

TEST_CASE("audio: the mixer resamples, pans and clips") {
    audio::Mixer m(44100, 4);
    // A 22050 Hz sound lasts twice as many output frames.
    const auto id = m.play(constant(22050, 100, 10000));
    REQUIRE(id != audio::Mixer::kNoVoice);
    std::vector<std::int16_t> out(2 * 150);
    m.mix(out.data(), 150);
    CHECK(out[0] == 10000);
    CHECK(out[1] == 10000);
    CHECK(m.playing(id));
    m.mix(out.data(), 60);
    CHECK(!m.playing(id));
    // Hard left.
    audio::PlayOptions left;
    left.pan = -1.0F;
    m.play(constant(44100, 10, 8000), left);
    m.mix(out.data(), 1);
    CHECK(out[0] == 8000);
    CHECK(out[1] == 0);
    // Two loud sounds clip instead of wrapping around.
    m.play(constant(44100, 10, 30000));
    m.play(constant(44100, 10, 30000));
    m.mix(out.data(), 1);
    CHECK(out[0] == 32767);
}

TEST_CASE("audio: group volumes; a full mixer drops the oldest effect, never the music") {
    audio::Mixer m(22050, 2);
    audio::PlayOptions music;
    music.group = audio::Group::Music;
    music.loop = true;
    const auto song = m.play(constant(22050, 4, 1000), music);
    const auto first = m.play(constant(22050, 1000, 1000));
    const auto second = m.play(constant(22050, 1000, 1000));
    CHECK(m.playing(song));
    CHECK(!m.playing(first));
    CHECK(m.playing(second));
    std::vector<std::int16_t> out(2 * 16);
    m.mix(out.data(), 16);  // the music loops
    CHECK(m.playing(song));
    m.stop_group(audio::Group::Effects);
    m.set_group_volume(audio::Group::Effects, 0.5F);
    m.play(constant(22050, 10, 2000));
    m.stop(song);
    m.mix(out.data(), 1);
    CHECK(out[0] == 1000);
    CHECK(m.playing_count(audio::Group::Music) == 0);
}

namespace {

/// A game directory in the temporary folder with the files GameDirectory asks for, some sounds
/// and music.
struct FakeGame {
    std::filesystem::path root;
    FakeGame() {
        root = test::temp_path("o1602_audio_test");
        std::filesystem::remove_all(root);
        for (const char* d : {"toolgfx", "gfx", "SAMPLES", "Speech8", "MUSIC8"}) {
            std::filesystem::create_directories(root / d);
        }
        for (const char* f : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            std::ofstream(root / f) << "x";
        }
        const auto tone = wave(1, 1, 22050, 8, std::vector<std::uint8_t>(2205, 200));  // 0.1 s
        write("SAMPLES/Event.wav", tone);
        write("Speech8/282.wav", tone);
        write("Speech8/284.wav", tone);
        write("Speech8/286.wav", tone);
        for (const char* t : {"b Song", "A Song", "c song", "Fight1", "Fight2"}) {
            write(std::string("MUSIC8/") + t + ".wav", wave(1, 2, 22050, 4, std::vector<std::uint8_t>(2205, 0x11), 4));
        }
    }
    ~FakeGame() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void write(const std::string& path, const std::vector<std::uint8_t>& bytes) const {
        REQUIRE(core::write_file(root / path, bytes).ok());
    }
};

}  // namespace

TEST_CASE("audio: the 4-bit music of MUSIC8 (IMA steps without the rounding term)") {
    // 0x7F: left nibble 7 (step 7: (28 + 14 + 7) >> 2 = 12), right nibble 15 (-12).
    auto pcm = audio::decode_wav(wave(1, 2, 22050, 4, {0x7F, 0x00}, 4));
    REQUIRE(pcm.ok());
    CHECK(pcm->channels == 2);
    REQUIRE(pcm->samples.size() == 4);
    CHECK(pcm->samples[0] == 12);
    CHECK(pcm->samples[1] == -12);
    // Nibble 0 after index 8 (step 16): +16 >> 2 * 0 = 0, index back to 7.
    CHECK(pcm->samples[2] == 12);
    CHECK(pcm->samples[3] == -12);
}

TEST_CASE("audio: sound files and the settings of Game.dat") {
    CHECK(audio::sample_file(1) == std::optional<std::string>("SAMPLES/kanone5.wav"));
    CHECK(audio::sample_file(54) == audio::sample_file(55));
    CHECK(!audio::sample_file(0));
    CHECK(!audio::sample_file(60));
    CHECK(!audio::sample_file(200));
    CHECK(audio::sample_file(91) == std::optional<std::string>("SAMPLES/shpmove5.wav"));
    const auto s = audio::parse_audio_settings(
        "Objekt: OPTIONS\r\n  Musik: FALSE, 6\r\n  Samples: TRUE\r\n  Random: FALSE\r\n"
        "  Volume: -750, -200, -595\r\n  Speach: 5, TRUE\r\n  Video: 7, TRUE\r\n");
    CHECK(!s.music);
    CHECK(s.effects);
    CHECK(!s.random_music);
    CHECK(s.music_volume == -750);
    CHECK(s.effect_volume == -200);
    CHECK(!s.speech[5]);
    CHECK(s.speech[1]);
    CHECK(!s.video[7]);
    CHECK(s.video[5]);
    CHECK(std::fabs(audio::gain_of(-2000) - 0.1F) < 1e-4F);
    CHECK(audio::gain_of(0) == 1.0F);
}

TEST_CASE("audio: effects play inside the view only, pan with x, and ten per frame") {
    CHECK(!audio::Effects::placement(1.0F, 0.0F));
    CHECK(!audio::Effects::placement(0.0F, -1.0F));
    const auto centre = audio::Effects::placement(0.0F, 0.0F);
    REQUIRE(centre);
    CHECK(centre->first == 1.0F);
    CHECK(centre->second == 0.0F);
    const auto right = audio::Effects::placement(0.9F, 0.9F);
    REQUIRE(right);
    CHECK(right->first < 1.0F);
    CHECK(right->second > 0.8F);

    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 64);
    audio::Effects effects(mixer, bank);
    for (int i = 0; i < 15; ++i) {
        effects.request(audio::kWavEvent, 0.0F, 0.0F);
    }
    effects.request(audio::kWavEvent, 2.0F, 0.0F);
    effects.flush();
    CHECK(mixer.playing_count(audio::Group::Effects) == 10);
}

TEST_CASE("audio: a voice without a message (the menu's banner choice), not when speech is off") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Effects effects(mixer, bank);
    audio::Voices voices(mixer, bank, effects);
    CHECK(voices.speak(107, 5));
    CHECK(voices.waiting() == 1);
    audio::AudioSettings s;
    s.speech[0] = false;
    voices.set_settings(s);
    CHECK(!voices.speak(107, 5));
    CHECK(voices.waiting() == 1);
}

TEST_CASE("audio: voice messages wait in four slots and speak one at a time") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Effects effects(mixer, bank);
    audio::Voices voices(mixer, bank, effects);
    // Plague (message 1): voices 282, 284, 286; it asks for normal speed.
    CHECK((voices.message(1, true) & audio::kMessageNormalSpeed) != 0);
    for (int i = 0; i < 4; ++i) {
        voices.message(1, false);
    }
    CHECK(voices.waiting() == 4);
    effects.flush();  // the fifth found no room: event.wav
    CHECK(mixer.playing_count(audio::Group::Effects) == 1);
    voices.update();
    CHECK(voices.speaking());
    CHECK(voices.waiting() == 3);
    voices.update();
    CHECK(voices.waiting() == 3);  // never cut off
    std::vector<std::int16_t> out(2 * 4000);
    mixer.mix(out.data(), 4000);
    voices.update();
    CHECK(voices.waiting() == 2);
    // Voices of a kind switched off: only the sound.
    audio::AudioSettings s;
    s.speech[1] = false;
    voices.set_settings(s);
    voices.stop_all();
    voices.message(2, false);
    CHECK(voices.waiting() == 0);
    // „Überprüft Eure Handelsrouten!“ has a switch of its own (the original: only the master switch).
    voices.message(0x5D, false);
    CHECK(voices.waiting() == 1);
    voices.stop_all();
    s.speech[audio::kSpeechRoutes] = false;
    voices.set_settings(s);
    voices.message(0x5D, false);
    CHECK(voices.waiting() == 0);
}

TEST_CASE("audio: messages with a video play it instead of their voice") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Effects effects(mixer, bank);
    audio::Voices voices(mixer, bank, effects);
    // Without a way to play videos (the viewer without --videos, the tests): the voice.
    voices.message(1, false);
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 1);
    voices.stop_all();

    std::vector<std::uint16_t> asked;
    voices.set_video_check([&](std::uint16_t n) {
        asked.push_back(n);
        return n != 703;  // the original names 703.smk, GOG has none
    });
    // Plague off screen: its video 282, no voice.
    voices.message(1, false);
    CHECK(voices.take_video() == std::optional<std::uint16_t>(282));
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 0);
    // On screen it speaks (no flag 0x01 or 0x08).
    voices.message(1, true);
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 1);
    voices.stop_all();
    // Flag 0x01 (the cathedral, 0x43): the video also on screen.
    voices.message(0x43, true);
    CHECK(voices.take_video() == std::optional<std::uint16_t>(262));
    // Flag 0x08 (ore found, 0x2B): on screen at about a quarter of the times.
    int videos = 0;
    for (int i = 0; i < 400; ++i) {
        voices.message(0x2B, true);
        videos += voices.take_video() ? 1 : 0;
        voices.stop_all();
    }
    CHECK(videos > 60);
    CHECK(videos < 140);
    // A missing video: the voice instead (the original stays silent, U-024).
    voices.message(8, false);
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 1);
    voices.stop_all();
    // Videos of a kind switched off (kind 1, Ereignisse): the voice.
    audio::AudioSettings s;
    s.video[1] = false;
    voices.set_settings(s);
    voices.message(1, false);
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 1);
    // The master switch blocks kind 0 too (mission success 0x5F, video 2000).
    s.video[1] = true;
    s.video[0] = false;
    voices.set_settings(s);
    voices.message(0x5F, false);
    CHECK(!voices.take_video());
}

TEST_CASE("audio: a message of a mod speaks its own sound and picks voices and videos apart") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Effects effects(mixer, bank);
    audio::Voices voices(mixer, bank, effects);
    audio::MessagePlayback own;
    own.kind = 1;
    own.voice_base = 282;  // its own sound goes before these
    own.voices = 1;
    own.sound = constant(22050, 2205, 1000);
    voices.message(own, false);
    CHECK(voices.waiting() == 1);
    voices.update();
    CHECK(voices.speaking());
    CHECK(voices.waiting() == 0);
    // Its kind switched off: only event.wav.
    audio::AudioSettings s;
    s.speech[1] = false;
    voices.set_settings(s);
    voices.stop_all();
    voices.message(own, false);
    CHECK(voices.waiting() == 0);
    effects.flush();
    CHECK(mixer.playing_count(audio::Group::Effects) == 1);
    // A video of its own number, not the voices' base.
    voices.set_settings(audio::AudioSettings{});
    voices.set_video_check([](std::uint16_t) { return true; });
    own.video_base = 23;
    own.videos = 1;
    voices.message(own, false);
    CHECK(voices.take_video() == std::optional<std::uint16_t>(23));
    CHECK(voices.waiting() == 0);
    // Without a sound of its own and without voices: event.wav.
    audio::MessagePlayback silent;
    voices.message(silent, true);
    CHECK(voices.waiting() == 0);
}

TEST_CASE("audio: music by name, battle music near shots, back after 25 s with a fade") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    std::vector<std::string> tracks;
    std::vector<std::string> fights;
    audio::Music::scan(*game, tracks, fights);
    CHECK((tracks == std::vector<std::string>{"A Song", "b Song", "c song"}));
    CHECK((fights == std::vector<std::string>{"Fight1", "Fight2"}));
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Music music(mixer, bank, tracks, fights);
    audio::AudioSettings s;
    s.random_music = false;
    music.set_settings(s);
    music.update(0, 16);
    CHECK(music.playing().empty());  // decoded beside the game first (FB-13)
    music.finish_loading();
    CHECK(music.playing() == "A Song");
    // A shot far away does nothing; one in the middle fades over to the battle music.
    music.shot(0.99F, 0.99F, 4000.0F, 4000.0F, 1000);
    CHECK(!music.battle());
    music.shot(0.1F, 0.1F, 1024.0F, 768.0F, 1000);
    CHECK(music.battle());
    music.update(1000, 1000);
    CHECK(music.playing() == "A Song");  // still fading
    music.update(2000, 3000);            // faded out: the battle music starts
    music.finish_loading();
    CHECK(music.playing().rfind("Fight", 0) == 0);
    music.update(27'000, 16);
    CHECK(!music.battle());
    music.update(27'000, 4000);
    music.finish_loading();
    CHECK(music.playing() == "b Song");
}

TEST_CASE("audio: scenery is heard no farther than in the original's widest view") {
    // Up to the original's widest view the whole view hears, unchanged.
    const audio::SceneryReach close = audio::scenery_reach(1024.0, 768.0);
    CHECK(close.x == 1.0F);
    CHECK(close.y == 1.0F);
    const audio::SceneryReach widest = audio::scenery_reach(audio::kWidestViewWidth, audio::kWidestViewHeight);
    CHECK(widest.x == 1.0F);
    CHECK(widest.y == 1.0F);
    const auto same = audio::scenery_place(close, 0.5F, -0.25F);
    REQUIRE(same);
    CHECK(same->first == 0.5F);
    CHECK(same->second == -0.25F);
    CHECK(!audio::scenery_place(close, 1.0F, 0.0F));

    // Zoomed out to four times the original's widest view (the free zoom's 0.2x on a large
    // screen): only the middle quarter hears, a sixteenth of the area, and as loud as there.
    const audio::SceneryReach far = audio::scenery_reach(4 * audio::kWidestViewWidth, 4 * audio::kWidestViewHeight);
    CHECK(std::fabs(far.x - 0.25F) < 1e-6F);
    CHECK(std::fabs(far.y - 0.25F) < 1e-6F);
    CHECK(!audio::scenery_place(far, 0.3F, 0.0F));
    CHECK(!audio::scenery_place(far, 0.0F, -0.26F));
    const auto edge = audio::scenery_place(far, 0.2F, -0.1F);
    REQUIRE(edge);
    CHECK(std::fabs(edge->first - 0.8F) < 1e-5F);
    CHECK(std::fabs(edge->second + 0.4F) < 1e-5F);
    // Placed there it pans and softens like the same spot of the original's widest view.
    const auto heard = audio::Effects::placement(edge->first, edge->second);
    const auto original = audio::Effects::placement(0.8F, -0.4F);
    REQUIRE(heard);
    REQUIRE(original);
    CHECK(std::fabs(heard->first - original->first) < 1e-5F);

    // A wide but short view: only the width is cut.
    const audio::SceneryReach wide = audio::scenery_reach(2 * audio::kWidestViewWidth, 1000.0);
    CHECK(std::fabs(wide.x - 0.5F) < 1e-6F);
    CHECK(wide.y == 1.0F);
}

namespace {

std::vector<std::uint8_t> sound_fixture(const char* name) {
    auto bytes = core::read_file(std::filesystem::path(O1602_TEST_SOUNDS) / name);
    REQUIRE(bytes.ok());
    return std::move(bytes).value();
}

}  // namespace

TEST_CASE("audio: Ogg Vorbis and FLAC files of packages are decoded, others refused") {
    // tone.flac: 0.05 s of 440 Hz at 8000 Hz, the right channel at half the left's level.
    const auto flac = audio::decode_sound(sound_fixture("tone.flac"));
    REQUIRE(flac.ok());
    CHECK(flac->rate == 8000);
    CHECK(flac->channels == 2);
    CHECK(flac->frames() == 400);
    std::int32_t loudest = 0;
    for (std::size_t f = 0; f < flac->frames(); ++f) {
        const std::int32_t left = flac->samples[f * 2];
        const std::int32_t right = flac->samples[f * 2 + 1];
        loudest = std::max(loudest, std::abs(left));
        CHECK(std::abs(right - left / 2) <= 2);
    }
    CHECK(loudest > 1000);

    // tone.ogg: the same tone, mono.
    const auto ogg = audio::decode_sound(sound_fixture("tone.ogg"));
    REQUIRE(ogg.ok());
    CHECK(ogg->rate == 8000);
    CHECK(ogg->channels == 1);
    CHECK(ogg->frames() >= 400);
    CHECK(ogg->frames() <= 512);
    CHECK(*std::max_element(ogg->samples.begin(), ogg->samples.end()) > 1000);

    // WAVE goes on through decode_wav; anything else, or a stream cut off at its start, is refused.
    const auto wav = audio::decode_sound(wave(1, 1, 22050, 8, std::vector<std::uint8_t>(100, 200)));
    REQUIRE(wav.ok());
    CHECK(wav->frames() == 100);
    CHECK(!audio::decode_sound(std::vector<std::uint8_t>{'I', 'D', '3', 4, 0, 0}).ok());
    auto cut = sound_fixture("tone.ogg");
    cut.resize(60);
    CHECK(!audio::decode_sound(cut).ok());
    auto cut_flac = sound_fixture("tone.flac");
    cut_flac.resize(20);
    CHECK(!audio::decode_sound(cut_flac).ok());
}

TEST_CASE("audio: music.toml adds a package's tracks after MUSIC8's") {
    FakeGame fake;
    std::filesystem::create_directories(fake.root / "mods" / "shanty" / "music");
    fake.write("mods/shanty/music/sea.ogg", sound_fixture("tone.ogg"));
    fake.write("mods/shanty/music/storm.flac", sound_fixture("tone.flac"));
    fake.write("mods/shanty/readme.txt", {'h', 'i'});
    const auto folder = fake.root / "mods" / "shanty";
    std::vector<audio::MusicTrack> own;
    std::vector<std::string> warnings;
    audio::apply_music_file(own, "shanty", folder,
                            "[sea]\n"
                            "file = \"music/sea.ogg\"\n"
                            "title = \"Sea Shanty\"\n"
                            "[storm]\n"
                            "file = \"music/storm.flac\"\n"
                            "battle = true\n"
                            "[text]\n"
                            "file = \"readme.txt\"\n"  // line 7: no sound
                            "[out]\n"
                            "file = \"../x.ogg\"\n"  // line 10: outside the package
                            "[none]\n"               // line 11: no file
                            "[twice]\n"
                            "file = \"music/sea.ogg\"\n"
                            "title = \"Sea Shanty\"\n"
                            "loop = true\n",  // line 15: no field
                            warnings);
    REQUIRE(own.size() == 2);
    CHECK(own[0].title == "Sea Shanty");
    CHECK(own[0].file == folder / "music" / "sea.ogg");
    CHECK(!own[0].battle);
    CHECK(own[1].title == "storm");
    CHECK(own[1].battle);
    REQUIRE(warnings.size() == 5);
    CHECK(warnings[0] == "shanty/music.toml line 7: readme.txt is missing or no WAV, Ogg Vorbis or FLAC file");
    CHECK(warnings[1] ==
          "shanty/music.toml line 10: 'file' is the path of a sound file in the package, folders split by '/'");
    CHECK(warnings[2] == "shanty/music.toml line 11: [none] needs a 'file'");
    CHECK(warnings[3] == "shanty/music.toml line 15: 'loop' is no field of a track and is passed over");
    CHECK(warnings[4] ==
          "shanty/music.toml line 12: a track is called \"Sea Shanty\" already; this one is passed over");

    // The tracks join MUSIC8's; one with a title MUSIC8 has is left out. The package's track plays
    // from its own file.
    own.push_back(audio::MusicTrack{"A Song", folder / "music" / "sea.ogg", false});
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    std::vector<std::string> tracks;
    std::vector<std::string> fights;
    audio::Music::scan(*game, tracks, fights);
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Music music(mixer, bank, tracks, fights, 1602, own);
    CHECK((music.tracks() == std::vector<std::string>{"A Song", "b Song", "c song", "Sea Shanty"}));
    music.play("Sea Shanty");
    music.finish_loading();
    CHECK(music.playing() == "Sea Shanty");
}

TEST_CASE("audio: a package's sound plays by its number among the original's") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 64);
    audio::Effects effects(mixer, bank);
    auto pcm = std::make_shared<audio::Pcm>();
    pcm->samples.assign(2205, 1000);
    effects.set_package_sounds({pcm, nullptr});
    effects.request(assets::kFirstPackageSound, 0.0F, 0.0F);
    effects.flush();
    CHECK(mixer.playing_count(audio::Group::Effects) == 1);
    // One that could not be decoded, and one beyond the packages', stay silent.
    effects.request(static_cast<audio::SampleId>(assets::kFirstPackageSound + 1), 0.0F, 0.0F);
    effects.request(static_cast<audio::SampleId>(assets::kFirstPackageSound + 7), 0.0F, 0.0F);
    effects.flush();
    CHECK(mixer.playing_count(audio::Group::Effects) == 1);
    effects.request(audio::kWavEvent, 0.0F, 0.0F);  // the original's still
    effects.flush();
    CHECK(mixer.playing_count(audio::Group::Effects) == 2);
}

TEST_CASE("audio: sounds.toml numbers a package's sounds after the original's") {
    FakeGame fake;
    std::filesystem::create_directories(fake.root / "mod" / "sounds");
    fake.write("mod/sounds/mill.wav", wave(1, 1, 22050, 8, std::vector<std::uint8_t>(100, 128)));
    fake.write("mod/sounds/fake.ogg", std::vector<std::uint8_t>{'n', 'o', 'p', 'e', 0});
    std::vector<assets::PackageSound> sounds;
    std::vector<std::string> warnings;
    assets::apply_sounds_file(sounds, "mill", fake.root / "mod",
                              "[grind]\nfile = \"sounds/mill.wav\"\n"
                              "[Loud]\nfile = \"sounds/mill.wav\"\n"  // line 3: upper case
                              "[fake]\nfile = \"sounds/fake.ogg\"\n"  // line 5: no sound file
                              "[away]\nfile = \"../mill.wav\"\n"      // line 8: outside the package
                              "[none]\nvolume = 3\n"                  // line 9, 10
                              "[again]\nfile = \"sounds/mill.wav\"\n",
                              warnings);
    REQUIRE(sounds.size() == 2);
    CHECK(sounds[0].id == "mill:grind");
    CHECK(sounds[0].number == assets::kFirstPackageSound);
    CHECK(sounds[0].file == fake.root / "mod" / "sounds" / "mill.wav");
    CHECK(sounds[1].id == "mill:again");
    CHECK(sounds[1].number == assets::kFirstPackageSound + 1);
    CHECK(assets::find_package_sound(sounds, "mill:again") == &sounds[1]);
    CHECK(assets::find_package_sound(sounds, "mill:fake") == nullptr);
    REQUIRE(warnings.size() == 5);
    CHECK(warnings[0].find("line 3: [Loud]") != std::string::npos);
    CHECK(warnings[1].find("sounds/fake.ogg is missing or no WAV") != std::string::npos);
    CHECK(warnings[2].find("'file' is the path of a sound file") != std::string::npos);
    CHECK(warnings[3].find("'volume' is no field of a sound") != std::string::npos);
    CHECK(warnings[4].find("mill:none needs a 'file'") != std::string::npos);
    const auto decoded = audio::SoundBank::decode_file(sounds[0].file);
    CHECK(decoded.ok());
}
