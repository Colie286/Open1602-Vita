// SPDX-License-Identifier: GPL-3.0-or-later
#include "event_presenter.hpp"

#include <algorithm>
#include <cstdio>

#include "names.hpp"

#include "o1602/audio/game_audio.hpp"
#include "o1602/core/text.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// „Ihr konntet den Euch gestellten Auftrag erfüllen!“, in no player's colour.
constexpr std::uint16_t kMessageMissionAccomplished = 0x54;
/// „Ein vergrabener Schatz wurde entdeckt!“ (MELDUNG 0x2E).
constexpr std::uint16_t kMessageTreasure = 0x2E;
/// „Ein Schiff steht zum Verkauf“ (MELDUNG 0x0A): its icon tells of a sold ship too.
constexpr std::uint16_t kMessageShipForSale = 0x0A;

/// A message, with where it happened and the player it is about.
Announcement announce(MessageId message, std::optional<std::pair<double, double>> tile = std::nullopt,
                      std::optional<sim::PlayerId> about = std::nullopt) {
    Announcement a;
    a.message = message;
    a.tile = tile;
    a.about = about;
    return a;
}

/// Where a city's message points (Space and a click on its icon go there): the Kontor, or the middle
/// of the island while the city has none. The original posts every message with a target.
std::optional<std::pair<double, double>> city_spot(const sim::World& world, const sim::City* c) {
    if (c == nullptr || c->island >= world.islands().size()) {
        return std::nullopt;
    }
    const sim::WorldIsland& isl = world.islands()[c->island];
    if (c->warehouse_x && c->warehouse_y) {
        return std::make_pair(static_cast<double>(isl.x) + *c->warehouse_x,
                              static_cast<double>(isl.y) + *c->warehouse_y);
    }
    return std::make_pair(isl.x + isl.map.width() / 2.0, isl.y + isl.map.height() / 2.0);
}

/// A player's largest city, for a message about the player as a whole (diplomacy).
std::optional<std::pair<double, double>> player_spot(const sim::World& world, sim::PlayerId player) {
    const sim::City* best = nullptr;
    std::uint64_t most = 0;
    for (const sim::City& c : world.cities()) {
        if (c.owner != player) {
            continue;
        }
        std::uint64_t n = 0;
        for (const std::uint32_t i : c.inhabitants) {
            n += i;
        }
        if (best == nullptr || n > most) {
            best = &c;
            most = n;
        }
    }
    return city_spot(world, best);
}

/// A message whose video plays only some of its tracks: the messages 0x6B..0x6E speak to the
/// winner on track 1 and to the others on track 2.
Announcement announce_with_tracks(std::uint16_t message, std::uint8_t tracks) {
    Announcement a;
    a.message = message;
    a.tracks = tracks;
    return a;
}

/// „Euer Volk wünscht …“ for a build level: by INFRA number; the gallows (10) and the
/// levels without a building (market, chapel) have none.
std::optional<std::uint16_t> wish_message(std::uint8_t infra) {
    switch (infra) {
        case 3:  // INFRA_SCHULE: „Euer Volk braucht eine Schule“
            return std::uint16_t{0x3E};
        case 4:  // INFRA_WIRT
            return std::uint16_t{0x3C};
        case 5:  // INFRA_KIRCHE
            return std::uint16_t{0x3D};
        case 6:  // INFRA_BADE
            return std::uint16_t{0x41};
        case 7:  // INFRA_THEATER
            return std::uint16_t{0x42};
        case 8:  // INFRA_HOCHSCHULE
            return std::uint16_t{0x3F};
        case 9:  // INFRA_ARZT
            return std::uint16_t{0x40};
        case 11:  // INFRA_SCHLOSS
            return std::uint16_t{0x44};
        case 12:  // INFRA_KATHETRALE
            return std::uint16_t{0x43};
        default:
            return std::nullopt;
    }
}

