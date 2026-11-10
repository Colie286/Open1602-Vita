// SPDX-License-Identifier: GPL-3.0-or-later
// Soldiers: walking, choosing targets, attacking, dying (docs/re/military.md).
#include <algorithm>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/sea_paths.hpp"

namespace o1602::sim {

namespace {

constexpr std::size_t kMaxSteps = 16;
constexpr std::uint32_t kIdleMs = 1000;        // idle units look again every second
constexpr std::uint32_t kIdleSpread = 8;       // ... spread over 8 steps by unit id
constexpr std::uint32_t kIdleStepMs = 25;      // of 25 ms each
constexpr std::uint32_t kAttackWaitMs = 1000;  // and stand a second after an attack
constexpr std::uint32_t kWanderAfterMs = 2000;
constexpr std::uint32_t kFollowMs = 3000;  // re-plan towards a moving target (soldier + 0x14)
constexpr std::uint32_t kHealMs = 10'000;  // inside the doctor's house
constexpr std::uint32_t kDisbandMs = 1450;
constexpr std::int32_t kPlanRadius = 0x2D;
constexpr std::int32_t kChaseRadius = 0x10;
constexpr std::int32_t kFollowRadius = 0x28;
constexpr std::int32_t kExplicitRange = 18;  // cells: an ordered target this near is attacked
constexpr std::int32_t kChaseRange = 8;      // cells: melee units chase enemies this near
constexpr std::int32_t kMeleeRange8 = 8;
constexpr std::int32_t kFriendlyGoal = 7;  // cells around an object a soldier may stop at
constexpr std::int32_t kDoctorGoal = 1;    // cells from an own doctor: the soldier goes in (0x00456D00)
constexpr std::int32_t kOthersRange = 8;   // cells: other figures block planning within 4 tiles
constexpr std::int32_t kSpread = 6;        // cells: how far a place moves to a free cell
constexpr std::int32_t kCrowded = 3;       // cells: walking slower next to another figure
constexpr std::uint8_t kMaxFailures = 3;
constexpr std::int32_t kWanderRange = 9;  // tiles around home
constexpr std::int32_t kBoardRange = 9;   // cells from an own ship: the soldier goes on board
constexpr std::uint8_t kAnimWalk = 0;
constexpr std::uint8_t kAnimStand = 1;
constexpr std::uint8_t kAnimAttack = 2;
constexpr std::uint8_t kAnimDie = 3;
constexpr std::uint64_t kStraightCellUs = 3'125'000;  // half a tile at Speed 1, Wegspeed 1
constexpr std::uint64_t kDiagonalCellUs = 4'419'417;
constexpr std::int32_t kSquareShift = 4;  // squares of 16 x 16 cells for near_units()

bool computer_controlled(const World& world, PlayerId p) noexcept {
    const Player* player = world.player(p);
    return player != nullptr &&
           (player->role == PlayerRole::Computer || world.behaviour(*player) == FactionBehaviour::Natives ||
            world.behaviour(*player) == FactionBehaviour::Pirates);
}

}  // namespace

void Military::grid_build() {
    const std::vector<Soldier>& all = m_world->soldiers();
    const std::int32_t w = static_cast<std::int32_t>((2 * m_world->width()) >> kSquareShift) + 1;
    const std::int32_t h = static_cast<std::int32_t>((2 * m_world->height()) >> kSquareShift) + 1;
    if (w == m_grid_w && h == m_grid_h && m_grid.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) {
        // Only the squares used since the last build hold units (TB-23): a large world with few
        // units does not clear all its squares every tick.
        for (const std::uint32_t square : m_square_of) {
            m_grid[square].clear();
        }
    } else {
        m_grid_w = w;
        m_grid_h = h;
        m_grid.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), {});
    }
    m_square_of.clear();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const std::int32_t sx = std::clamp(all[i].cell_x() >> kSquareShift, 0, m_grid_w - 1);
        const std::int32_t sy = std::clamp(all[i].cell_y() >> kSquareShift, 0, m_grid_h - 1);
        const auto square = static_cast<std::uint32_t>(sy * m_grid_w + sx);
        m_grid[square].push_back(static_cast<std::uint32_t>(i));
        m_square_of.push_back(square);
    }
    m_grid_data = all.data();
}

bool Military::grid_valid() const noexcept {
    const std::vector<Soldier>& all = m_world->soldiers();
    return m_grid_data != nullptr && m_grid_data == all.data() && m_square_of.size() == all.size();
}

