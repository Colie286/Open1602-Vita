// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/islands.hpp"

#include <algorithm>
#include <utility>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/editor/document.hpp"
#include "o1602/sim/island_shapes.hpp"
#include "o1602/sim/island_templates.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world_generator.hpp"

namespace o1602::editor {

namespace {

using core::ErrorCode;
using core::make_error;

/// Every new island grows grain, grass, trees and fish, as the world generator's (bits from the
/// first crop on).
constexpr std::uint32_t kNewFertility = 0x1181;

/// The first island of an island file of the original (a template, an .scp file).
core::Result<assets::Island> first_island(const assets::ChunkFile& file, const std::string& name) {
    auto islands = assets::load_islands(file);
    if (!islands) {
        return islands.error();
    }
    if (islands->empty() || islands->front().layers.empty()) {
        return make_error(ErrorCode::InvalidFormat, "island file without tiles: " + name);
    }
    return std::move(islands->front());
}

/// What a game has made of an island goes: who knows it, a drought, its place.
void clear_game_state(assets::IslandHeader& header) {
    header.pos_x = 0;
    header.pos_y = 0;
    header.explored = 0;
    header.discovered = 0;
    header.drought_strength = 0;
    header.drought_end = 0;
    header.curse_pending = false;
    for (auto& deposit : header.deposits) {
        deposit.mask = 0;
    }
}

core::Result<std::unique_ptr<Operation>> edit(sim::World after, std::string_view kind) {
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), kind));
}

}  // namespace

WorldEdit::WorldEdit(sim::World after, std::string_view kind)
    : m_other(std::move(after)), m_kind(kind), m_memory(sizeof(*this) + world_memory(m_other)) {}

void WorldEdit::apply(sim::World& world) {
    std::swap(world, m_other);
}

void WorldEdit::revert(sim::World& world) {
    std::swap(world, m_other);
}

void FertilityEdit::apply(sim::World& world) {
    if (m_island < world.islands().size()) {
        world.island(m_island).header.fertility = m_after;
    }
}

void FertilityEdit::revert(sim::World& world) {
    if (m_island < world.islands().size()) {
        world.island(m_island).header.fertility = m_before;
    }
}

core::Result<std::unique_ptr<Operation>> set_fertility(const sim::World& world, std::size_t index,
                                                       std::uint32_t fertility) {
    if (index >= world.islands().size()) {
        return make_error(ErrorCode::NotFound, "no island " + std::to_string(index + 1));
    }
    const std::uint32_t before = world.islands()[index].header.fertility;
    if (before == fertility) {
        return make_error(ErrorCode::InvalidFormat, "the island grows these crops already");
    }
    return std::unique_ptr<Operation>(std::make_unique<FertilityEdit>(index, before, fertility));
}

std::size_t PlanEdit::memory() const noexcept {
    const auto size = [](const std::optional<sim::PlannedIslands>& p) {
        return p ? p->plan.islands.size() * sizeof(assets::ScenarioPlan::Island) : std::size_t{0};
    };
    return sizeof(*this) + size(m_before) + size(m_after);
}

bool placeholder_open(const sim::World& world, std::size_t index) noexcept {
    const auto& planned = world.planned_islands();
    if (!planned || index >= planned->plan.islands.size()) {
        return false;
    }
    const std::uint16_t number = planned->plan.islands[index].number;
    return std::none_of(world.islands().begin(), world.islands().end(),
                        [&](const sim::WorldIsland& i) { return i.header.number == number; });
}

std::pair<std::uint32_t, std::uint32_t> placeholder_size(const assets::ScenarioPlan::Island& island) noexcept {
    const std::size_t size = std::min<std::size_t>(island.size, sim::templates::kSizeClasses - 1);
    return {sim::templates::kClassWidth[size], sim::templates::kClassHeight[size]};
}