/// „Es fehlt an …“ by the goods' order, food (0x34) to jewellery (0x3B); other goods have none.
std::optional<std::uint16_t> shortage_message(std::uint8_t ware) {
    if (ware >= sim::ware::kFood && ware <= sim::ware::kJewellery) {
        return static_cast<std::uint16_t>(0x34 + ware - sim::ware::kFood);
    }
    return std::nullopt;
}

}  // namespace

std::array<std::pair<std::uint8_t, std::uint32_t>, 3> island_crops(const sim::WorldIsland& island) {
    static constexpr std::array<std::uint8_t, 3> kNorth = {46, 50, 48};  // tobacco, grapes, sugar cane
    static constexpr std::array<std::uint8_t, 3> kSouth = {51, 49, 47};  // cocoa, cotton, spices
    std::array<std::pair<std::uint8_t, std::uint32_t>, 3> crops{};
    const std::array<std::uint8_t, 3>& wares = island.header.climate == sim::climate::kSouth ? kSouth : kNorth;
    for (std::size_t i = 0; i < crops.size(); ++i) {
        crops[i] = {wares[i], sim::island_fertility(island, wares[i]) * 100 / 128};
    }
    return crops;
}

std::string crops_text(const sim::WorldIsland& island) {
    std::string text;
    for (const auto& [ware, percent] : island_crops(island)) {
        if (!text.empty()) {
            text += ", ";
        }
        text += "#" + std::to_string(ware) + " " + std::to_string(percent) + " %";
    }
    return text;
}

void add_chat_line(std::deque<ChatLine>* chat, ChatLine line, bool echo) {
    if (echo && !line.name.empty()) {
        std::fprintf(stderr, "multiplayer: chat from %s (%zu bytes)\n", core::cp1252_to_utf8(line.name).c_str(),
                     line.text.size());
    }
    if (chat == nullptr) {
        return;
    }
    line.serial = chat->empty() ? 1 : chat->back().serial + 1;
    chat->push_back(std::move(line));
    while (chat->size() > kChatKept) {
        chat->pop_front();
    }
}

EventPresenter::EventPresenter(PlayerLabel label) : m_label(std::move(label)) {}

std::string EventPresenter::rival_won_text(const sim::Player& winner, std::string_view label) const {
    // "message.won.green": the translation words the colour (German declines it); a banner without
    // one (a mod's) is named by the player's name.
    if (m_banners != nullptr) {
        if (const render::Banner* b = m_banners->get(winner.banner)) {
            const std::size_t colon = b->id.find(':');
            const std::string key = "message.won." + b->id.substr(colon == std::string::npos ? 0 : colon + 1);
            if (const std::string& text = ui::tr(key); text != key) {
                return text;
            }
        }
    }
    return ui::tr("message.won", {winner.name.empty() ? label : std::string_view(winner.name)});
}

