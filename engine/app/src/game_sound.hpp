// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sounds of a running game: turns the simulation's events, the scenery's
// sounds and the player's orders into effects, voice messages and music, and feeds the device.
// Each computer plays its own sounds; nothing here changes the game.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "message_table.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/package_sounds.hpp"
#include "o1602/audio/game_audio.hpp"
#include "o1602/audio/package_music.hpp"
#include "o1602/platform/audio_output.hpp"
#include "o1602/render/ambient.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/video/smacker.hpp"

namespace o1602::app {

class GameSound {
public:
    /// Opens the sound device; without one (or with neither sound nor music wanted) the game stays
    /// silent and every call does nothing.
    GameSound(const assets::GameDirectory& game, bool effects, bool music);
    ~GameSound();
    GameSound(const GameSound&) = delete;
    GameSound& operator=(const GameSound&) = delete;

    [[nodiscard]] bool active() const noexcept { return m_output != nullptr; }

    /// Where world tiles are on screen this frame: the offset from the middle of the view in view
    /// half sizes (-1 .. 1 on screen), the view's size in pixels and how far the scenery is heard
    /// (audio::scenery_reach).
    using Locator = std::function<std::pair<float, float>(double tile_x, double tile_y)>;
    void set_view(Locator locate, float width_px, float height_px, audio::SceneryReach reach = {});

    /// A sound at a world tile (WAV_* number).
    void at(audio::SampleId sample, double tile_x, double tile_y);
    /// A sound with variants (`sample` + 0 .. variants - 1).
    void at(audio::SampleId sample, std::uint8_t variants, double tile_x, double tile_y);
    /// A click on the panel.
    void click();

    /// The settings of the sounds (the panel MUSIK and ANNO); switching sound or music on opens
    /// the device if the game started without it. These are the player's choices, as saved:
    /// --no-sound and --no-music silence only this run and never show up here.
    [[nodiscard]] const audio::AudioSettings& settings() const noexcept { return m_settings; }
    void set_settings(const audio::AudioSettings& settings);
    /// The music of the content packages, after MUSIC8's tracks (docs/modding/sounds.md); the music
    /// starts anew with them.
    void set_package_music(std::vector<audio::MusicTrack> tracks);
    /// The sounds of the packages (sounds.toml), decoded here once; buildings and figures play them
    /// by their numbers. A file that cannot be decoded stays silent, with a line in the log.
    void set_package_sounds(std::span<const assets::PackageSound> sounds);
    /// Titles of the music (empty without device) and the one playing now.
    [[nodiscard]] std::vector<std::string> tracks() const;
    [[nodiscard]] std::string playing() const;
    void play_track(const std::string& title);
    /// The test sound of the effects' volume, in the middle of the view.
    void test_effects();
    /// A message for the local player at a world tile (nothing: no place), about another player
    /// (a rival's new city or ship; nothing: the local player), with a text of its own for the
    /// status bar (empty: the original's) and without its voice if `voice` is off. Returns its flags.
    std::uint16_t message(MessageId message, std::optional<std::pair<double, double>> tile = std::nullopt,
                          std::optional<sim::PlayerId> about = std::nullopt, std::string_view text = {},
                          bool voice = true);
    /// One of `count` voices from `base` (every second file), without a message (the menu).
    void speak(std::uint16_t base, std::uint8_t count);
    /// Stops the voice speaking and those waiting (closing the tutorial, _MaxstreamStop).
    void stop_voices();
    /// Hears every message with its flags (the status bar's icons in the colour of `about`, its own
    /// text, normal speed).
    using Listener =
        std::function<void(MessageId message, std::uint16_t flags, std::optional<std::pair<double, double>> tile,
                           std::optional<sim::PlayerId> about, std::string_view text)>;
    void set_listener(Listener listener) { m_listener = std::move(listener); }
    /// What the messages play (the mods' messages among them); without: the original's. Outlives this.
    void set_messages(const MessageTable* messages) noexcept {
        m_messages = messages != nullptr ? messages : &MessageTable::original();
    }