namespace {

bool sea_free(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
              std::optional<std::size_t> except);
bool on_placeholder(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
                    std::optional<std::size_t> except);

bool overlaps(std::int64_t ax, std::int64_t ay, std::int64_t aw, std::int64_t ah, std::int64_t bx, std::int64_t by,
              std::int64_t bw, std::int64_t bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/// The world with its plan as `planned`, if the plan holds together in it.
core::Result<std::unique_ptr<Operation>> plan_edit(const sim::World& world, sim::PlannedIslands planned,
                                                   std::string_view kind) {
    if (auto problems = sim::check_planned_islands(planned, world.width(), world.height()); !problems.empty()) {
        return make_error(ErrorCode::InvalidFormat, "world plan: " + problems.front());
    }
    return std::unique_ptr<Operation>(std::make_unique<PlanEdit>(world.planned_islands(), std::move(planned), kind));
}

}  // namespace

std::optional<std::size_t> placeholder_at(const sim::World& world, std::uint32_t x, std::uint32_t y) noexcept {
    const auto& planned = world.planned_islands();
    if (!planned) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < planned->plan.islands.size(); ++i) {
        const auto& e = planned->plan.islands[i];
        const auto [w, h] = placeholder_size(e);
        if (placeholder_open(world, i) && overlaps(x, y, 1, 1, e.x, e.y, w, h)) {
            return i;
        }
    }
    return std::nullopt;
}

bool free_for_placeholder(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
                          std::optional<std::size_t> except) {
    return sea_free(world, x, y, w, h, std::nullopt) && !on_placeholder(world, x, y, w, h, except);
}

namespace {

/// Whether the rectangle overlaps an open placeholder other than `except`.
bool on_placeholder(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
                    std::optional<std::size_t> except) {
    const auto& planned = world.planned_islands();
    if (!planned) {
        return false;
    }
    for (std::size_t i = 0; i < planned->plan.islands.size(); ++i) {
        const auto& e = planned->plan.islands[i];
        const auto [pw, ph] = placeholder_size(e);
        if ((!except || *except != i) && placeholder_open(world, i) && overlaps(x, y, w, h, e.x, e.y, pw, ph)) {
            return true;
        }
    }
    return false;
}

}  // namespace

core::Result<std::unique_ptr<Operation>> add_placeholder(const sim::World& world, std::uint8_t climate,
                                                         std::uint8_t size, std::uint32_t x, std::uint32_t y) {
    const auto& planned = world.planned_islands();
    if (!planned) {
        return make_error(ErrorCode::InvalidFormat, "the world has no plan of islands made at the start");
    }
    if (planned->plan.islands.size() >= sim::PlannedIslands::kMaxIslands) {
        return make_error(ErrorCode::LimitExceeded, "the plan holds no more islands");
    }
    // The smallest number neither an island nor a placeholder has (the plan keeps one byte).
    std::uint16_t number = 0;
    const auto taken = [&](std::uint16_t n) {
        return std::any_of(world.islands().begin(), world.islands().end(),
                           [&](const sim::WorldIsland& i) { return i.header.number == n; }) ||
               std::any_of(planned->plan.islands.begin(), planned->plan.islands.end(),
                           [&](const assets::ScenarioPlan::Island& e) { return e.number == n; });
    };
    while (number < 256 && taken(number)) {
        ++number;
    }
    if (number >= 256) {
        return make_error(ErrorCode::LimitExceeded, "no island number is free");
    }
    assets::ScenarioPlan::Island island;
    island.climate = std::min<std::uint8_t>(climate, assets::ScenarioPlan::kClimateByPosition);
    island.size = std::min<std::uint8_t>(size, sim::templates::kSizeClasses - 1);
    island.number = static_cast<std::uint8_t>(number);
    island.x = x;
    island.y = y;
    const auto [w, h] = placeholder_size(island);
    if (!free_for_placeholder(world, x, y, w, h)) {
        return make_error(ErrorCode::InvalidFormat, "the place is not free");
    }
    sim::PlannedIslands after = *planned;
    after.plan.islands.push_back(island);
    return plan_edit(world, std::move(after), "placeholder_new");
}