std::vector<Notice> EventPresenter::city_events(const sim::World& world, sim::PlayerId local,
                                                const std::vector<sim::CityEvent>& events) {
    std::vector<Notice> out;
    for (const sim::CityEvent& e : events) {
        if (e.kind == sim::CityEvent::Kind::Population && e.owner != local && local != sim::kNoPlayer &&
            e.message >= 0x13 && e.message <= 0x1D) {
            // Another player's city: „Eine Stadt des Konkurrenten hat nun mehr als …“ from 300 on.
            // The original has three rivals and tells each city; with up to eleven,
            // only the first city of each rival at each size.
            const std::uint32_t key = std::uint32_t{e.owner} << 8 | e.message;
            if (std::find(m_rival_sizes.begin(), m_rival_sizes.end(), key) == m_rival_sizes.end()) {
                m_rival_sizes.push_back(key);
                // The icon in the rival's colour, like the rival's new ship (it showed the local
                // player's colour while pointing at the rival's city).
                out.emplace_back(announce(static_cast<std::uint16_t>(e.message + 0x0C),
                                          city_spot(world, world.city(e.island, e.city)), e.owner));
            }
            continue;
        }
        if (e.owner != local) {
            continue;
        }
        const sim::City* city = world.city(e.island, e.city);
        const std::string name = city != nullptr ? city->name : std::string("?");
        const auto spot = city_spot(world, city);
        if (e.kind == sim::CityEvent::Kind::Hunger) {
            std::fprintf(stderr, "%s: the people are starving\n", name.c_str());
            out.emplace_back(announce(0x33, spot));
        } else if (e.kind == sim::CityEvent::Kind::Monument) {
            out.emplace_back(announce(0x31, spot));
        } else if (e.kind == sim::CityEvent::Kind::MaterialLocked) {
            std::fprintf(stderr, "%s: the people get no building material\n", name.c_str());
            out.emplace_back(announce(0x48, spot));
        } else if (e.kind == sim::CityEvent::Kind::Unlocked) {
            std::fprintf(stderr, "%s: build level %u reached\n", name.c_str(), e.infra);
            // levels.toml's message, if it gave the level one (docs/modding/levels.md).
            const auto& levels = world.infra_levels();
            const std::optional<std::string>* own = e.infra < levels.size() ? &levels[e.infra].message : nullptr;
            if (own != nullptr && own->has_value()) {
                if ((*own)->empty()) {
                    continue;  // none
                }
                if (const auto message = m_messages != nullptr ? m_messages->find(**own) : std::nullopt) {
                    Announcement a = announce(*message, spot);
                    a.text = m_messages->text(*message, ui::current_language(), {{"city", name}});
                    out.emplace_back(std::move(a));
                    continue;
                }
                std::fprintf(stderr, "levels.toml: no message \"%s\" (messages.toml)\n", (*own)->c_str());
            }
            if (const auto message = wish_message(e.infra)) {
                out.emplace_back(announce(*message, spot));
            }
        } else if (e.kind == sim::CityEvent::Kind::Population) {
            out.emplace_back(announce(e.message, spot));
        } else if (e.kind == sim::CityEvent::Kind::RunningLow) {
            std::fprintf(stderr, "%s: ware %u lasts about %u more minutes\n", name.c_str(), e.ware, e.minutes);
            if (const auto message = shortage_message(e.ware)) {
                Announcement a = announce(*message, spot);
                const assets::OwnGood* own = world.goods().own(e.ware);
                const std::string ware = own != nullptr                 ? own->name(ui::current_language())
                                         : e.ware < m_ware_names.size() ? m_ware_names[e.ware]
                                                                        : "#" + std::to_string(e.ware);
                a.text =
                    ui::tr("message.running_low", {ware, name, std::to_string(std::max<std::uint16_t>(e.minutes, 1))});
                out.emplace_back(std::move(a));
            }
        } else {
            std::fprintf(stderr, "%s: short of ware %u\n", name.c_str(), e.ware);
            // Told already, before the store ran empty (RunningLow).
            if (const auto message = shortage_message(e.ware); message && !e.warned) {
                out.emplace_back(announce(*message, spot));
            }
        }
    }
    return out;
}

