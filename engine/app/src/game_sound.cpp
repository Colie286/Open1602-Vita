// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_sound.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "o1602/core/file.hpp"
#include "o1602/sim/game_data.hpp"

namespace o1602::app {

namespace {

constexpr std::uint32_t kRate = 44100;
constexpr std::uint32_t kMusicSeed = 1602;  // audio::Music's default seed
// What the device holds ahead: a sound starts after what is queued, so this is the delay of every
// click, build and command sound (a player's feedback found 120 ms too long). Three frames at 60 fps.
constexpr std::uint32_t kQueueMs = 50;
// After a slow frame the queue grows to twice its length (at most this), so that a second hitch does
// not run the device dry, and shrinks back within about two seconds; before a known pause it is
// filled up to it.
constexpr std::uint32_t kMaxQueueMs = 500;
constexpr std::uint32_t kQueueShrink = 4;  // the queue gives back real time / this per frame
constexpr double kPosUnit = sim::kPosUnit;
constexpr audio::SampleId kWavBirds = 27;  // WAV_VOGEL1 .. 6
constexpr audio::SampleId kWavSurf = 33;   // WAV_WELLEN

// Messages (MELDUNG numbers).
constexpr std::uint16_t kMsgCurse = 0x04;
constexpr std::uint16_t kMsgPiratesAttack = 0x05;
constexpr std::uint16_t kMsgShipLaunched = 0x06;
constexpr std::uint16_t kMsgShipRepaired = 0x07;
constexpr std::uint16_t kMsgShipAttacked = 0x08;
constexpr std::uint16_t kMsgShipSunk = 0x09;
constexpr std::uint16_t kMsgShipForSale = 0x0A;
constexpr std::uint16_t kMsgRivalShip = 0x0B;
constexpr std::uint16_t kMsgUnitTrained = 0x0C;
constexpr std::uint16_t kMsgHealed = 0x0D;
constexpr std::uint16_t kMsgShelled = 0x0F;
constexpr std::uint16_t kMsgLanded = 0x10;
constexpr std::uint16_t kMsgCrops = 0x2A;
constexpr std::uint16_t kMsgOre = 0x2B;
constexpr std::uint16_t kMsgGold = 0x2C;
constexpr std::uint16_t kMsgNatives = 0x2F;
constexpr std::uint16_t kMsgPirates = 0x30;
constexpr std::uint16_t kMsgArmyFull = 0x45;
constexpr std::uint16_t kMsgFleetFull = 0x47;
constexpr std::uint16_t kMsgRoutes = 0x5D;

/// The player a message is about, nothing for none (the icon then takes the local player's colour).
std::optional<sim::PlayerId> attacker(sim::PlayerId p) {
    return p != sim::kNoPlayer ? std::optional<sim::PlayerId>(p) : std::nullopt;
}

std::optional<std::pair<double, double>> unit_tile(const sim::World& world, const sim::Target& t) {
    if (t.kind == sim::Target::Kind::Soldier) {
        if (const sim::Soldier* s = world.soldier(t.ship)) {
            return std::make_pair(s->pos_x / kPosUnit, s->pos_y / kPosUnit);
        }
    } else if (t.kind == sim::Target::Kind::Ship) {
        if (const sim::Ship* s = world.ship(t.ship)) {
            return std::make_pair(s->pos_x / kPosUnit, s->pos_y / kPosUnit);
        }
    } else if (t.kind == sim::Target::Kind::Building && t.island < world.islands().size()) {
        const sim::WorldIsland& i = world.islands()[t.island];
        return std::make_pair(static_cast<double>(i.x + t.x), static_cast<double>(i.y + t.y));
    }
    return std::nullopt;
}

std::uint16_t unit_figure(const sim::World& world, const sim::Target& t) {
    if (t.kind == sim::Target::Kind::Soldier) {
        if (const sim::Soldier* s = world.soldier(t.ship)) {
            return s->figure;
        }
    } else if (t.kind == sim::Target::Kind::Ship) {
        if (const sim::Ship* s = world.ship(t.ship)) {
            return s->figure;
        }
    }
    return 0;
}

}  // namespace

GameSound::GameSound(const assets::GameDirectory& game, bool effects, bool music)
    : m_game(&game),
      m_mixer(kRate, 48),
      m_bank(game),
      m_effects(m_mixer, m_bank),
      m_voices(m_mixer, m_bank, m_effects),
      m_allow_effects(effects),
      m_allow_music(music) {
    // The settings of the original's Game.dat, read only (Open1602 never writes it).
    if (const auto path = game.resolve("Game.dat")) {
        if (auto text = core::read_file(*path)) {
            m_settings = audio::parse_audio_settings(std::string(text->begin(), text->end()));
        }
    }
    const audio::AudioSettings on = audible();
    if (!on.effects && !on.music) {
        return;
    }
    (void)open_device();
}

audio::AudioSettings GameSound::audible() const noexcept {
    audio::AudioSettings on = m_settings;
    on.effects = on.effects && m_allow_effects;
    on.music = on.music && m_allow_music;
    return on;
}

void GameSound::apply() {
    const audio::AudioSettings on = audible();
    m_effects.set_enabled(on.effects);
    m_effects.set_volume(on.effect_volume);
    m_voices.set_settings(on);
    if (m_music) {
        m_music->set_settings(on);
    }
}

bool GameSound::open_device() {
    if (m_output != nullptr) {
        return true;
    }
    auto output = platform::AudioOutput::open(kRate);
    if (!output) {
        std::fprintf(stderr, "no sound: %s\n", output.error().message.c_str());
        return false;
    }
    m_output = std::move(output).value();
    std::vector<std::string> tracks;
    std::vector<std::string> fights;
    audio::Music::scan(*m_game, tracks, fights);
    m_music = std::make_unique<audio::Music>(m_mixer, m_bank, tracks, fights, kMusicSeed, m_package_music);
    apply();
    if (tracks.empty() && audible().music) {
        std::fprintf(stderr, "no music: MUSIC8 is empty or missing\n");
    }
    return true;
}

void GameSound::set_settings(const audio::AudioSettings& settings) {
    m_settings = settings;
    const audio::AudioSettings on = audible();
    if (m_output == nullptr && (on.effects || on.music) && !open_device()) {
        return;
    }
    apply();
}

void GameSound::set_package_sounds(std::span<const assets::PackageSound> sounds) {
    if (std::equal(sounds.begin(), sounds.end(), m_package_sounds.begin(), m_package_sounds.end())) {
        return;
    }
    m_package_sounds.assign(sounds.begin(), sounds.end());
    std::vector<std::shared_ptr<const audio::Pcm>> decoded;
    for (const assets::PackageSound& sound : m_package_sounds) {
        const std::size_t slot = sound.number - assets::kFirstPackageSound;
        if (decoded.size() <= slot) {
            decoded.resize(slot + 1);
        }
        auto pcm = audio::SoundBank::decode_file(sound.file);
        if (!pcm) {
            std::fprintf(stderr, "sounds.toml: %s: %s\n", sound.id.c_str(), pcm.error().message.c_str());
            continue;
        }
        decoded[slot] = std::make_shared<const audio::Pcm>(std::move(pcm).value());
    }
    m_effects.set_package_sounds(std::move(decoded));
}

void GameSound::set_package_music(std::vector<audio::MusicTrack> tracks) {
    if (tracks == m_package_music) {
        return;
    }
    m_package_music = std::move(tracks);
    if (m_music) {
        m_music->stop();
        std::vector<std::string> titles;
        std::vector<std::string> fights;
        audio::Music::scan(*m_game, titles, fights);
        m_music = std::make_unique<audio::Music>(m_mixer, m_bank, titles, fights, kMusicSeed, m_package_music);
        apply();
    }
}

std::vector<std::string> GameSound::tracks() const {
    if (m_music) {
        return m_music->tracks();
    }
    std::vector<std::string> tracks;
    std::vector<std::string> fights;
    audio::Music::scan(*m_game, tracks, fights);
    for (const audio::MusicTrack& t : m_package_music) {
        if (!t.battle && std::find(tracks.begin(), tracks.end(), t.title) == tracks.end()) {
            tracks.push_back(t.title);
        }
    }
    return tracks;
}

std::string GameSound::playing() const {
    return m_music ? m_music->playing() : std::string();
}

void GameSound::play_track(const std::string& title) {
    if (m_music) {
        m_music->play(title);
    }
}

void GameSound::test_effects() {
    if (m_output != nullptr && audible().effects) {
        m_effects.request(audio::kWavTest, 0.0F, 0.0F);
    }
}

GameSound::~GameSound() = default;

void GameSound::set_view(Locator locate, float width_px, float height_px, audio::SceneryReach reach) {
    m_locate = std::move(locate);
    m_view_w = width_px;
    m_view_h = height_px;
    m_reach = reach;
}

void GameSound::at(audio::SampleId sample, double tile_x, double tile_y) {
    if (m_output == nullptr || !m_locate || sample == audio::kWavNothing) {
        return;
    }
    const auto [x, y] = m_locate(tile_x, tile_y);
    m_effects.request(sample, x, y);
}

void GameSound::at(audio::SampleId sample, std::uint8_t variants, double tile_x, double tile_y) {
    const std::uint32_t pick = variants > 1 ? (m_variant++ * 2654435761U >> 16) % variants : 0;
    at(static_cast<audio::SampleId>(sample + pick), tile_x, tile_y);
}

void GameSound::click() {
    if (m_output != nullptr) {
        m_effects.request(audio::kWavSelect, 0.0F, 0.0F);
    }
}

std::uint16_t GameSound::message(MessageId message, std::optional<std::pair<double, double>> tile,
                                 std::optional<sim::PlayerId> about, std::string_view text, bool voice) {
    const MessageDefinition* m = m_messages->get(message);
    if (m_output == nullptr || !voice || m == nullptr) {
        const std::uint16_t flags = m != nullptr ? m->playback.flags : 0;
        if (m_listener) {
            m_listener(message, flags, tile, about, text);
        }
        return flags;
    }
    bool visible = false;
    if (tile && m_locate) {
        const auto [x, y] = m_locate(tile->first, tile->second);
        visible = std::abs(x) < 1.0F && std::abs(y) < 1.0F;
    }
    const std::uint16_t flags = m_voices.message(m->playback, visible);
    if (m_listener) {
        m_listener(message, flags, tile, about, text);
    }
    return flags;
}

void GameSound::speak(std::uint16_t base, std::uint8_t count) {
    if (m_output != nullptr) {
        (void)m_voices.speak(base, count);
    }
}

void GameSound::stop_voices() {
    if (m_output != nullptr) {
        m_voices.stop_all();
    }
}

void GameSound::construction(const sim::World& world, const std::vector<sim::ConstructionEvent>& events) {
    // One sound per batch, the first building that has one: a
    // dragged road or a row of houses sounds once.
    for (const sim::ConstructionEvent& e : events) {
        if (e.kind == sim::ConstructionEvent::Kind::Placed && e.type != nullptr && e.type->build_sound != 0 &&
            e.island < world.islands().size()) {
            const sim::WorldIsland& i = world.islands()[e.island];
            at(e.type->build_sound, static_cast<double>(i.x) + e.x + e.type->size_x / 2.0,
               static_cast<double>(i.y) + e.y + e.type->size_y / 2.0);
            return;
        }
    }
}

void GameSound::combat(const sim::World& world, const assets::FigureTable& figures,
                       const std::vector<sim::MilitaryEvent>& events, sim::PlayerId local, std::uint64_t game_ms) {
    for (const sim::MilitaryEvent& e : events) {
        const double tx = e.x / kPosUnit;
        const double ty = e.y / kPosUnit;
        switch (e.kind) {
            case sim::MilitaryEvent::Kind::Shot:
                if (const assets::FigureType* f = figures.get(e.figure)) {
                    at(f->shot_sound.sample, f->shot_sound.variants, tx, ty);
                }
                if (m_music && m_locate) {
                    const auto [x, y] = m_locate(tx, ty);
                    m_music->shot(x, y, m_view_w, m_view_h, game_ms);
                }
                break;
            case sim::MilitaryEvent::Kind::Hit: {
                // The shooter's Hitsample, else the effect's.
                const assets::FigureType* f = figures.get(unit_figure(world, e.unit));
                if (f == nullptr || f->hit_sound.sample == 0) {
                    f = figures.get(e.figure);
                }
                if (f != nullptr) {
                    at(f->hit_sound.sample, f->hit_sound.variants, tx, ty);
                }
                break;
            }
            case sim::MilitaryEvent::Kind::SoldierKilled:
                at(audio::kWavDeadGuy, tx, ty);
                break;
            case sim::MilitaryEvent::Kind::SettlementShelled:
                if (e.owner == local) {
                    message(kMsgShelled, unit_tile(world, e.target), e.player);  // the attacker's colour
                }
                break;
            case sim::MilitaryEvent::Kind::TroopsLanded:
                if (e.owner == local) {
                    message(kMsgLanded, unit_tile(world, e.unit), e.player);  // the colour of who landed
                }
                break;
            case sim::MilitaryEvent::Kind::UnitTrained:
                if (e.player == local) {
                    message(kMsgUnitTrained, unit_tile(world, e.unit));
                }
                break;
            case sim::MilitaryEvent::Kind::SoldierHealed:
                if (e.player == local || e.owner == local) {
                    message(kMsgHealed, unit_tile(world, e.target));
                }
                break;
            case sim::MilitaryEvent::Kind::ArmyFull:
                if (e.player == local) {
                    message(kMsgArmyFull);
                }
                break;
            case sim::MilitaryEvent::Kind::NativesCurse:
                if (e.player == local) {
                    message(kMsgCurse);
                }
                break;
            default:
                break;  // ships attacked and sunk come with the ship events
        }
    }
}

void GameSound::ships(const sim::World& world, const assets::FigureTable& figures,
                      const std::vector<sim::ShipEvent>& events, sim::PlayerId local, std::uint64_t game_ms) {
    for (const sim::ShipEvent& e : events) {
        const sim::Ship* ship = world.ship(e.ship);
        const auto ship_tile = ship != nullptr ? std::optional<std::pair<double, double>>(
                                                     std::make_pair(ship->pos_x / kPosUnit, ship->pos_y / kPosUnit))
                                               : std::nullopt;
        const auto island_tile = [&]() -> std::optional<std::pair<double, double>> {
            if (e.island >= world.islands().size()) {
                return std::nullopt;
            }
            const sim::WorldIsland& i = world.islands()[e.island];
            return std::make_pair(static_cast<double>(i.x + e.x), static_cast<double>(i.y + e.y));
        };
        switch (e.kind) {
            case sim::ShipEvent::Kind::Fired:
                if (ship != nullptr && ship_tile) {
                    if (const assets::FigureType* f = figures.get(ship->figure)) {
                        at(f->shot_sound.sample, f->shot_sound.variants, ship_tile->first, ship_tile->second);
                    }
                    if (m_music && m_locate) {
                        const auto [x, y] = m_locate(ship_tile->first, ship_tile->second);
                        m_music->shot(x, y, m_view_w, m_view_h, game_ms);
                    }
                }
                break;
            case sim::ShipEvent::Kind::ShipLaunched:
                if (e.player == local) {
                    message(kMsgShipLaunched, ship_tile);
                } else {
                    message(kMsgRivalShip, ship_tile, e.player);  // the icon in the rival's colour
                }
                break;
            case sim::ShipEvent::Kind::ShipRepaired:
                if (e.player == local) {
                    message(kMsgShipRepaired, ship_tile);
                }
                break;
            case sim::ShipEvent::Kind::FleetFull:
                if (e.player == local) {
                    message(kMsgFleetFull);
                }
                break;
            case sim::ShipEvent::Kind::Attacked:
                if (e.player == local) {
                    // The icon in the attacker's colour, the pirates' for pirates.
                    message(e.amount == 1 ? kMsgPiratesAttack : kMsgShipAttacked, ship_tile, attacker(e.city));
                }
                break;
            case sim::ShipEvent::Kind::Sunk:
                if (e.player == local) {
                    message(kMsgShipSunk, ship_tile);
                }
                break;
            case sim::ShipEvent::Kind::RouteWarning:
                if (e.player == local) {
                    message(kMsgRoutes, ship_tile);
                }
                break;
            case sim::ShipEvent::Kind::OfferedForSale:
                if (e.player != local) {
                    message(kMsgShipForSale, ship_tile, e.player);  // the seller's colour
                }
                break;
            case sim::ShipEvent::Kind::CropsExplored:
                if (e.player == local) {
                    message(kMsgCrops, island_tile());
                }
                break;
            case sim::ShipEvent::Kind::DepositFound:
                if (e.player == local) {
                    message(e.ware == sim::ware::kGold ? kMsgGold : kMsgOre, island_tile());
                }
                break;
            case sim::ShipEvent::Kind::NativesFound:
                if (e.player == local) {
                    message(kMsgNatives, ship_tile);
                }
                break;
            case sim::ShipEvent::Kind::PiratesFound:
                if (e.player == local) {
                    message(kMsgPirates, ship_tile);
                }
                break;
            default:
                break;
        }
    }
}

void GameSound::scenery(const std::vector<render::AmbientSound>& sounds) {
    if (m_output == nullptr || !m_locate) {
        return;
    }
    bool birds = false;
    bool surf = false;
    for (const render::AmbientSound& s : sounds) {
        const bool bird = s.kind == render::AmbientSound::Kind::Birds;
        if (bird ? birds : surf) {
            continue;
        }
        const auto [x, y] = m_locate(s.x, s.y);
        const auto place = audio::scenery_place(m_reach, x, y);
        if (!place) {
            continue;
        }
        (bird ? birds : surf) = true;
        m_effects.request(bird ? static_cast<audio::SampleId>(kWavBirds + s.variant) : kWavSurf, place->first,
                          place->second);
    }
}

void GameSound::figures(const std::vector<const std::vector<render::FigureInstance>*>& lists,
                        const std::vector<render::EffectFigure>& effects, const assets::FigureTable& table,
                        std::uint64_t game_ms, std::uint32_t speed, const render::TileBox& drawn) {
    if (m_output == nullptr || !m_locate || speed != 1) {
        return;
    }
    std::vector<std::pair<std::uint16_t, std::uint8_t>> heard;
    const auto consider = [&](std::uint16_t figure, std::uint8_t anim_index, double x, double y) {
        const assets::FigureType* type = table.get(figure);
        const assets::FigureAnim* anim = type != nullptr ? type->anim(anim_index) : nullptr;
        if (anim == nullptr || anim->sound == 0) {
            return;
        }
        const auto key = std::make_pair(figure, anim_index);
        if (std::find(heard.begin(), heard.end(), key) != heard.end()) {
            return;  // one per type and frame: the original's trigger sits in the type
        }
        const auto [vx, vy] = m_locate(x, y);
        if (std::abs(vx) >= 1.0F || std::abs(vy) >= 1.0F) {
            return;
        }
        heard.push_back(key);
        const std::uint64_t cycle_ms = std::max<std::uint64_t>(1, std::uint64_t{anim->count} * anim->speed_ms);
        const std::uint64_t cycle = game_ms / cycle_ms;
        const auto [it, fresh] = m_cycles.try_emplace(key, cycle);
        if (fresh || it->second == cycle) {
            return;
        }
        it->second = cycle;
        // n = 1: always; else r = rand() % n, played only for r < k.
        const std::uint32_t r = anim->sound_range > 1 ? (m_variant++ * 2654435761U >> 16) % anim->sound_range : 0;
        if (r < anim->sound_chance) {
            at(static_cast<audio::SampleId>(anim->sound + r), x, y);
        }
    };
    for (const auto* list : lists) {
        if (list == nullptr) {
            continue;
        }
        for (const render::FigureInstance& f : *list) {
            if (drawn.contains(f.x + 0.5, f.y + 0.5)) {  // FB-9: what is off screen is not heard
                consider(f.figure, f.anim, f.x + 0.5, f.y + 0.5);
            }
        }
    }
    for (const render::EffectFigure& e : effects) {
        if (drawn.contains(e.x, e.y)) {
            consider(e.figure, e.anim, e.x, e.y);
        }
    }
}

void GameSound::order(bool ship, bool attack, double tile_x, double tile_y) {
    // A voice of the unit; ships attacking ships have four, others two.
    if (ship) {
        attack ? at(audio::kWavShipAttack, 4, tile_x, tile_y) : at(audio::kWavShipMove, 5, tile_x, tile_y);
    } else {
        attack ? at(audio::kWavSoldierAttack, 6, tile_x, tile_y) : at(audio::kWavSoldierMove, 4, tile_x, tile_y);
    }
}

void GameSound::start_video(const std::vector<std::shared_ptr<const audio::Pcm>>& tracks) {
    stop_video();
    m_video = true;
    if (m_output == nullptr) {
        return;
    }
    // The original stops the music; a voice already speaking goes on.
    if (m_music) {
        m_music->stop();
    }
    for (const auto& pcm : tracks) {
        // The sample volume, for every track (the original leaves track 2 at full volume, U-019).
        audio::PlayOptions o;
        o.group = audio::Group::Speech;
        o.volume = audio::gain_of(m_settings.effect_volume);
        m_video_voices.push_back(m_mixer.play(pcm, o));
    }
}

void GameSound::stop_video() {
    for (const auto v : m_video_voices) {
        m_mixer.stop(v);
    }
    m_video_voices.clear();
    m_video = false;
}

void GameSound::frame(std::uint64_t game_ms, std::uint32_t real_ms) {
    if (m_output == nullptr) {
        return;
    }
    m_effects.flush();
    // While a video plays, the voices and the music wait.
    if (!m_video) {
        m_voices.update();
        if (m_music) {
            m_music->update(game_ms, real_ms);
        }
    }
    m_queue_ms =
        std::max<std::uint32_t>(kQueueMs, m_queue_ms - std::min<std::uint32_t>(m_queue_ms, real_ms / kQueueShrink));
    m_queue_ms = std::max(m_queue_ms, std::min(kMaxQueueMs, 2 * real_ms));
    top_up(m_queue_ms);
}

void GameSound::prefill() {
    if (m_output != nullptr) {
        top_up(kMaxQueueMs);
    }
}

std::uint32_t GameSound::queued_ms() const noexcept {
    return m_output != nullptr ? static_cast<std::uint32_t>(m_output->queued_frames() * 1000 / kRate) : 0;
}

void GameSound::top_up(std::uint32_t ms) {
    const std::size_t target = std::size_t{kRate} * ms / 1000;
    const std::size_t queued = m_output->queued_frames();
    if (queued < target) {
        const std::size_t n = target - queued;
        m_buffer.resize(n * 2);
        m_mixer.mix(m_buffer.data(), n);
        m_output->push(m_buffer.data(), n);
    }
}

}  // namespace o1602::app