void Military::grid_moved(const Soldier& s) {
    if (!grid_valid()) {
        return;
    }
    const auto i = static_cast<std::uint32_t>(&s - m_grid_data);
    const std::int32_t sx = std::clamp(s.cell_x() >> kSquareShift, 0, m_grid_w - 1);
    const std::int32_t sy = std::clamp(s.cell_y() >> kSquareShift, 0, m_grid_h - 1);
    const auto square = static_cast<std::uint32_t>(sy * m_grid_w + sx);
    if (square == m_square_of[i]) {
        return;
    }
    std::vector<std::uint32_t>& old = m_grid[m_square_of[i]];
    const auto it = std::find(old.begin(), old.end(), i);
    *it = old.back();
    old.pop_back();
    m_grid[square].push_back(i);
    m_square_of[i] = square;
}

template <typename Visit>
void Military::near_units(std::int32_t cx, std::int32_t cy, std::int32_t r, Visit&& visit) const {
    const std::vector<Soldier>& all = m_world->soldiers();
    if (!grid_valid()) {
        for (const Soldier& o : all) {
            if (visit(o)) {
                return;
            }
        }
        return;
    }
    // Clamped like the units' squares, so a unit beyond the edge is still found.
    const std::int32_t x0 = std::clamp((cx - r) >> kSquareShift, 0, m_grid_w - 1);
    const std::int32_t x1 = std::clamp((cx + r) >> kSquareShift, 0, m_grid_w - 1);
    const std::int32_t y0 = std::clamp((cy - r) >> kSquareShift, 0, m_grid_h - 1);
    const std::int32_t y1 = std::clamp((cy + r) >> kSquareShift, 0, m_grid_h - 1);
    for (std::int32_t sy = y0; sy <= y1; ++sy) {
        for (std::int32_t sx = x0; sx <= x1; ++sx) {
            for (const std::uint32_t i : m_grid[static_cast<std::size_t>(sy) * static_cast<std::size_t>(m_grid_w) +
                                                static_cast<std::size_t>(sx)]) {
                if (visit(all[i])) {
                    return;
                }
            }
        }
    }
}

const assets::FigureType* Military::type_of(const Soldier& s) const {
    return m_figures->get(s.figure);
}

std::uint32_t Military::idle_ms(const Soldier& s) noexcept {
    // Spread by unit so that units idle since the same tick do not all look again in one tick (TB-1).
    return kIdleMs + (s.id % kIdleSpread) * kIdleStepMs;
}

void Military::start_wait(Soldier& s, std::uint32_t ms) {
    SoldierMotion& m = s.motion;
    m.phase = SoldierMotion::Phase::Wait;
    m.elapsed_us = 0;
    m.duration_us = std::max<std::uint32_t>(1, ms * 1000U);
}

void Military::rest(Soldier& s) {
    // A unit that neither walks nor fires stands: the walk or the attack it ended with does not go
    // on playing while it waits.
    if (s.anim == kAnimWalk || s.anim == kAnimAttack) {
        s.anim = kAnimStand;
    }
    start_wait(s, idle_ms(s));
}

void Military::advance(Soldier& s, std::uint32_t budget_us) {
    for (std::size_t step = 0; step < kMaxSteps; ++step) {
        SoldierMotion& m = s.motion;
        if (m.phase == SoldierMotion::Phase::Idle) {
            decide(s);
            if (m.phase == SoldierMotion::Phase::Idle) {
                start_wait(s, idle_ms(s));
            }
            continue;
        }
        const std::uint32_t left = m.duration_us > m.elapsed_us ? m.duration_us - m.elapsed_us : 0U;
        const bool done = budget_us >= left;
        m.elapsed_us = done ? m.duration_us : m.elapsed_us + budget_us;
        budget_us = done ? budget_us - left : 0U;
        if (m.phase == SoldierMotion::Phase::Run) {
            const std::int32_t way = static_cast<std::int32_t>(m.count) * kCellUnit;
            const auto part =
                static_cast<std::int32_t>(std::int64_t{way} * m.elapsed_us / std::max<std::uint32_t>(1, m.duration_us));
            s.pos_x = m.from_x + step_x(m.direction) * part;
            s.pos_y = m.from_y + step_y(m.direction) * part;
        }
        if (!done) {
            return;
        }
        if (m.phase == SoldierMotion::Phase::Dying) {
            s.owner = kNoPlayer;  // removed at the end of the tick
            return;
        }
        if (m.phase == SoldierMotion::Phase::Healing) {
            // Back at full health (event type 3 of 0x00478AB0, message 13).
            const assets::FigureType* type = type_of(s);
            s.health = type != nullptr ? type->combat.max_health : 0;
            s.anim = kAnimStand;
            m_events.push_back(MilitaryEvent{
                MilitaryEvent::Kind::SoldierHealed, Target::soldier(s.id), {}, s.owner, s.owner, s.pos_x, s.pos_y});
        }
        m.phase = SoldierMotion::Phase::Idle;
        decide(s);
        if (m.phase == SoldierMotion::Phase::Idle) {
            start_wait(s, idle_ms(s));
        }
        if (budget_us == 0) {
            return;
        }
    }
}