std::vector<Notice> EventPresenter::ship_events(const sim::World& world, sim::PlayerId local,
                                                const std::vector<sim::ShipEvent>& events) const {
    // MELDUNG 0x2A-0x30.
    std::vector<Notice> out;
    for (const sim::ShipEvent& e : events) {
        if (e.kind == sim::ShipEvent::Kind::Sold && e.player == local && local != sim::kNoPlayer) {
            // Open1602: the seller is told who bought the ship, without voice, under the icon of
            // the offer in the buyer's colour (the original tells nobody).
            const sim::Ship* ship = world.ship(e.ship);
            const sim::Player* buyer = world.player(e.city);
            if (ship == nullptr || buyer == nullptr) {
                continue;
            }
            Announcement a = announce(kMessageShipForSale,
                                      std::make_pair(ship->pos_x / static_cast<double>(sim::kPosUnit),
                                                     ship->pos_y / static_cast<double>(sim::kPosUnit)),
                                      e.city);
            a.text = ui::tr("message.ship_bought", {player_name(*buyer), ship_name(*ship),
                                                    std::to_string(std::max<std::int64_t>(e.money, 0))});
            a.voice = false;
            out.emplace_back(std::move(a));
            std::fprintf(stderr, "%s bought the ship %s for %lld\n", m_label(*buyer, e.city).c_str(),
                         ship_name(*ship).c_str(), static_cast<long long>(e.money));
            continue;
        }
        if (e.player != local || e.island >= world.islands().size()) {
            continue;
        }
        const unsigned number = world.islands()[e.island].header.number;
        switch (e.kind) {
            case sim::ShipEvent::Kind::NativesFound:
                std::fprintf(stderr, "you discovered a native village! (island %u)\n", number);
                break;
            case sim::ShipEvent::Kind::PiratesFound:
                std::fprintf(stderr, "the pirates' hideout has been found! (island %u)\n", number);
                break;
            case sim::ShipEvent::Kind::CropsExplored:
                std::fprintf(stderr, "the crops of island %u are explored: %s\n", number,
                             crops_text(world.islands()[e.island]).c_str());
                break;
            case sim::ShipEvent::Kind::DepositFound:
                std::fprintf(stderr, "%s on island %u (%u,%u)!\n",
                             e.ware == sim::ware::kGold ? "you discovered a gold vein" : "an ore deposit lies", number,
                             e.x, e.y);
                break;
            case sim::ShipEvent::Kind::ExploreFinished:
                std::fprintf(stderr, "island %u explored\n", number);
                break;
            default:
                break;
        }
    }
    return out;
}

std::vector<Notice> EventPresenter::disaster_events(const sim::World& world, sim::PlayerId local,
                                                    const std::vector<sim::DisasterEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::DisasterEvent& e : events) {
        if (e.owner != local) {
            continue;
        }
        const sim::City* city = world.city(e.island, e.city);
        const char* name = city != nullptr ? city->name.c_str() : "?";
        const bool fire = e.disaster == sim::Disaster::Kind::Fire;
        if (e.kind == sim::DisasterEvent::Kind::Started && !e.spread && e.island < world.islands().size()) {
            std::fprintf(stderr, "%s: %s (%u,%u)\n", name,
                         fire ? "a fire broke out!" : "the plague haunts your settlement!", e.x, e.y);
            const sim::WorldIsland& isl = world.islands()[e.island];
            out.emplace_back(
                announce(fire ? std::uint16_t{0x02} : std::uint16_t{0x01},
                         std::make_pair(static_cast<double>(isl.x + e.x), static_cast<double>(isl.y + e.y))));
        } else if (e.kind == sim::DisasterEvent::Kind::BurnedDown) {
            std::fprintf(stderr, "%s: a building burned down (%u,%u)\n", name, e.x, e.y);
        }
    }
    return out;
}

std::vector<Notice> EventPresenter::island_events(const sim::World& world, sim::PlayerId local,
                                                  const std::vector<sim::IslandEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::IslandEvent& e : events) {
        const sim::City* own = nullptr;
        for (const sim::City& c : world.cities()) {
            if (c.island == e.island && c.owner == local) {
                own = &c;
            }
        }
        if (own == nullptr) {
            continue;
        }
        switch (e.kind) {
            case sim::IslandEvent::Kind::DroughtStarted:
                std::fprintf(stderr, "%s: the drought destroys your harvest!\n", own->name.c_str());
                out.emplace_back(announce(0x03, city_spot(world, own)));
                break;
            case sim::IslandEvent::Kind::Withered:
                std::fprintf(stderr, "%s: %u fields withered\n", own->name.c_str(), e.count);
                break;
            case sim::IslandEvent::Kind::Recovered:
                std::fprintf(stderr, "%s: %u fields grow again\n", own->name.c_str(), e.count);
                break;
            case sim::IslandEvent::Kind::DroughtOver:
                std::fprintf(stderr, "%s: the drought is over\n", own->name.c_str());
                break;
        }
    }
    return out;
}

