// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's players (ADR 0013): the page "Players" shows the humans' and computers' slots and
// changes what each is and starts with, adds slots up to sim::kMaxPlayers and takes the last away, each
// change a step of undo.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "map_viewer.hpp"
#include "names.hpp"

#include "o1602/editor/players.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// The colour of a banner as the multiplayer lobby names it, Windows-1252.
std::string colour_name(sim::BannerId banner) {
    const auto banners = editor::player_banners();
    const auto it = std::find(banners.begin(), banners.end(), banner);
    if (it == banners.end()) {
        return std::to_string(banner + 1);
    }
    return player_colour_name(static_cast<std::size_t>(it - banners.begin()));
}

}  // namespace

void MapViewer::editor_players_model(EditorModel& m) const {
    const sim::World& world = m_editor->document.world();
    for (const sim::PlayerId slot : editor::player_slots(world)) {
        const sim::Player& p = world.players()[slot];
        EditorPlayer e;
        e.slot = slot;
        e.name = player_name(p);
        e.own_name = p.name;
        e.human = p.role == sim::PlayerRole::Human;
        e.banner = p.banner;
        e.money = p.money;
        e.monuments_allowed = p.monuments_allowed;
        e.arches_allowed = p.arches_allowed;
        e.colour = colour_name(p.banner);
        m.player_slots.push_back(std::move(e));
    }
    for (EditorPlayer& e : m.player_slots) {
        for (const EditorPlayer& other : m.player_slots) {
            const editor::Relations r = editor::relations(world, e.slot, other.slot);
            e.relations.push_back(EditorPlayer::Standing{r.peace, r.trade});
        }
    }
    m.can_add_player = m.player_slots.size() < sim::kMaxPlayers;
    m.can_remove_player = !m.player_slots.empty() && m.player_slots.back().slot >= sim::kOriginalPlayers;
}

void MapViewer::editor_players_request(const EditorRequest& request) {
    const sim::World& world = m_editor->document.world();
    using Kind = EditorRequest::Kind;
    if (request.kind == Kind::AddPlayer || request.kind == Kind::RemovePlayer) {
        auto op = request.kind == Kind::AddPlayer ? editor::add_player(world) : editor::remove_last_player(world);
        if (!op) {
            editor_notice(ui::tr(request.kind == Kind::AddPlayer ? "editor.players.refused.add"
                                                                 : "editor.players.refused.remove"),
                          true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        editor_islands_changed();
        editor_notice(ui::tr(request.kind == Kind::AddPlayer ? "editor.players.added" : "editor.players.removed"),
                      false, request.kind == Kind::AddPlayer ? "player added" : "player removed");
        return;
    }
    if (request.kind == Kind::SetRelations) {
        if (request.player.relations.empty()) {
            return;
        }
        const EditorPlayer::Standing s = request.player.relations.front();
        auto op = editor::set_relations(world, request.player.slot, static_cast<sim::PlayerId>(request.index),
                                        editor::Relations{s.peace, s.trade});
        if (!op) {
            editor_notice(ui::tr("editor.relations.refused"), true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        m_editor->notice.clear();
        std::fprintf(stderr, "editor: relations of players %u and %u changed\n",
                     static_cast<unsigned>(request.player.slot), static_cast<unsigned>(request.index));
        return;
    }
    // The slot: the one the request names, or the one the page shows (a name typed).
    const std::vector<sim::PlayerId> slots = editor::player_slots(world);
    if (slots.empty()) {
        return;
    }
    sim::PlayerId slot = request.player.slot;
    if (request.index == 3) {
        const EditorPanel* panel = m_hud ? m_hud->editor_panel() : nullptr;
        slot = slots[std::min(panel != nullptr ? panel->chosen_player() : 0, slots.size() - 1)];
    }
    const sim::Player* p = world.player(slot);
    if (p == nullptr) {
        return;
    }
    editor::PlayerValues v = editor::player_values(*p);
    if (request.index == 3) {
        v.name = request.name;
    } else if (request.index == 1 || request.index == 2) {
        // The next colour, or the one before; one another player has is exchanged with it.
        const auto banners = editor::player_banners();
        const auto it = std::find(banners.begin(), banners.end(), v.banner);
        const std::size_t at = it == banners.end() ? 0 : static_cast<std::size_t>(it - banners.begin());
        v.banner = banners[request.index == 1 ? (at + 1) % banners.size() : (at + banners.size() - 1) % banners.size()];
    } else {
        v.role = request.player.human ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        v.money = std::clamp(request.player.money, editor::kMinStartMoney, editor::kMaxStartMoney);
        v.monuments_allowed = request.player.monuments_allowed;
        v.arches_allowed = request.player.arches_allowed;
    }
    auto op = editor::set_player(world, slot, v);
    if (!op) {
        editor_notice(ui::tr("editor.players.refused.change"), true, op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
    m_editor->notice.clear();
    std::fprintf(stderr, "editor: player %u changed\n", static_cast<unsigned>(slot));
}

}  // namespace o1602::app