std::uint32_t Military::run_us(const Soldier& s, std::uint8_t direction, std::uint8_t cells) const {
    const assets::FigureType* type = type_of(s);
    const std::uint32_t speed = type != nullptr ? std::max<std::uint32_t>(1, type->speed) : 200U;
    std::uint8_t ways = 32;
    (void)m_combat->land().cell(s.cell_x(), s.cell_y(), type != nullptr ? type->speed_type : 0, &ways);
    std::uint32_t w = ways;
    // Next to another figure with a lower number the soldier walks slower (0x00458770).
    // Whether there is one matters, not which: the order of the units does not.
    bool crowded = false;
    near_units(s.cell_x(), s.cell_y(), kCrowded, [&](const Soldier& o) {
        crowded = o.id < s.id && !o.away() && sea_distance(o.cell_x() - s.cell_x(), o.cell_y() - s.cell_y()) < kCrowded;
        return crowded;
    });
    if (crowded) {
        w = std::min<std::uint32_t>(126, w * 5 / 4);
    }
    const std::uint64_t per_cell = (direction & 1U) != 0 ? kDiagonalCellUs : kStraightCellUs;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(1, per_cell * w * cells / speed));
}

bool Military::start_next_run(Soldier& s) {
    SoldierMotion& m = s.motion;
    if (m.path_index >= m.path.size()) {
        return false;
    }
    const std::uint8_t code = m.path[m.path_index];
    const auto dir = static_cast<std::uint8_t>((code >> 4U) & 7U);
    const auto cells = static_cast<std::uint8_t>(code & 0x0FU);
    // A run starts on a cell centre; soldiers turn at once (Drehtime 0).
    s.place_cell(s.cell_x(), s.cell_y());
    s.direction = dir;
    s.anim = kAnimWalk;
    m.phase = SoldierMotion::Phase::Run;
    m.direction = dir;
    m.count = cells;
    m.from_x = s.pos_x;
    m.from_y = s.pos_y;
    m.elapsed_us = 0;
    m.duration_us = run_us(s, dir, cells);
    ++m.path_index;
    return true;
}

bool Military::target_valid(const Target& t) const {
    switch (t.kind) {
        case Target::Kind::None:
            return false;
        case Target::Kind::Spot:
            return m_world->contains(t.x / 2, t.y / 2);
        case Target::Kind::Position:
            return m_world->contains(t.x, t.y);
        case Target::Kind::Soldier: {
            const Soldier* s = m_world->soldier(t.ship);
            return s != nullptr && !s->away();
        }
        case Target::Kind::Ship: {
            const Ship* s = m_world->ship(t.ship);
            return s != nullptr && !s->sinking;
        }
        case Target::Kind::Building:
            return t.island < m_world->islands().size() &&
                   building_at(*m_world, *m_buildings, t.island,
                               {static_cast<std::int32_t>(t.x), static_cast<std::int32_t>(t.y)})
                       .has_value();
        case Target::Kind::City: {
            const City* c = m_world->city(t.island, t.city);
            return c != nullptr && c->warehouse_x && c->warehouse_y;
        }
    }
    return false;
}

std::optional<CellRect> Military::target_cells(const Target& t) const {
    switch (t.kind) {
        case Target::Kind::Spot: {
            const auto x = static_cast<std::int32_t>(t.x);
            const auto y = static_cast<std::int32_t>(t.y);
            return CellRect{x, y, x, y};
        }
        case Target::Kind::Position: {
            const auto x = static_cast<std::int32_t>(t.x);
            const auto y = static_cast<std::int32_t>(t.y);
            return CellRect{2 * x, 2 * y, 2 * x + 1, 2 * y + 1};
        }
        case Target::Kind::City: {
            const City* c = m_world->city(t.island, t.city);
            if (c == nullptr || !c->warehouse_x || !c->warehouse_y) {
                return std::nullopt;
            }
            return target_cells(Target::building(t.island, *c->warehouse_x, *c->warehouse_y));
        }
        case Target::Kind::Ship: {
            const Ship* s = m_world->ship(t.ship);
            if (s == nullptr) {
                return std::nullopt;
            }
            return CellRect{2 * s->x, 2 * s->y, 2 * s->x + 1, 2 * s->y + 1};
        }
        case Target::Kind::Soldier: {
            const Soldier* s = m_world->soldier(t.ship);
            if (s == nullptr) {
                return std::nullopt;
            }
            return CellRect{s->cell_x(), s->cell_y(), s->cell_x(), s->cell_y()};
        }
        default:
            break;
    }
    if (const auto f = m_combat->fighter(t)) {
        return f->cells;
    }
    return std::nullopt;
}