std::vector<Notice> EventPresenter::volcano_events(const sim::World& world, sim::PlayerId local,
                                                   const std::vector<sim::VolcanoEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::VolcanoEvent& e : events) {
        if (e.kind == sim::VolcanoEvent::Kind::Erupted && e.message && e.island < world.islands().size() &&
            world.islands()[e.island].discovered_by(local)) {
            std::fprintf(stderr, "a volcano has erupted! (island %u)\n",
                         static_cast<unsigned>(world.islands()[e.island].header.number));
            const sim::WorldIsland& isl = world.islands()[e.island];
            out.emplace_back(
                announce(0x5E, std::make_pair(isl.x + isl.map.width() / 2.0, isl.y + isl.map.height() / 2.0)));
        }
    }
    return out;
}

void EventPresenter::command_results(const std::vector<sim::CommandResult>& results) const {
    for (const sim::CommandResult& r : results) {
        if (!r.build) {
            if (r.demolished != 0) {
                std::fprintf(stderr, "demolished %zu, back: %lld gold, %u t tools, %u t wood, %u t bricks\n",
                             r.demolished, static_cast<long long>(r.refund.money), r.refund.tools / 32,
                             r.refund.wood / 32, r.refund.bricks / 32);
            }
            continue;
        }
        std::fprintf(stderr, "built %zu of %zu", r.build->placed, r.build->items.size());
        for (const sim::PlannedBuild& item : r.build->items) {
            if (!item.ok()) {
                std::fprintf(stderr, " - %s", sim::refusal_name(item.refusal));
                break;
            }
        }
        std::fprintf(stderr, "\n");
    }
}

std::vector<Notice> EventPresenter::construction_events(const sim::World& world, sim::PlayerId local,
                                                        const std::vector<sim::ConstructionEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::ConstructionEvent& e : events) {
        if (e.kind == sim::ConstructionEvent::Kind::TreasureFound) {
            // The message at the treasure, for the builder only.
            if (e.player == local && e.island < world.islands().size()) {
                const sim::WorldIsland& i = world.islands()[e.island];
                std::fprintf(stderr, "you found a treasure: %u gold (island %u)\n", e.money,
                             static_cast<unsigned>(i.header.number));
                out.emplace_back(announce(kMessageTreasure, std::make_pair(static_cast<double>(i.x) + e.treasure.x,
                                                                           static_cast<double>(i.y) + e.treasure.y)));
            }
            continue;
        }
        if (e.kind != sim::ConstructionEvent::Kind::CityFounded) {
            continue;
        }
        if (const sim::City* c = world.city(e.island, e.city)) {
            std::fprintf(stderr, "a new city was founded: %s\n", c->name.c_str());
            if (c->owner != local && e.island < world.islands().size()) {
                const sim::WorldIsland& i = world.islands()[e.island];
                out.emplace_back(
                    announce(0x0E, std::make_pair(i.x + i.map.width() / 2.0, i.y + i.map.height() / 2.0), c->owner));
            }
        }
    }
    return out;
}

