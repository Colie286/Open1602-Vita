// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/islands.hpp"

#include <algorithm>
#include <utility>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/sim/island_shapes.hpp"
#include "o1602/sim/island_templates.hpp"

namespace o1602::editor {

namespace {

using core::ErrorCode;
using core::make_error;

/// Every new island grows grain, grass, trees and fish, as the world generator's (bits from the
/// first crop on).
constexpr std::uint32_t kNewFertility = 0x1181;

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
        auto islands = assets::load_islands(*file);
        if (!islands) {
            return islands.error();
        }
        if (islands->empty() || islands->front().layers.empty()) {
            return make_error(ErrorCode::InvalidFormat, "island template without tiles: " + path);
        }
        made = std::move(islands->front());
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

bool free_place(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
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

}  // namespace o1602::editor