PlayerId Military::owner_of(const Target& t) const {
    switch (t.kind) {
        case Target::Kind::Soldier: {
            const Soldier* s = m_world->soldier(t.ship);
            return s != nullptr ? s->owner : kNoPlayer;
        }
        case Target::Kind::Ship: {
            const Ship* s = m_world->ship(t.ship);
            return s != nullptr ? s->owner : kNoPlayer;
        }
        case Target::Kind::Building: {
            const auto f = m_combat->fighter(t);
            return f ? f->owner : kNoPlayer;
        }
        case Target::Kind::City: {
            const City* c = m_world->city(t.island, t.city);
            return c != nullptr ? c->owner : kNoPlayer;
        }
        default:
            break;
    }
    return kNoPlayer;
}

bool Military::attackable(const Soldier& s, const Target& t) const {
    // A unit, tower or building of a player at war with the soldier's owner (or with a pending
    // offer), or a tree or field that is not its own. The original also attacks a building of a
    // friendly player it was sent to (S-088).
    if (t.kind != Target::Kind::Soldier && t.kind != Target::Kind::Ship && t.kind != Target::Kind::Building) {
        return false;
    }
    if (t.kind == Target::Kind::Building) {
        // Something standing: not open ground, ruins or what is left of a destroyed Kontor.
        const auto b = building_at(*m_world, *m_buildings, t.island,
                                   {static_cast<std::int32_t>(t.x), static_cast<std::int32_t>(t.y)});
        if (!b || b->type->destroyed || b->type->kind == building_kind::kGround ||
            b->type->kind == building_kind::kRuin || b->type->kind == building_kind::kBeachRuin) {
            return false;
        }
        if (m_combat->fellable(t, s.owner)) {
            return true;
        }
    }
    const PlayerId other = owner_of(t);
    return other != kNoPlayer && other != s.owner && m_world->diplomacy().hostile(s.owner, other);
}

std::optional<std::uint8_t> Military::fire_direction(const Soldier& s, const assets::FigureType& type,
                                                     const Target& t) const {
    // Combat_FireDirection 0x00453E50: in range of the nearest cell of the target, with nothing
    // blocking shots in between.
    const auto cells = target_cells(t);
    if (!cells) {
        return std::nullopt;
    }
    const std::int32_t cx = s.cell_x();
    const std::int32_t cy = s.cell_y();
    if (cells->distance(cx, cy) > static_cast<std::int32_t>(type.combat.range8) / 4) {
        return std::nullopt;
    }
    const auto [nx, ny] = cells->nearest(cx, cy);
    if (!line_of_sight(m_combat->land(), cx, cy, nx, ny, *cells)) {
        return std::nullopt;
    }
    return direction_to(nx - cx, ny - cy);
}

bool Military::attack(Soldier& s, const assets::FigureType& type, const Target& t) {
    const std::uint64_t now = m_world->time_ms();
    if (const auto dir = fire_direction(s, type, t)) {
        s.motion.path.clear();
        s.motion.path_index = 0;
        s.direction = *dir;
        if (now >= s.next_attack_ms) {
            const auto me = m_combat->fighter(Target::soldier(s.id));
            const auto other = m_combat->fighter(t);
            if (me && other) {
                m_combat->shoot(*me, *other, type.combat.hit_delay_ms);
                s.next_attack_ms = now + type.work_time_ms;
                s.last_attack_ms = now;
                s.anim = kAnimAttack;
                m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::Shot, me->unit, t, s.owner, other->owner, s.pos_x,
                                                 s.pos_y, s.figure});
            }
        } else if (s.anim != kAnimAttack) {
            s.anim = kAnimStand;
        }
        start_wait(s, kAttackWaitMs);
        return true;
    }
    return false;
}

bool Military::plan(Soldier& s, std::int32_t radius, bool hostile_goal) {
    return plan_to(s, s.target, radius, hostile_goal);
}