    /// Messages pick their videos when `check` says the number can be played (without: never).
    void set_video_check(std::function<bool(std::uint16_t)> check) { m_voices.set_video_check(std::move(check)); }
    /// The video a message asked for, once.
    [[nodiscard]] std::optional<std::uint16_t> take_video() noexcept { return m_voices.take_video(); }
    /// A video starts: its tracks, decoded beforehand (VideoPlayer), play at
    /// the effects' volume, the music stops, voices and music wait until stop_video().
    void start_video(const std::vector<std::shared_ptr<const audio::Pcm>>& tracks);
    void stop_video();

    // Events of the simulation that sound, for the local player `local`.
    void construction(const sim::World& world, const std::vector<sim::ConstructionEvent>& events);
    void combat(const sim::World& world, const assets::FigureTable& figures,
                const std::vector<sim::MilitaryEvent>& events, sim::PlayerId local, std::uint64_t game_ms);
    void ships(const sim::World& world, const assets::FigureTable& figures, const std::vector<sim::ShipEvent>& events,
               sim::PlayerId local, std::uint64_t game_ms);
    /// Birds and surf: only within the scenery's reach, and at most one of each per frame, as the
    /// original plays at most one sound per figure type and pass.
    void scenery(const std::vector<render::AmbientSound>& sounds);
    /// Work and animal sounds (AnimSmp) of the figures on screen: once per cycle of an animation
    /// and figure type, at the first figure of that type in view, only at normal speed.
    /// Figures outside `drawn` are passed over unlooked at.
    void figures(const std::vector<const std::vector<render::FigureInstance>*>& lists,
                 const std::vector<render::EffectFigure>& effects, const assets::FigureTable& table,
                 std::uint64_t game_ms, std::uint32_t speed, const render::TileBox& drawn = {});
    /// Move or attack order for a ship (true) or soldiers at a tile.
    void order(bool ship, bool attack, double tile_x, double tile_y);

    /// Every frame: plays what was asked for, the next voice, the music, and tops up the device.
    void frame(std::uint64_t game_ms, std::uint32_t real_ms);
    /// Before a known pause of the frames (loading a game, a zoom level, a video): fills the
    /// device's queue for longer.
    void prefill();
    /// How long the device plays what it holds now: a sound started now is heard that much later.
    [[nodiscard]] std::uint32_t queued_ms() const noexcept;

private:
    /// Opens the device and the music; false without a device.
    bool open_device();
    /// The settings as they sound: the player's, less what the command line switched off.
    [[nodiscard]] audio::AudioSettings audible() const noexcept;
    /// Hands the audible settings to effects, voices and music.
    void apply();
    /// Mixes until the device holds `ms` of sound.
    void top_up(std::uint32_t ms);

    const assets::GameDirectory* m_game;
    std::unique_ptr<platform::AudioOutput> m_output;
    audio::Mixer m_mixer;
    audio::SoundBank m_bank;
    audio::Effects m_effects;
    audio::Voices m_voices;
    std::unique_ptr<audio::Music> m_music;
    std::vector<audio::MusicTrack> m_package_music;
    std::vector<assets::PackageSound> m_package_sounds;
    audio::AudioSettings m_settings;
    bool m_allow_effects = true;  ///< false with --no-sound
    bool m_allow_music = true;    ///< false with --no-music
    Locator m_locate;
    Listener m_listener;
    const MessageTable* m_messages = &MessageTable::original();
    float m_view_w = 1024.0F;
    float m_view_h = 768.0F;
    audio::SceneryReach m_reach;
    std::uint32_t m_variant = 0;  ///< own counter for the variants: no game random numbers (S-112)
    std::vector<std::int16_t> m_buffer;
    std::uint32_t m_queue_ms = 50;  ///< what the device holds now (kQueueMs, more after a slow frame)
    std::vector<audio::Mixer::VoiceId> m_video_voices;
    bool m_video = false;
    std::map<std::pair<std::uint16_t, std::uint8_t>, std::uint64_t> m_cycles;  ///< figure, anim -> last cycle heard
};

}  // namespace o1602::app
