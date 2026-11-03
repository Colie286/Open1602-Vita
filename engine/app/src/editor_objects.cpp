// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's buildings (ADR 0013): the page "Buildings" places the build menu's buildings for a
// player without costs and takes buildings away, each click or drag one step of undo.

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "map_viewer.hpp"
#include "names.hpp"

#include "o1602/assets/text_table.hpp"
#include "o1602/editor/objects.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/editor/terrain.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// The build menu's categories that list buildings (not the demolish tool), in the menu's order.
std::vector<const ui::BuildCategory*> object_groups() {
    std::vector<const ui::BuildCategory*> groups;
    for (const ui::BuildCategory& c : ui::build_menu().categories) {
        if (!c.demolish && c.size() > 0) {
            groups.push_back(&c);
        }
    }
    return groups;
}

/// The players a building can be placed for: humans and computers, as the panel counts them.
std::vector<sim::PlayerId> object_owners(const sim::World& world) {
    std::vector<sim::PlayerId> owners;
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const sim::PlayerRole role = world.players()[i].role;
        if (role == sim::PlayerRole::Human || role == sim::PlayerRole::Computer) {
            owners.push_back(static_cast<sim::PlayerId>(i));
        }
    }
    return owners;
}

/// Why a building is not placed, in the player's language.
std::string build_refusal(sim::BuildRefusal refusal) {
    using R = sim::BuildRefusal;
    switch (refusal) {
        case R::OutsideIsland:
            return ui::tr("editor.objects.refused.island");
        case R::Terrain:
        case R::Overlap:
        case R::UnitInTheWay:
        case R::Infertile:
            return ui::tr("editor.objects.refused.terrain");
        case R::Territory:
            return ui::tr("editor.objects.refused.territory");
        case R::ForeignCity:
            return ui::tr("editor.objects.refused.foreign");
        case R::CityLimit:
            return ui::tr("editor.objects.refused.city_limit");
        case R::SecondKontor:
            return ui::tr("editor.objects.refused.second_kontor");
        case R::NoCoast:
            return ui::tr("editor.objects.refused.coast");
        case R::NoDeposit:
        case R::NoRock:
            return ui::tr("editor.objects.refused.mine");
        case R::NoRiver:
            return ui::tr("editor.objects.refused.river");
        default:
            return ui::tr("editor.objects.refused.other", {sim::refusal_name(refusal)});
    }
}

}  // namespace

const assets::BuildingType* MapViewer::editor_object_piece() const {
    if (!m_editor) {
        return nullptr;
    }
    const auto groups = object_groups();
    if (groups.empty()) {
        return nullptr;
    }
    const ui::BuildCategory& c = *groups[m_editor->objects.group % groups.size()];
    const auto id = c.at(std::min(m_editor->objects.piece, c.size() - 1));
    return id ? m_buildings.by_id(*id) : nullptr;
}

void MapViewer::editor_object_model(EditorModel& m) const {
    m.objects = m_editor->objects;
    const auto groups = object_groups();
    m.object_groups = groups.size();
    if (!groups.empty()) {
        const ui::BuildCategory& c = *groups[m_editor->objects.group % groups.size()];
        m.object_pieces = c.size();
        const std::size_t index = std::min(m_editor->objects.piece, c.size() - 1);
        const std::vector<std::string> none;
        const assets::TextTable* texts = m_hud ? m_hud->texts() : nullptr;
        std::string name = texts != nullptr ? c.name(index, texts->section(c.names)) : c.name(index, none);
        if (name.empty()) {
            name = ui::tr("editor.prop.building_number", {std::to_string(c.at(index).value_or(0))});
        }
        m.object_name = std::move(name);
        m.object_group = ui::tr("editor.objects.group." + std::to_string(c.button));
    }
    if (const assets::BuildingType* b = editor_object_piece()) {
        m.object_turns = b->rotate > 0 || b->size_x != b->size_y;
    }
    const auto owners = object_owners(m_editor->document.world());
    if (!owners.empty()) {
        const sim::Player* p = m_editor->document.world().player(owners[m_editor->objects.owner % owners.size()]);
        m.object_owner = p != nullptr ? player_name(*p) : std::string("-");
    }
}

void MapViewer::editor_objects_request(const EditorRequest& request) {
    EditorObjects o = request.objects;
    o.rotation &= 3U;
    m_editor->objects = o;
    m_editor->notice.clear();
}