std::vector<Notice> EventPresenter::diplomacy_notices(const sim::World& world, sim::PlayerId local,
                                                      const std::vector<sim::DiplomacyNotice>& notices) const {
    static constexpr std::array<std::uint16_t, 9> kMessages = {0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x49, 0x4A, 0x4B};
    std::vector<Notice> out;
    for (const sim::DiplomacyNotice& n : notices) {
        if (n.to == local) {
            // An answer to a founding points at the computer's city on that island.
            const bool founding = n.kind == sim::DiplomacyNotice::Kind::FoundingProtest ||
                                  n.kind == sim::DiplomacyNotice::Kind::FoundingRefused ||
                                  n.kind == sim::DiplomacyNotice::Kind::FoundingTolerated;
            // The icon in the colour of the partner who acted (Open1602: with up to eleven partners the
            // message alone does not tell who).
            out.emplace_back(
                announce(kMessages[static_cast<std::size_t>(n.kind)],
                         founding ? city_spot(world, world.city(n.island, n.city)) : player_spot(world, n.from),
                         n.from != sim::kNoPlayer ? std::optional<sim::PlayerId>(n.from) : std::nullopt));
        } else if (m_rival_wars && local != sim::kNoPlayer && n.from != local &&
                   (n.kind == sim::DiplomacyNotice::Kind::PeaceBroken ||
                    n.kind == sim::DiplomacyNotice::Kind::PeaceSigned)) {
            // Open1602: war or peace between two others, without voice, under the icon of who acted.
            const sim::Player* from = world.player(n.from);
            const sim::Player* to = world.player(n.to);
            if (from == nullptr || to == nullptr || from->role == sim::PlayerRole::Neutral ||
                to->role == sim::PlayerRole::Neutral) {
                continue;
            }
            const bool war = n.kind == sim::DiplomacyNotice::Kind::PeaceBroken;
            Announcement a = announce(kMessages[static_cast<std::size_t>(n.kind)], player_spot(world, n.from), n.from);
            a.text = ui::tr(war ? "message.rival_war" : "message.rival_peace", {player_name(*from), player_name(*to)});
            a.voice = false;
            out.emplace_back(std::move(a));
            std::fprintf(stderr, "%s: %s and %s\n", war ? "war" : "peace", m_label(*from, n.from).c_str(),
                         m_label(*to, n.to).c_str());
        }
    }
    return out;
}

void EventPresenter::expect_end_screen(sim::GameOutcome outcome, std::uint64_t delay_ms, std::uint64_t now_ms) {
    if (!m_end_pending && !m_end_shown) {
        m_end_pending = EndPending{outcome, delay_ms, now_ms, false};
    }
}