bool Military::plan_to(Soldier& s, const Target& target, std::int32_t radius, bool hostile_goal) {
    const assets::FigureType* type = type_of(s);
    const auto cells = target_cells(target);
    if (type == nullptr || !cells) {
        return false;
    }
    m_plans_left = m_plans_left != 0 ? m_plans_left - 1 : 0;
    LandPlan r;
    r.start_x = s.cell_x();
    r.start_y = s.cell_y();
    r.radius = radius;
    if (s.failures != 0) {
        // After a failure the window moves by half its size in a random diagonal (0x004581F0).
        const std::uint32_t d = m_world->random(RandomStream::Military).next() & 3U;
        r.shift_x = (d & 1U) != 0 ? radius / 2 : -radius / 2;
        r.shift_y = (d & 2U) != 0 ? radius / 2 : -radius / 2;
    }
    r.speed_type = type->speed_type;
    r.max_run = type->combat.max_step;
    r.walker = s.owner;
    // Only a set of cells to the planner: the order of the units does not matter.
    near_units(r.start_x, r.start_y, kOthersRange, [&](const Soldier& o) {
        if (o.id != s.id && !o.away() && sea_distance(o.cell_x() - r.start_x, o.cell_y() - r.start_y) <= kOthersRange) {
            r.blocked.emplace_back(o.cell_x(), o.cell_y());
        }
        return false;
    });
    const auto [ax, ay] = cells->nearest(r.start_x, r.start_y);
    r.aim_x = ax;
    r.aim_y = ay;
    const CellRect rect = *cells;
    const LandMap& land = m_combat->land();
    if (hostile_goal) {
        // Cells from which the target is in range and in sight; the first reached wins.
        const std::int32_t range = static_cast<std::int32_t>(type->combat.range8) / 4;
        r.first_goal = true;
        r.goal = [rect, range, &land](std::int32_t x, std::int32_t y) {
            if (rect.distance(x, y) > range) {
                return false;
            }
            const auto [nx, ny] = rect.nearest(x, y);
            return line_of_sight(land, x, y, nx, ny, rect);
        };
    } else {
        // The object itself or a cell next to it, as near to it as can be. An own doctor only takes the
        // soldier in from the next cell, so the way must end there: the original plans anew until then.
        const std::int32_t goal = friendly_goal(s, target);
        r.goal = [rect, goal](std::int32_t x, std::int32_t y) { return rect.distance(x, y) <= goal; };
    }
    const LandCourse course = m_planner.plan(land, r);
    if (course.path.empty()) {
        return course.reached_goal;
    }
    s.motion.path = course.path;
    s.motion.path_index = 0;
    s.radius = static_cast<std::uint8_t>(radius);
    s.failures = 0;
    return true;
}

bool Military::heals_at(const Soldier& s, const Target& t) const {
    if (t.kind != Target::Kind::Building || owner_of(t) != s.owner) {
        return false;
    }
    const auto b =
        building_at(*m_world, *m_buildings, t.island, {static_cast<std::int32_t>(t.x), static_cast<std::int32_t>(t.y)});
    return b && b->type->prod_kind == prod::kKlinik;
}

std::int32_t Military::friendly_goal(const Soldier& s, const Target& t) const {
    if (heals_at(s, t)) {
        return kDoctorGoal;
    }
    if (t.kind == Target::Kind::Spot) {
        // A place the soldier can stand on is walked onto, however near it is; one that is built
        // over or where another unit stands is near enough from a few cells away.
        const auto x = static_cast<std::int32_t>(t.x);
        const auto y = static_cast<std::int32_t>(t.y);
        bool taken = m_combat->land().cell(x, y, 0) != LandMap::Cell::Walk;
        near_units(x, y, 0, [&](const Soldier& o) {
            taken = taken || (o.id != s.id && !o.away() && o.cell_x() == x && o.cell_y() == y);
            return taken;
        });
        return taken ? kFriendlyGoal : 0;
    }
    return kFriendlyGoal;
}

namespace {

std::uint64_t cell_key(std::int32_t x, std::int32_t y) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32U) | static_cast<std::uint32_t>(x);
}

}  // namespace

std::unordered_set<std::uint64_t> Military::occupied_cells(const std::unordered_set<std::uint32_t>& leaving) const {
    std::unordered_set<std::uint64_t> occupied;
    for (const Soldier& o : m_world->soldiers()) {
        if (o.away() || o.owner == kNoPlayer || leaving.contains(o.id)) {
            continue;
        }
        if (o.target.empty()) {
            occupied.insert(cell_key(o.cell_x(), o.cell_y()));
        } else if (o.target.kind == Target::Kind::Spot) {
            occupied.insert(cell_key(static_cast<std::int32_t>(o.target.x), static_cast<std::int32_t>(o.target.y)));
        }
    }
    return occupied;
}