core::Result<std::unique_ptr<Operation>> change_placeholder(const sim::World& world, std::size_t index,
                                                            const assets::ScenarioPlan::Island& changed) {
    if (!placeholder_open(world, index)) {
        return make_error(ErrorCode::NotFound, "no placeholder " + std::to_string(index + 1));
    }
    const auto& planned = *world.planned_islands();
    if (planned.plan.islands[index] == changed) {
        return make_error(ErrorCode::InvalidFormat, "the placeholder is so already");
    }
    const auto [w, h] = placeholder_size(changed);
    if (!free_for_placeholder(world, changed.x, changed.y, w, h, index)) {
        return make_error(ErrorCode::InvalidFormat, "the place is not free");
    }
    sim::PlannedIslands after = planned;
    const bool moved = after.plan.islands[index].x != changed.x || after.plan.islands[index].y != changed.y;
    after.plan.islands[index] = changed;
    return plan_edit(world, std::move(after), moved ? "placeholder_move" : "placeholder");
}

core::Result<std::unique_ptr<Operation>> remove_placeholder(const sim::World& world, std::size_t index) {
    if (!placeholder_open(world, index)) {
        return make_error(ErrorCode::NotFound, "no placeholder " + std::to_string(index + 1));
    }
    sim::PlannedIslands after = *world.planned_islands();
    after.plan.islands.erase(after.plan.islands.begin() + static_cast<std::ptrdiff_t>(index));
    return plan_edit(world, std::move(after), "placeholder_remove");
}

std::size_t world_memory(const sim::World& world) noexcept {
    std::size_t n = sizeof(sim::World) + std::size_t{world.width()} * world.height() * sizeof(sim::World::Cell);
    for (const sim::WorldIsland& island : world.islands()) {
        const std::size_t tiles = std::size_t{island.map.width()} * island.map.height();
        n += sizeof(island) + 2 * tiles * sizeof(sim::MapCell) +
             std::size_t{island.map.blocks_x()} * island.map.blocks_y() * sizeof(std::uint64_t);
    }
    return n;
}

core::Result<sim::WorldIsland> make_island(const NewIsland& request, std::uint16_t number,
                                           const assets::BuildingTable& buildings, const assets::GameDirectory* game,
                                           std::vector<std::string>* warnings) {
    const std::uint8_t climate = request.climate & 1U;
    const std::uint8_t size = std::min<std::uint8_t>(request.size, sim::templates::kSizeClasses - 1);
    assets::Island made;
    if (request.shape == NewIsland::Shape::Own) {
        sim::shapes::ShapeRequest r;
        r.width = static_cast<std::uint16_t>(sim::templates::kClassWidth[size]);
        r.height = static_cast<std::uint16_t>(sim::templates::kClassHeight[size]);
        r.climate = climate;
        r.size = size;
        r.number = static_cast<std::uint8_t>(number);
        // Ore mountains as the templates of the class have them: a big template one big and two
        // small ones.
        const std::uint32_t area = std::uint32_t{r.width} * r.height;
        r.big_mountains = static_cast<std::uint8_t>(area / 3500);
        r.small_mountains = static_cast<std::uint8_t>((area + 1000) / 2000);
        r.seed = request.seed;
        made = sim::shapes::make_island(r);
    } else {
        if (game == nullptr) {
            return make_error(ErrorCode::NotFound, "the original's island templates are not there");
        }
        const std::uint16_t count = sim::templates::kTemplateCount[climate][size];
        const auto template_number = static_cast<std::uint16_t>(request.seed % std::max<std::uint16_t>(count, 1));
        const std::string path = sim::templates::template_path(climate, size, template_number, false);
        const auto resolved = game->resolve(path);
        if (!resolved) {
            return make_error(ErrorCode::NotFound, "island template not found: " + path);
        }
        auto file = assets::ChunkFile::load(*resolved);
        if (!file) {
            return file.error();
        }
        auto first = first_island(*file, path);
        if (!first) {
            return first.error();
        }
        made = std::move(first).value();
        made.header.template_number = template_number;
        made.header.template_size = size;
    }
    made.header.number = number;
    made.header.climate = climate;
    made.header.pos_x = 0;
    made.header.pos_y = 0;
    made.header.deposits.clear();
    made.header.craters.clear();
    made.header.fertility = kNewFertility;
    made.header.explored = 0;
    made.header.discovered = 0;
    made.header.drought_strength = 0;
    made.header.drought_end = 0;
    made.header.curse_pending = false;
    made.header.volcano_erupted = false;
    made.header.stored_complete = true;
    sim::WorldIsland island{made.header, sim::IslandMap::from_island(made, buildings, warnings, nullptr), 0, 0};
    island.header.width = static_cast<std::uint16_t>(island.map.width());
    island.header.height = static_cast<std::uint16_t>(island.map.height());
    island.deer_phase = made.header.deer_phase;
    island.last_phase = made.header.last_phase;
    return island;
}

