// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sounds of the game as the original plays them (docs/re/audio.md): effects at a place on the
// screen, the voice messages and the music. Audio is presentation only: it follows the simulation's
// events and the camera, draws from its own random numbers (the original takes the game's rand(),
// S-112) and never changes the game.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/audio/mixer.hpp"
#include "o1602/audio/package_music.hpp"
#include "o1602/audio/sound_bank.hpp"

namespace o1602::audio {

// ---- Effects -------------------------------------------------------------------------------------

/// The original's sound numbers (WAV_* of the .cod files, 0..91).
using SampleId = std::uint16_t;
inline constexpr std::size_t kSampleCount = 93;  ///< the original's 92 and Open1602's kWavBakery
inline constexpr SampleId kWavNothing = 0;
inline constexpr SampleId kWavDeadGuy = 11;        ///< a unit killed
inline constexpr SampleId kWavSelect = 34;         ///< a click on the panel
inline constexpr SampleId kWavEvent = 35;          ///< a message without voice
inline constexpr SampleId kWavSoldierAttack = 73;  ///< + 0..5
inline constexpr SampleId kWavShipAttack = 79;     ///< + 0..3
inline constexpr SampleId kWavSoldierMove = 83;    ///< + 0..3
inline constexpr SampleId kWavShipMove = 87;       ///< + 0..4
/// Open1602 (D-001): SAMPLES/Backer.wav, which the original never loads, for the bakery that names
/// the undefined WAV_BAECKER (assets::fixes::kBakerySound).
inline constexpr SampleId kWavBakery = 92;
inline constexpr SampleId kWavTest = 58;  ///< "Test" of the effects' volume (plantage.wav, 0x0049572D)

/// File of a sound relative to the game directory ("SAMPLES/kanone5.wav"), nothing for the
/// numbers the original leaves silent (GameInit 0x00494870).
[[nodiscard]] std::optional<std::string> sample_file(SampleId id);

/// Volume in the original's unit, 1/100 dB of attenuation (-4000 .. 0), as a gain.
[[nodiscard]] float gain_of(std::int32_t centibel) noexcept;

/// The kinds of messages the voice and video options switch: 0 (all) and the original's seven of
/// [SPEECHKIND], then Open1602's own.
inline constexpr std::size_t kMessageKinds = 9;
/// Open1602: „Überprüft Eure Handelsrouten!“ (MELDUNG 0x5D) gets a switch of its own; the
/// original gives it kind 0, which only the master switch mutes.
inline constexpr std::uint8_t kSpeechRoutes = 8;

struct AudioSettings {
    bool music = true;
    bool effects = true;
    bool random_music = true;
    std::int32_t music_volume = -1000;  ///< 1/100 dB
    std::int32_t effect_volume = 0;
    /// Voice messages per kind (Text.cod [SPEECHKIND]): [0] all, [1..7] Ereignisse .. Diplomatie,
    /// [8] Open1602's trade routes (kSpeechRoutes).
    std::array<bool, kMessageKinds> speech{true, true, true, true, true, true, true, true, true};
    /// Videos of the messages per kind, the same kinds ("Video: k, TRUE" in Game.dat switches off).
    std::array<bool, kMessageKinds> video{true, true, true, true, true, true, true, true, true};
    std::vector<std::string> disabled_songs;  ///< by track title (the original keeps indices)
};

/// The audio lines of the original's Game.dat (read only; Open1602 never writes it): Musik,
/// Samples, Random, Volume (the third value is the mission progress), Song, Speach, Video.
[[nodiscard]] AudioSettings parse_audio_settings(std::string_view game_dat);

/// Effects at places on the screen (Maxsound.dll's MaxwavePlayOnce): the place is the offset from
/// the middle of the view, -1 .. 1 across its width and height. Outside the view a sound is not
/// played (the original culls by the whole screen around the view's middle, U-015); inside it
/// pans with x and softens towards the corners (DirectSound3D with the listener half a view behind
/// the screen and rolloff 2). At most `per_frame` requests per frame play, as in
/// the original (10, later ones are dropped).
class Effects {
public:
    Effects(Mixer& mixer, SoundBank& bank, std::size_t per_frame = 10)
        : m_mixer(&mixer), m_bank(&bank), m_limit(per_frame) {}

    void set_enabled(bool on) noexcept { m_enabled = on; }
    void set_volume(std::int32_t centibel) noexcept { m_gain = gain_of(centibel); }
    /// Queues a sound at (x, y) of the view.
    void request(SampleId id, float x, float y);
    /// Plays what was queued since the last call.
    void flush();