std::vector<Notice> EventPresenter::game_events(const sim::World& world, sim::PlayerId local,
                                                const std::vector<sim::GameEvent>& events, std::uint64_t now_ms) {
    std::vector<Notice> out;
    // Whether `player` got a triumphal arch for defeating `defeated` in this batch.
    const auto arch_for = [&events](sim::PlayerId player, sim::PlayerId defeated) {
        return std::any_of(events.begin(), events.end(), [&](const sim::GameEvent& a) {
            return a.kind == sim::GameEvent::Kind::ArchAllowed && a.player == player && a.by == defeated;
        });
    };
    for (const sim::GameEvent& e : events) {
        const sim::Player* p = world.player(e.player);
        const std::string who =
            p != nullptr ? m_label(*p, e.player) : std::string("player ") + std::to_string(e.player);
        switch (e.kind) {
            case sim::GameEvent::Kind::MissionAccomplished:
                std::fprintf(stderr, "%s accomplished the mission!\n", who.c_str());
                if (e.player == local) {
                    expect_end_screen(sim::GameOutcome::Won, kEndScreenDelayMs, now_ms);
                    // 0x6B is the multiplayer message with the winner's colour (0x6B..0x6E); its
                    // video speaks to the winner on track 1. The original has it for its four
                    // colours only: the others hear „Ihr konntet den Euch gestellten Auftrag erfüllen!“.
                    if (e.message == audio::kMessageMultiplayerWin && p != nullptr) {
                        out.emplace_back(
                            p->banner < sim::kOriginalPlayers
                                ? announce_with_tracks(static_cast<std::uint16_t>(e.message + p->banner), 0x03)
                                : announce(kMessageMissionAccomplished));
                    } else {
                        out.emplace_back(announce(e.message));
                    }
                }
                break;
            case sim::GameEvent::Kind::GameOver:
                std::fprintf(stderr, "the game is over\n");
                out.emplace_back(ScoreUpdate{sim::kNoPlayer, true});  // op 0x3E, after the winners are known
                if (e.player != local && world.player(local) != nullptr && !world.player(local)->won) {
                    expect_end_screen(sim::GameOutcome::Lost, kEndScreenDelayMs, now_ms);
                    // The winner's message: his mission's, or that of the victory conditions.
                    if (p != nullptr && e.message == audio::kMessageMultiplayerWin &&
                        p->banner < sim::kOriginalPlayers) {
                        // A race: "the red player fulfilled his task", the others' voice (track 2).
                        out.emplace_back(announce_with_tracks(
                            static_cast<std::uint16_t>(audio::kMessageMultiplayerWin + p->banner), 0x05));
                    } else if (p != nullptr && e.message == audio::kMessageMultiplayerWin) {
                        // The further colours have no voice: the same words in the status bar only,
                        // under the winner's icon.
                        Announcement a = announce(audio::kMessageMultiplayerWin, std::nullopt, e.player);
                        a.text = rival_won_text(*p, who);
                        a.voice = false;
                        out.emplace_back(std::move(a));
                    } else {
                        out.emplace_back(announce(0x53));  // "Ihr wurdet besiegt!"
                    }
                }
                break;
            case sim::GameEvent::Kind::PlayerDefeated:
                std::fprintf(stderr, "%s is defeated\n", who.c_str());
                out.emplace_back(ScoreUpdate{e.player, false});  // op 3 sub 9, the defeated one still counted
                if (e.player == local) {
                    expect_end_screen(sim::GameOutcome::Lost, kDefeatDelayMs, now_ms);
                    out.emplace_back(announce(0x53));
                    // The defeat videos come directly, whatever the video switches say:
                    // bankruptcy 998, otherwise 1010 or 2020.
                    out.emplace_back(DirectVideo{e.reason == sim::GameEvent::Reason::Bankrupt ? std::uint16_t{998}
                                                 : (m_defeat_variant++ & 1U) == 0             ? std::uint16_t{1010}
                                                                                              : std::uint16_t{2020}});
                } else if (e.reason == sim::GameEvent::Reason::Bankrupt) {
                    out.emplace_back(announce(0x59, std::nullopt, e.player));  // the leaving player's colour
                } else if (e.by != sim::kNoPlayer && !arch_for(local, e.player)) {
                    // Op 3 sub 9: "Ein Konkurrent wurde besiegt!", unless nobody is credited, or
                    // to the credited player who hears of his triumphal arch instead; the icon in
                    // the colour of the defeated.
                    out.emplace_back(announce(0x5A, std::nullopt, e.player));
                }
                break;
            case sim::GameEvent::Kind::ArchAllowed:
                if (e.player == local) {
                    std::fprintf(stderr, "you may build a triumphal arch\n");
                    out.emplace_back(announce(0x32));
                }
                break;
        }
    }
    return out;
}

std::optional<sim::GameOutcome> EventPresenter::end_screen_due(std::uint64_t now_ms, bool videos, bool game_paused) {
    if (!m_end_pending) {
        return std::nullopt;
    }
    EndPending& e = *m_end_pending;
    const std::uint64_t step = now_ms >= e.last_ms ? now_ms - e.last_ms : 0;
    e.last_ms = now_ms;
    // WinMain runs no game frame while a video plays, the next video of a chain included.
    if (videos) {
        e.after_video = true;
        return std::nullopt;
    }
    if (!game_paused) {
        e.left_ms -= std::min(e.left_ms, step);
    }
    if (!e.after_video && e.left_ms > 0) {
        return std::nullopt;
    }
    const sim::GameOutcome outcome = e.outcome;
    m_end_pending.reset();
    return outcome;
}

void EventPresenter::notify(std::string text, std::string_view log, std::uint64_t now_ms) {
    if (!log.empty()) {
        std::fprintf(stderr, "notice: %.*s\n", static_cast<int>(log.size()), log.data());
    }
    m_notice = std::move(text);
    m_notice_until_ms = now_ms + kNoticeMs;
}

}  // namespace o1602::app