namespace {

/// In the map and on open sea, or on the island `except`.
bool sea_free(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
              std::optional<std::size_t> except) {
    if (x < 0 || y < 0 || x + w > world.width() || y + h > world.height() || w == 0 || h == 0) {
        return false;
    }
    for (std::uint32_t dy = 0; dy < h; ++dy) {
        for (std::uint32_t dx = 0; dx < w; ++dx) {
            const sim::World::Cell& c =
                world.cell(static_cast<std::uint32_t>(x) + dx, static_cast<std::uint32_t>(y) + dy);
            if (c.island != sim::World::kSea && (!except || c.island != *except)) {
                return false;
            }
        }
    }
    return true;
}
}  // namespace

bool free_place(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
                std::optional<std::size_t> except) {
    return sea_free(world, x, y, w, h, except) && !on_placeholder(world, x, y, w, h, std::nullopt);
}

bool ship_in_way(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
                 std::optional<std::size_t> except) {
    const auto in = [](const sim::Ship& ship, std::int64_t rx, std::int64_t ry, std::int64_t rw, std::int64_t rh) {
        return ship.x >= rx && ship.y >= ry && ship.x < rx + rw && ship.y < ry + rh;
    };
    const sim::WorldIsland* own = except && *except < world.islands().size() ? &world.islands()[*except] : nullptr;
    return std::any_of(world.ships().begin(), world.ships().end(), [&](const sim::Ship& ship) {
        if (own != nullptr && in(ship, own->x, own->y, own->map.width(), own->map.height())) {
            return false;
        }
        return in(ship, x, y, w, h);
    });
}

core::Result<std::unique_ptr<Operation>> add_island(const sim::World& world, sim::WorldIsland island, std::uint32_t x,
                                                    std::uint32_t y, const assets::BuildingTable& buildings) {
    const bool taken = std::any_of(world.islands().begin(), world.islands().end(),
                                   [&](const sim::WorldIsland& i) { return i.header.number == island.header.number; });
    if (taken) {
        const auto number = world.free_island_number();
        if (!number) {
            return make_error(ErrorCode::LimitExceeded, "no island number is free");
        }
        island.header.number = *number;
    }
    island.x = x;
    island.y = y;
    island.header.pos_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(x, 0xFFFF));
    island.header.pos_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(y, 0xFFFF));
    sim::World after = world;
    if (auto status = after.add_island(std::move(island), &buildings); !status) {
        return status.error();
    }
    return edit(std::move(after), "island_new");
}

core::Result<std::unique_ptr<Operation>> grow_island(const sim::World& world, std::size_t index, std::uint32_t left,
                                                     std::uint32_t top, std::uint32_t right, std::uint32_t bottom,
                                                     const assets::BuildingTable& buildings) {
    const assets::BuildingType* sea = buildings.by_id(assets::id::island_shape::kDarkSea);
    if (sea == nullptr) {
        return make_error(ErrorCode::NotFound, "no open sea in the buildings");
    }
    sim::World after = world;
    if (auto status = after.grow_island(index, left, top, right, bottom, *sea, &buildings); !status) {
        return status.error();
    }
    return edit(std::move(after), "island_grow");
}