    /// Volume and pan of a sound at (x, y) of the view; nothing outside it.
    [[nodiscard]] static std::optional<std::pair<float, float>> placement(float x, float y) noexcept;

private:
    struct Request {
        SampleId id = 0;
        float x = 0.0F;
        float y = 0.0F;
    };
    Mixer* m_mixer;
    SoundBank* m_bank;
    std::size_t m_limit;
    bool m_enabled = true;
    float m_gain = 1.0F;
    std::vector<Request> m_queue;
};

/// The widest view the original hears its scenery in (birds and surf): its game view at 1024 x 768
/// (768 x 734, docs/re/ui.md) at the small zoom level, in pixels of the large level (four each).
inline constexpr double kWidestViewWidth = 768.0 * 4;
inline constexpr double kWidestViewHeight = 734.0 * 4;

/// How far the scenery is heard, in view half sizes: the whole view up to the original's widest
/// one, beyond that (the free zoom shows more) only the middle that the widest view covers. More
/// trees and beaches on screen thus never sound more often than in the original.
struct SceneryReach {
    float x = 1.0F;
    float y = 1.0F;
};
/// The reach for a view of `width` x `height` pixels of the large zoom level.
[[nodiscard]] SceneryReach scenery_reach(double width, double height) noexcept;
/// Where a scenery sound at (x, y) of the view (Effects::request) plays: within the reach it is
/// placed as in the original's widest view, so it pans and softens as there; nothing outside.
[[nodiscard]] std::optional<std::pair<float, float>> scenery_place(const SceneryReach& reach, float x,
                                                                   float y) noexcept;

// ---- Voice messages ------------------------------------------------------------------------------

/// What a message (MELDUNG number) brings (table 0x0049B210): its kind of voice message, the first
/// voice file and how many there are (every second number), the videos and flags.
struct MessageAudio {
    std::uint8_t kind = 0;  ///< [SPEECHKIND], 0 = only the master switch
    std::uint16_t base = 0;
    std::uint8_t voices = 0;
    std::uint8_t videos = 0;
    std::uint16_t flags = 0;
};
inline constexpr std::uint16_t kMessageVideoAlways = 0x01;     ///< video also when the place is on screen
inline constexpr std::uint16_t kMessageVideoQuarter = 0x08;    ///< on screen: video at a quarter of the times
inline constexpr std::uint16_t kMessageNormalSpeed = 0x02;     ///< the game falls back to normal speed
inline constexpr std::uint16_t kMessageVoiceHidden = 0x20;     ///< voice only if the place is off screen
inline constexpr std::uint16_t kMessageMultiplayerWin = 0x6B;  ///< + banner 0..3

[[nodiscard]] const MessageAudio* message_audio(std::uint16_t message) noexcept;

/// What a message plays, in general: an original message (from its MessageAudio) or one of
/// Open1602's own or a mod's (app::MessageTable), which may bring its own sound and pick its voices
/// and videos apart (the original takes both from `base`).
struct MessagePlayback {
    std::uint8_t kind = 0;  ///< [SPEECHKIND], 0 = only the master switch
    std::uint16_t flags = 0;
    std::uint16_t voice_base = 0;  ///< Speech8 files voice_base, voice_base + 2, ...
    std::uint8_t voices = 0;
    std::uint16_t video_base = 0;  ///< VideoSmk numbers video_base, video_base + 1, ...
    std::uint8_t videos = 0;
    std::shared_ptr<const Pcm> sound;  ///< own sound, spoken instead of the Speech8 voices

    /// The playback of an original message.
    [[nodiscard]] static MessagePlayback of(const MessageAudio& m) {
        return MessagePlayback{m.kind, m.flags, m.base, m.voices, m.base, m.videos, nullptr};
    }
};

/// The voice of the messages (Speech8): four waiting at most, one speaking at a time, never cut
/// off (0x00488D00, 0x00488D50). A message that finds no room, or has no voice, sounds event.wav.
class Voices {
public:
    Voices(Mixer& mixer, SoundBank& bank, Effects& effects, std::uint32_t seed = 1602)
        : m_mixer(&mixer), m_bank(&bank), m_effects(&effects), m_random(seed) {}