std::optional<sim::BuildOrder> MapViewer::editor_object_order(std::int64_t x, std::int64_t y) const {
    const sim::World& world = m_state->world;
    const assets::BuildingType* b = editor_object_piece();
    const auto owners = object_owners(world);
    if (b == nullptr || owners.empty() || x < 0 || y < 0 || x >= world.width() || y >= world.height()) {
        return std::nullopt;
    }
    const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (c.island == sim::World::kSea) {
        return std::nullopt;
    }
    std::uint32_t fw = 0;
    std::uint32_t fh = 0;
    sim::rotated_size(*b, m_editor->objects.rotation, fw, fh);
    sim::BuildOrder order;
    order.player = owners[m_editor->objects.owner % owners.size()];
    order.island = c.island;
    order.free = true;
    // The footprint about the mouse, as the build tool lays it.
    const auto item = [&](std::uint8_t rotation, std::int32_t dx, std::int32_t dy) {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        sim::rotated_size(*b, rotation, w, h);
        return sim::BuildItem{b->id, static_cast<std::int32_t>(c.local_x) - static_cast<std::int32_t>((w - 1) / 2) + dx,
                              static_cast<std::int32_t>(c.local_y) - static_cast<std::int32_t>((h - 1) / 2) + dy,
                              rotation, 0};
    };
    order.items.push_back(item(m_editor->objects.rotation, 0, 0));
    // A building that needs a coast, river or rock face where it stands exactly (a Kontor, a
    // shipyard, a mill, a mine): the nearest place and turn under the mouse where it fits.
    const sim::BuildRefusal refused = sim::plan_build(world, m_buildings, order).items.front().refusal;
    if (refused == sim::BuildRefusal::NoCoast || refused == sim::BuildRefusal::NoRiver ||
        refused == sim::BuildRefusal::NoRock) {
        const auto half = static_cast<std::int32_t>(std::max(fw, fh) / 2);
        for (std::int32_t d = 0; d <= half; ++d) {
            for (std::int32_t dy = -d; dy <= d; ++dy) {
                for (std::int32_t dx = -d; dx <= d; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != d) {
                        continue;
                    }
                    for (std::uint8_t r = 0; r < 4; ++r) {
                        sim::BuildOrder tried = order;
                        tried.items.front() =
                            item(static_cast<std::uint8_t>((m_editor->objects.rotation + r) & 3U), dx, dy);
                        if (sim::plan_build(world, m_buildings, tried).placed > 0) {
                            return tried;
                        }
                    }
                }
            }
        }
    }
    return order;
}

bool MapViewer::editor_place_object(std::int64_t x, std::int64_t y, bool tell) {
    const auto order = editor_object_order(x, y);
    if (!order) {
        if (tell) {
            const bool owners = !object_owners(m_state->world).empty();
            editor_notice(ui::tr(owners ? "editor.objects.refused.island" : "editor.objects.note.no_players"), true,
                          "building not placed");
        }
        return false;
    }
    editor::PlacedBuildings placed = editor::place_buildings(m_state->world, m_buildings, *order, m_city_names);
    if (!placed.step) {
        if (tell && !placed.plan.items.empty()) {
            editor_notice(build_refusal(placed.plan.items.front().refusal), true, "building refused");
        }
        return false;
    }
    placed.step->apply(m_state->world);
    m_editor->object_steps.push_back(std::move(placed.step));
    return true;
}

bool MapViewer::editor_objects_stroke(std::optional<render::TilePos> tile, Stroke phase) {
    switch (phase) {
        case Stroke::Begin: {
            editor_end_objects();
            if (!tile) {
                return true;
            }
            if (m_editor->objects.tool == EditorObjects::Tool::Remove) {
                const sim::World& world = m_state->world;
                const sim::World::Cell& c =
                    world.cell(static_cast<std::uint32_t>(tile->x), static_cast<std::uint32_t>(tile->y));
                if (c.island == sim::World::kSea) {
                    editor_notice(ui::tr("editor.objects.refused.nothing"), true, "nothing to take away");
                    return true;
                }
                auto op = editor::remove_building(
                    world, m_buildings, c.island,
                    sim::TilePos{static_cast<std::int32_t>(c.local_x), static_cast<std::int32_t>(c.local_y)});
                if (!op) {
                    editor_notice(
                        ui::tr(op.error().code == core::ErrorCode::NotFound ? "editor.objects.refused.nothing"
                                                                            : "editor.objects.refused.remove"),
                        true, op.error().message);
                    return true;
                }
                m_editor->document.perform(std::move(op).value());
                editor_islands_changed();
                m_editor->notice.clear();
                return true;
            }
            m_editor->object_at = {tile->x, tile->y};
            m_editor->object_drag = true;
            (void)editor_place_object(tile->x, tile->y, true);
            if (!m_editor->object_steps.empty()) {
                editor_islands_changed();
            }
            return true;
        }
        case Stroke::Continue: {
            if (!m_editor->object_drag || !tile || !m_editor->object_at) {
                return m_editor->object_drag;
            }
            const assets::BuildingType* b = editor_object_piece();
            if (b == nullptr) {
                return true;
            }
            std::uint32_t fw = 0;
            std::uint32_t fh = 0;
            sim::rotated_size(*b, m_editor->objects.rotation, fw, fh);
            const auto [lx, ly] = *m_editor->object_at;
            // Dragged: the next one where it no longer covers the last, along the way.
            bool placed = false;
            for (const auto& [px, py] : editor::line_tiles(lx, ly, tile->x, tile->y)) {
                const auto [ax, ay] = *m_editor->object_at;
                if (std::abs(px - ax) < static_cast<std::int64_t>(fw) &&
                    std::abs(py - ay) < static_cast<std::int64_t>(fh)) {
                    continue;
                }
                m_editor->object_at = {px, py};
                placed = editor_place_object(px, py, false) || placed;
            }
            if (placed) {
                editor_islands_changed();
            }
            return true;
        }
        case Stroke::End:
            editor_end_objects();
            return true;
    }
    return false;
}

void MapViewer::editor_end_objects() {
    if (!m_editor) {
        return;
    }
    m_editor->object_drag = false;
    m_editor->object_at.reset();
    if (m_editor->object_steps.empty()) {
        return;
    }
    if (m_editor->object_steps.size() == 1) {
        m_editor->document.record(std::move(m_editor->object_steps.front()));
    } else {
        m_editor->document.record(std::make_unique<editor::Steps>(std::move(m_editor->object_steps), "building_place"));
    }
    m_editor->object_steps.clear();
    m_editor->notice.clear();
}

}  // namespace o1602::app