core::Result<std::unique_ptr<Operation>> add_sea_island(const sim::World& world, std::uint32_t x, std::uint32_t y,
                                                        std::uint32_t width, std::uint32_t height, std::uint8_t climate,
                                                        const assets::BuildingTable& buildings) {
    const assets::BuildingType* sea = buildings.by_id(assets::id::island_shape::kDarkSea);
    if (sea == nullptr) {
        return make_error(ErrorCode::NotFound, "no open sea in the buildings");
    }
    const auto number = world.free_island_number();
    if (!number) {
        return make_error(ErrorCode::LimitExceeded, "no island number is free");
    }
    if (width == 0 || height == 0 || width > sim::shapes::kMaxSide || height > sim::shapes::kMaxSide) {
        return make_error(ErrorCode::OutOfRange, "an island of that size cannot be made");
    }
    sim::IslandMap map(width, height);
    const std::uint32_t phases = std::max<std::uint32_t>(sea->anim_count, 1);
    for (std::uint32_t ty = 0; ty < height; ++ty) {
        for (std::uint32_t tx = 0; tx < width; ++tx) {
            sim::MapCell cell;
            cell.sprite = static_cast<std::uint16_t>(sea->gfx);
            cell.anim_phase = static_cast<std::uint8_t>((3U * (x + tx) + y + ty) % phases & 0xFU);
            map.set_top_cell(tx, ty, cell);
            map.set_ground_cell(tx, ty, cell);
        }
    }
    assets::IslandHeader header;
    header.chunk_name = "INSEL5";
    header.number = *number;
    header.width = static_cast<std::uint16_t>(width);
    header.height = static_cast<std::uint16_t>(height);
    header.climate = static_cast<std::uint8_t>(climate & 1U);
    header.fertility = kNewFertility;
    header.stored_complete = true;
    return add_island(world, sim::WorldIsland{header, std::move(map), 0, 0}, x, y, buildings);
}

core::Result<std::unique_ptr<Operation>> move_island(const sim::World& world, std::size_t index, std::uint32_t x,
                                                     std::uint32_t y, const assets::BuildingTable& buildings) {
    sim::World after = world;
    if (auto status = after.move_island(index, x, y, &buildings); !status) {
        return status.error();
    }
    return edit(std::move(after), "island_move");
}

core::Result<std::unique_ptr<Operation>> rotate_island(const sim::World& world, std::size_t index,
                                                       const assets::BuildingTable& buildings) {
    sim::World after = world;
    if (auto status = after.rotate_island(index, &buildings); !status) {
        return status.error();
    }
    return edit(std::move(after), "island_turn");
}

core::Result<std::unique_ptr<Operation>> copy_island(const sim::World& world, std::size_t index, std::uint32_t x,
                                                     std::uint32_t y, const assets::BuildingTable& buildings) {
    const auto number = world.free_island_number();
    if (!number) {
        return make_error(ErrorCode::LimitExceeded, "no island number is free");
    }
    sim::World after = world;
    if (auto status = after.copy_island(index, x, y, *number, &buildings); !status) {
        return status.error();
    }
    return edit(std::move(after), "island_copy");
}

core::Result<std::unique_ptr<Operation>> remove_island(const sim::World& world, std::size_t index,
                                                       const assets::BuildingTable& buildings) {
    sim::World after = world;
    if (auto status = after.remove_island(index, &buildings); !status) {
        return status.error();
    }
    return edit(std::move(after), "island_remove");
}