    void set_settings(const AudioSettings& s) noexcept { m_settings = s; }
    /// Whether the video of a number can be played (the file exists, videos are wanted). Without
    /// it no message picks a video; a message whose video is missing speaks instead (the original
    /// stays silent then, U-024).
    void set_video_check(std::function<bool(std::uint16_t)> check) { m_video_check = std::move(check); }
    /// A message for the local player; `visible` whether its place is on screen. Returns the
    /// message's flags (kMessageNormalSpeed asks the game to slow down). A message plays its video
    /// instead of a voice when videos of its kind are on and its place is off screen, or always
    /// (flag 0x01), or at a quarter of the times (flag 0x08) (Msg_Audio 0x00488E80).
    std::uint16_t message(std::uint16_t message, bool visible);
    /// The same for a message described by its playback (own sound before the Speech8 voices).
    std::uint16_t message(const MessagePlayback& m, bool visible);
    /// One of `count` voice files base, base + 2, ... at random, without a message (the menu's
    /// "Wählt Euer Banner": Speech_Queue(0x6B, 5, 2) 0x0048C008). False if muted or no room.
    bool speak(std::uint16_t base, std::uint8_t count);
    /// The video the last messages asked for (the latest wins), once.
    [[nodiscard]] std::optional<std::uint16_t> take_video() noexcept { return std::exchange(m_video, std::nullopt); }
    /// Starts the next voice when the last one has ended.
    void update();
    void stop_all();
    [[nodiscard]] std::size_t waiting() const noexcept;
    [[nodiscard]] bool speaking() const noexcept { return m_mixer->playing(m_voice); }

private:
    static constexpr std::size_t kSlots = 4;
    /// A waiting voice: a Speech8 file number or a sound of its own.
    struct Slot {
        std::uint16_t number = 0;  ///< 0 and no sound: free
        std::shared_ptr<const Pcm> sound;
        [[nodiscard]] bool free() const noexcept { return number == 0 && !sound; }
    };
    /// One of `count` files from `base` (every second number) into a free slot.
    bool queue(std::uint16_t base, std::uint8_t count);
    /// An own sound into a free slot.
    bool queue(std::shared_ptr<const Pcm> sound);
    Mixer* m_mixer;
    SoundBank* m_bank;
    Effects* m_effects;
    std::mt19937 m_random;
    AudioSettings m_settings;
    std::function<bool(std::uint16_t)> m_video_check;
    std::optional<std::uint16_t> m_video;
    std::array<Slot, kSlots> m_queue{};
    std::size_t m_cursor = 0;
    Mixer::VoiceId m_voice = Mixer::kNoVoice;
};

// ---- Music ---------------------------------------------------------------------------------------

/// The music (Music_Update 0x00489190): the tracks of MUSIC8 except Fight1..3, at random (not
/// one of the last few) or in order; battle music while shots fall near the middle of the view, back
/// 25 s after the last; a change fades out over 3 s. The order of the tracks is by name, not by
/// the file system's (U-014). The tracks of content packages come after the original's
/// (package_music.hpp).
class Music {
public:
    Music(Mixer& mixer, SoundBank& bank, std::vector<std::string> tracks, std::vector<std::string> fights,
          std::uint32_t seed = 1602, std::vector<MusicTrack> own = {});

    /// Titles of the MUSIC8 files (without ".wav"), sorted: normal tracks and the Fight tracks.
    static void scan(const assets::GameDirectory& game, std::vector<std::string>& tracks,
                     std::vector<std::string>& fights);

    void set_settings(const AudioSettings& s);
    /// A shot at (x, y) of the view, in view half sizes; near the middle it calls the battle music.
    void shot(float x, float y, float view_width_px, float view_height_px, std::uint64_t game_ms);
    /// Every frame: `game_ms` the game time (the modes follow it), `real_ms` the frame time (fading).
    void update(std::uint64_t game_ms, std::uint32_t real_ms);

    [[nodiscard]] const std::vector<std::string>& tracks() const noexcept { return m_tracks; }
    /// Stops the track at once (a video starts, 0x00488C00); the next update picks a new one.
    void stop();
    /// Plays `title` now (a click on it in the list, 0x00488C20); the list goes on from there.
    void play(const std::string& title);
    /// Title playing now, empty if none (also while its file is still being decoded).
    [[nodiscard]] std::string playing() const;
    /// Waits for a track being decoded and starts it (tests; update() does so once it is there).
    void finish_loading();
    [[nodiscard]] bool battle() const noexcept { return m_battle; }

private:
    /// Starts decoding a track beside the game (FB-13: a track of 20 MB took a frame or two).
    void start(const std::string& title);
    /// The track decoded, if it is there (`wait`: waits for it); plays it.
    void take_loaded(bool wait);
    void fade_out();
    [[nodiscard]] std::optional<std::string> pick(std::uint64_t game_ms);

    Mixer* m_mixer;
    SoundBank* m_bank;
    std::vector<std::string> m_tracks;
    std::vector<std::string> m_fights;
    std::vector<MusicTrack> m_own;  ///< the packages' tracks, played from their files
    std::vector<bool> m_enabled;
    std::mt19937 m_random;
    AudioSettings m_settings;
    Mixer::VoiceId m_voice = Mixer::kNoVoice;
    std::string m_title;
    std::vector<std::string> m_history;  ///< last tracks played, newest last
    std::optional<std::size_t> m_current;
    bool m_battle = false;
    std::uint64_t m_mode_ms = 0;  ///< game time of the last shot or mode change
    bool m_fading = false;
    std::uint32_t m_fade_ms = 0;
    /// The track whose file a thread decodes. Dropped when the music stops; the thread ends on its
    /// own (a future of a packaged_task does not wait for it).
    struct Loading {
        std::string title;
        std::future<core::Result<Pcm>> pcm;
    };
    std::optional<Loading> m_loading;
};

}  // namespace o1602::audio