std::pair<std::int32_t, std::int32_t> Military::free_cell(std::int32_t x, std::int32_t y, std::int32_t from_x,
                                                          std::int32_t from_y,
                                                          const std::unordered_set<std::uint64_t>& occupied) const {
    const LandMap& land = m_combat->land();
    const auto free = [&](std::int32_t cx, std::int32_t cy) {
        return land.cell(cx, cy, 0) == LandMap::Cell::Walk && !occupied.contains(cell_key(cx, cy));
    };
    if (free(x, y)) {
        return {x, y};
    }
    std::pair<std::int32_t, std::int32_t> best{x, y};
    std::int32_t best_d = 0;
    std::int32_t best_from = 0;
    for (std::int32_t dy = -kSpread; dy <= kSpread; ++dy) {
        for (std::int32_t dx = -kSpread; dx <= kSpread; ++dx) {
            const std::int32_t d = sea_distance(dx, dy);
            if (d > 2 * kSpread || !free(x + dx, y + dy)) {
                continue;
            }
            const std::int32_t from = sea_distance(x + dx - from_x, y + dy - from_y);
            if (best_d == 0 || d < best_d || (d == best_d && from < best_from)) {
                best = {x + dx, y + dy};
                best_d = d;
                best_from = from;
            }
        }
    }
    return best;
}

bool Military::arrived(Soldier& s) {
    // Arrival (0x00456D00 step 7): at an own doctor's house the soldier goes in to be healed; at a
    // place or another object it has arrived when its way ends near it.
    const auto cells = target_cells(s.target);
    if (!cells) {
        return false;
    }
    const std::int32_t d = cells->distance(s.cell_x(), s.cell_y());
    if (heals_at(s, s.target)) {
        if (d > kDoctorGoal) {
            return false;
        }
        s.target = {};
        s.motion.phase = SoldierMotion::Phase::Healing;
        s.motion.elapsed_us = 0;
        s.motion.duration_us = kHealMs * 1000U;
        s.motion.path.clear();
        s.motion.path_index = 0;
        return true;
    }
    if (s.target.kind == Target::Kind::Ship) {
        Ship* ship = m_world->ship(s.target.ship);
        if (ship != nullptr && ship->owner == s.owner && !ship->cart()) {  // nobody boards a trade cart
            if (d > kBoardRange) {
                return false;
            }
            if (board(s, *ship)) {
                return true;
            }
        }
    }
    if (d > friendly_goal(s, s.target)) {
        return false;
    }
    if (s.target.kind == Target::Kind::Spot && !s.patrol.active) {
        // Units never stand on one cell (History Edition): one that ended its way where another
        // stands goes on to the free cell nearest it that nobody stands on or is headed to. The
        // original lets units end their ways on one another.
        bool stacked = false;
        near_units(s.cell_x(), s.cell_y(), 0, [&](const Soldier& o) {
            stacked = o.id != s.id && !o.away() && o.owner != kNoPlayer && o.target.empty() &&
                      o.cell_x() == s.cell_x() && o.cell_y() == s.cell_y();
            return stacked;
        });
        if (stacked) {
            const std::unordered_set<std::uint64_t> occupied = occupied_cells({s.id});
            const auto [fx, fy] = free_cell(s.cell_x(), s.cell_y(), s.cell_x(), s.cell_y(), occupied);
            if (fx != s.cell_x() || fy != s.cell_y()) {
                s.target = Target::spot(static_cast<std::uint32_t>(fx), static_cast<std::uint32_t>(fy));
                return true;
            }
        }
    }
    if (s.patrol.active && s.target == s.patrol.points[s.patrol.index]) {
        // Patrol: on to the other point; once only stops after the last (0x00458190).
        if (s.patrol.one_way && s.patrol.index == 1) {
            s.patrol.active = false;
            s.target = {};
        } else {
            s.patrol.index = static_cast<std::uint8_t>(s.patrol.index ^ 1U);
            if (s.patrol.points[s.patrol.index].empty()) {
                s.patrol.index = 0;
            }
            s.target = s.patrol.points[s.patrol.index];
        }
        return true;
    }
    next_queued(s);
    return true;
}