std::pair<std::uint32_t, std::uint32_t> used_size(const sim::World& world) noexcept {
    std::uint64_t w = 1;
    std::uint64_t h = 1;
    const auto take = [&](std::uint64_t x, std::uint64_t y, std::uint64_t width, std::uint64_t height) {
        w = std::max(w, x + width);
        h = std::max(h, y + height);
    };
    for (const sim::WorldIsland& island : world.islands()) {
        take(island.x, island.y, island.map.width(), island.map.height());
    }
    for (const sim::Ship& ship : world.ships()) {
        take(ship.x, ship.y, 1, 1);
    }
    for (const sim::Soldier& soldier : world.soldiers()) {
        take(static_cast<std::uint64_t>(std::max<std::int64_t>(soldier.pos_x, 0)) / 4096,
             static_cast<std::uint64_t>(std::max<std::int64_t>(soldier.pos_y, 0)) / 4096, 1, 1);
    }
    for (const sim::ScenarioMarker& marker : world.markers()) {
        take(marker.x, marker.y, marker.width, marker.height);
    }
    if (const auto& planned = world.planned_islands()) {
        for (std::size_t i = 0; i < planned->plan.islands.size(); ++i) {
            if (placeholder_open(world, i)) {
                const auto [pw, ph] = placeholder_size(planned->plan.islands[i]);
                take(planned->plan.islands[i].x, planned->plan.islands[i].y, pw, ph);
            }
        }
    }
    return {static_cast<std::uint32_t>(std::min<std::uint64_t>(w, sim::World::kMaxSide)),
            static_cast<std::uint32_t>(std::min<std::uint64_t>(h, sim::World::kMaxSide))};
}

core::Result<core::Bytes> save_island(const sim::World& world, std::size_t index,
                                      const assets::BuildingTable& buildings) {
    if (index >= world.islands().size()) {
        return make_error(ErrorCode::NotFound, "no island " + std::to_string(index + 1));
    }
    if (world.island_use(index) != sim::World::IslandUse::None) {
        return make_error(ErrorCode::InvalidFormat, "the island is in use");
    }
    sim::WorldIsland island = world.islands()[index];
    clear_game_state(island.header);
    island.x = 0;
    island.y = 0;
    sim::World alone(island.map.width(), island.map.height());
    if (auto status = alone.add_island(std::move(island), &buildings); !status) {
        return status.error();
    }
    if (auto valid = sim::validate(alone); !valid) {
        return valid.error();
    }
    return sim::write_game(alone, buildings);
}

core::Result<sim::WorldIsland> load_island(std::span<const std::uint8_t> bytes, const assets::BuildingTable& buildings,
                                           const assets::GameDirectory* game, std::vector<std::string>* warnings,
                                           std::uint8_t climate) {
    const auto placed = [](sim::WorldIsland island) {
        clear_game_state(island.header);
        island.x = 0;
        island.y = 0;
        return island;
    };
    if (sim::is_open1602_save(bytes)) {
        auto world = Document::read(bytes, buildings, game, warnings);
        if (!world) {
            return world.error();
        }
        if (world->islands().empty()) {
            return make_error(ErrorCode::InvalidFormat, "the file holds no island");
        }
        return placed(world->islands().front());
    }
    auto file = assets::ChunkFile::from_bytes(core::Bytes(bytes.begin(), bytes.end()));
    if (!file) {
        return file.error();
    }
    auto made = first_island(*file, "island file");
    if (!made) {
        return made.error();
    }
    made->header.stored_complete = true;
    made->header.climate = static_cast<std::uint8_t>(climate & 1U);
    sim::WorldIsland island{made->header, sim::IslandMap::from_island(*made, buildings, warnings, nullptr), 0, 0};
    island.header.width = static_cast<std::uint16_t>(island.map.width());
    island.header.height = static_cast<std::uint16_t>(island.map.height());
    island.deer_phase = made->header.deer_phase;
    island.last_phase = made->header.last_phase;
    return placed(std::move(island));
}

core::Result<std::unique_ptr<Operation>> resize_world(const sim::World& world, std::uint32_t width,
                                                      std::uint32_t height, const assets::BuildingTable& buildings) {
    if (width == world.width() && height == world.height()) {
        return make_error(ErrorCode::InvalidFormat, "the map has this size already");
    }
    sim::World after = world;
    if (auto status = after.resize(width, height, &buildings); !status) {
        return status.error();
    }
    return edit(std::move(after), "world_size");
}

}  // namespace o1602::editor