void Military::next_queued(Soldier& s) {
    s.target = {};
    while (!s.queue.empty() && s.target.empty()) {
        const Target t = s.queue.front();
        s.queue.erase(s.queue.begin());
        if (target_valid(t)) {
            s.target = t;
        }
    }
}

bool Military::wander(Soldier& s) {
    // Natives stroll around their home: one chance in four per look, up to five random tiles within
    // 9 of it, on road or open ground (0x00456D00 with 0x00443320/50).
    Random& random = m_world->random(RandomStream::Military);
    if ((random.next() & 3U) != 0 || s.home.empty()) {
        return false;
    }
    const auto home = target_cells(s.home);
    if (!home) {
        return false;
    }
    const std::int32_t hx = home->x0 / 2;
    const std::int32_t hy = home->y0 / 2;
    for (int tries = 0; tries < 5; ++tries) {
        // As the original's helper (Island_RandomX 0x00443320, base + r - 2 * (rand() % r)): only
        // offsets of one parity, from -7 to +9. Kept on purpose for faithful wandering.
        const std::int32_t x = hx + kWanderRange - 2 * static_cast<std::int32_t>(random.below(kWanderRange));
        const std::int32_t y = hy + kWanderRange - 2 * static_cast<std::int32_t>(random.below(kWanderRange));
        const std::uint16_t island = m_combat->land().island_at(x, y);
        if (island == World::kSea || island != s.island) {
            continue;
        }
        const assets::BuildingType* b = m_combat->land().building(x, y);
        if (b == nullptr || (b->kind != building_kind::kRoad && b->kind != building_kind::kGround)) {
            continue;
        }
        // The tile as a place, like the one the warrior first went to: a road or ground tile is no
        // building, so as a building target it had no cells, the way failed and the warrior stood.
        s.target = Target::tile_spot(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        return true;
    }
    return false;
}

void Military::kill(Soldier& s, PlayerId killer, bool disband) {
    if (s.dying()) {
        return;
    }
    const assets::FigureType* type = type_of(s);
    s.motion.phase = SoldierMotion::Phase::Dying;
    s.motion.path.clear();
    s.motion.path_index = 0;
    s.motion.elapsed_us = 0;
    s.motion.duration_us = (disband ? kDisbandMs : (type != nullptr ? type->combat.die_ms : 2500U)) * 1000U;
    s.disbanded = disband;
    s.target = {};
    s.queue.clear();
    if (!disband) {
        s.anim = kAnimDie;
    }
    // The last human or computer player who killed a unit (player + 0x81; the natives' curse),
    // and the score's counters.
    Player* k = m_world->player(killer);
    const bool counts = k != nullptr && (k->role == PlayerRole::Human || k->role == PlayerRole::Computer);
    if (counts) {
        m_world->diplomacy().set_blamed(s.owner, killer);
    }
    if (!disband) {
        if (Player* victim = m_world->player(s.owner)) {
            add_count(victim->soldiers_lost, 1);
        }
        if (counts) {
            add_count(k->soldiers_killed, 1);
        }
    }
    if (!disband) {
        m_events.push_back(MilitaryEvent{
            MilitaryEvent::Kind::SoldierKilled, {}, Target::soldier(s.id), killer, s.owner, s.pos_x, s.pos_y});
    }
}

bool Military::on_other_island(const Soldier& s, const Target& t) const {
    if (s.island == 0xFFFF) {
        return false;
    }
    if (t.kind == Target::Kind::Building) {
        return t.island != s.island;
    }
    if (t.kind == Target::Kind::Soldier) {
        const Soldier* o = m_world->soldier(t.ship);
        return o != nullptr && o->island != 0xFFFF && o->island != s.island;
    }
    return false;
}

void Military::decide(Soldier& s) {
    SoldierMotion& m = s.motion;
    const assets::FigureType* type = type_of(s);
    if (type == nullptr || s.away()) {
        start_wait(s, idle_ms(s));
        return;
    }
    // At the end of a run the soldier stands on a cell centre (0x00456D00 step 1).
    s.place_cell(s.cell_x(), s.cell_y());
    if (const std::uint16_t island = m_combat->land().island_at(s.cell_x() / 2, s.cell_y() / 2);
        island != World::kSea) {
        s.island = island;
    }
    if (m_plans_left == 0) {
        // No way search left in this tick: the unit decides in the next one (TB-1).
        start_wait(s, std::max<std::uint32_t>(1, m_tick_ms));
        return;
    }
    if (!s.target.empty() && !target_valid(s.target)) {
        next_queued(s);
        m.path.clear();
        m.path_index = 0;
    }
    const std::uint64_t now = m_world->time_ms();
    const Target self = Target::soldier(s.id);

    // An ordered target near enough is attacked (step 4).
    Target enemy;
    if (attackable(s, s.target)) {
        if (const auto cells = target_cells(s.target);
            cells && cells->distance(s.cell_x(), s.cell_y()) <= kExplicitRange) {
            enemy = s.target;
        }
    }
    // Otherwise the best enemy in range, or one to chase (step 5).
    std::optional<Target> chase;
    if (enemy.empty()) {
        if (const auto me = m_combat->fighter(self)) {
            const std::span<const Candidate> candidates =
                m_combat->rank(*me, m_combat->hostiles(self), me->range8 + 64);
            for (const Candidate& c : candidates) {
                if (c.near && now >= s.next_attack_ms && fire_direction(s, *type, c.unit)) {
                    enemy = c.unit;
                    break;
                }
            }
            // Computer players' and patrolling units chase enemies; the player's melee units only
            // when they are not walking somewhere and the enemy is within 4 tiles. Ranged units of
            // the player never walk towards enemies on their own (original).
            const bool melee = static_cast<std::int32_t>(type->combat.range8) <= kMeleeRange8;
            const bool walking = !s.target.empty() && !attackable(s, s.target);
            const bool hunter = computer_controlled(*m_world, s.owner) || s.patrol.active || s.attack_move;
            if (enemy.empty() && !s.hold && (hunter || (melee && !walking))) {
                std::size_t tried = 0;
                bool out_of_plans = false;
                for (const Candidate& c : candidates) {
                    if (!hunter) {
                        const auto cells = target_cells(c.unit);
                        if (!cells || cells->distance(s.cell_x(), s.cell_y()) > kChaseRange) {
                            continue;
                        }
                    }
                    if (++tried > 7) {
                        break;  // the reachability test covers 7 candidates (0x00454580)
                    }
                    if (melee && on_other_island(s, c.unit)) {
                        // No way leads there on foot; skip the search, which would only fail
                        // (review point 18), but take its random draw so the game stays the same.
                        if (s.failures != 0) {
                            m_world->random(RandomStream::Military).next();
                        }
                        continue;
                    }
                    if (m_plans_left == 0) {
                        out_of_plans = true;
                        break;
                    }
                    if (plan_to(s, c.unit, kChaseRadius, true)) {
                        chase = c.unit;
                        break;
                    }
                }
                if (out_of_plans && !chase) {
                    start_wait(s, std::max<std::uint32_t>(1, m_tick_ms));
                    return;
                }
            }
        }
    }
    if (!enemy.empty()) {
        if (attack(s, *type, enemy)) {
            return;
        }
        // Out of range or sight: walk into range (step 6); holding its position it waits.
        if (s.hold) {
            rest(s);
            return;
        }
        if (!plan_to(s, enemy, kPlanRadius, true)) {
            ++s.failures;
            if (s.failures > kMaxFailures && enemy == s.target) {
                next_queued(s);
                s.failures = 0;
            }
            rest(s);
            return;
        }
    } else if (chase) {
        // plan_to has set the way.
    } else if (m.path_index >= m.path.size()) {
        // Nothing to fight; the way is done (step 7).
        const bool hostile_target = attackable(s, s.target);
        if (!s.target.empty() && !hostile_target && arrived(s)) {
            if (s.motion.phase == SoldierMotion::Phase::Healing || s.owner == kNoPlayer) {
                return;  // inside the doctor's house, or on board
            }
        }
        if (s.target.empty() && now >= s.last_attack_ms + kWanderAfterMs) {
            const Player* p = m_world->player(s.owner);
            if (p != nullptr && m_world->behaviour(*p) == FactionBehaviour::Natives) {
                (void)wander(s);
            }
        }
        if (!s.target.empty()) {
            if (!plan(s, kPlanRadius, attackable(s, s.target))) {
                ++s.failures;
                if (s.failures > kMaxFailures) {
                    next_queued(s);
                    s.failures = 0;
                }
                rest(s);
                return;
            }
        }
    } else if (s.target.moving() && now >= s.next_follow_ms) {
        // Following a moving target: a new way every 3 s (step 8).
        s.next_follow_ms = now + kFollowMs;
        if (const auto cells = target_cells(s.target);
            cells && cells->distance(s.cell_x(), s.cell_y()) > kFriendlyGoal) {
            (void)plan(s, kFollowRadius, false);
        }
    }
    if (!start_next_run(s)) {
        rest(s);
    }
}

}  // namespace o1602::sim
