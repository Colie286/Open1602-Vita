// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/unit_selection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "o1602/sim/command_io.hpp"

namespace o1602::ui {

namespace {

constexpr double kShipFormationScale = 3.5;  // tiles between the places of a ship formation
constexpr double kScatter = 13.36;           // 1.67 times the spread, in 1/8 tile (0x0044C8B0)
constexpr std::uint32_t kWoundedLimit = 64;  // health * 128 / max: half health (0x0044C510)

bool valid(const sim::World& world, const UnitRef& ref) {
    if (ref.kind == UnitRef::Kind::Ship) {
        const sim::Ship* s = world.ship(ref.id);
        return s != nullptr && !s->sinking;
    }
    const sim::Soldier* s = world.soldier(ref.id);
    return s != nullptr && !s->away();
}

std::int16_t to_offset(double v) {
    const double t = std::trunc(v);
    return static_cast<std::int16_t>(std::clamp(t, -32768.0, 32767.0));
}

std::uint16_t figure_of(const sim::World& world, const UnitRef& ref) {
    if (ref.kind == UnitRef::Kind::Ship) {
        const sim::Ship* s = world.ship(ref.id);
        return s != nullptr ? s->figure : 0;
    }
    const sim::Soldier* s = world.soldier(ref.id);
    return s != nullptr ? s->figure : 0;
}

/// FIGUR Kind: 0 for ships, 1 .. 4 for the land units.
std::int32_t figure_kind(const sim::World& world, const assets::FigureTable& figures, const UnitRef& ref) {
    if (ref.kind == UnitRef::Kind::Ship) {
        return 0;
    }
    const assets::FigureType* t = figures.get(figure_of(world, ref));
    return t != nullptr ? t->kind : 0;
}

std::uint64_t key_of(const UnitRef& ref) {
    return std::uint64_t{ref.kind == UnitRef::Kind::Ship ? 1U : 0U} << 32 | ref.id;
}

/// `units` without repeats, in their order: in linear time, as a selection may hold thousands.
std::vector<UnitRef> without_repeats(std::vector<UnitRef> units) {
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(units.size());
    std::erase_if(units, [&](const UnitRef& r) { return !seen.insert(key_of(r)).second; });
    return units;
}

/// One order for the land units in parts the network takes (sim::kMaxCommandItems), each with the
/// offsets that go with its units if there are any.
void push_soldier_orders(std::vector<sim::Command>& out, sim::SoldierOrder all) {
    const std::size_t n = all.soldiers.size();
    for (std::size_t first = 0; first < n; first += sim::kMaxCommandItems) {
        const std::size_t last = std::min(n, first + sim::kMaxCommandItems);
        sim::SoldierOrder part = all;
        part.soldiers.assign(all.soldiers.begin() + static_cast<std::ptrdiff_t>(first),
                             all.soldiers.begin() + static_cast<std::ptrdiff_t>(last));
        if (!all.offsets.empty()) {
            part.offsets.assign(all.offsets.begin() + static_cast<std::ptrdiff_t>(first),
                                all.offsets.begin() + static_cast<std::ptrdiff_t>(last));
        }
        out.emplace_back(std::move(part));
    }
}

std::int32_t floor_tile(double v, std::uint32_t limit) {
    const auto t = static_cast<std::int32_t>(std::floor(v));
    return std::clamp(t, 0, static_cast<std::int32_t>(limit) - 1);
}

}  // namespace

std::optional<std::pair<double, double>> unit_position(const sim::World& world, const UnitRef& ref) {
    if (ref.kind == UnitRef::Kind::Ship) {
        const sim::Ship* s = world.ship(ref.id);
        if (s == nullptr) {
            return std::nullopt;
        }
        return std::make_pair(s->pos_x / static_cast<double>(sim::kShipUnit),
                              s->pos_y / static_cast<double>(sim::kShipUnit));
    }
    const sim::Soldier* s = world.soldier(ref.id);
    if (s == nullptr) {
        return std::nullopt;
    }
    return std::make_pair(s->x(), s->y());
}

sim::PlayerId unit_owner(const sim::World& world, const UnitRef& ref) {
    if (ref.kind == UnitRef::Kind::Ship) {
        const sim::Ship* s = world.ship(ref.id);
        return s != nullptr ? s->owner : sim::kNoPlayer;
    }
    const sim::Soldier* s = world.soldier(ref.id);
    return s != nullptr ? s->owner : sim::kNoPlayer;
}

std::optional<double> unit_health(const sim::World& world, const assets::FigureTable& figures, const UnitRef& ref) {
    std::uint32_t health = 0;
    std::uint32_t max = 0;
    if (ref.kind == UnitRef::Kind::Ship) {
        const sim::Ship* s = world.ship(ref.id);
        if (s == nullptr) {
            return std::nullopt;
        }
        const assets::FigureType* t = figures.get(s->figure);
        max = t != nullptr ? t->ship.max_health : 0;
        health = s->health;
    } else {
        const sim::Soldier* s = world.soldier(ref.id);
        if (s == nullptr) {
            return std::nullopt;
        }
        const assets::FigureType* t = figures.get(s->figure);
        max = t != nullptr ? t->combat.max_health : 0;
        health = s->health;
    }
    if (max == 0 || health == 0) {
        return 1.0;  // full (old files store 0)
    }
    return std::min(1.0, static_cast<double>(health) / max);
}

bool UnitSelection::contains(const UnitRef& ref) const noexcept {
    return std::find(m_units.begin(), m_units.end(), ref) != m_units.end();
}

std::optional<UnitRef::Kind> UnitSelection::kind() const noexcept {
    return m_units.empty() ? std::nullopt : std::optional(m_units.front().kind);
}

sim::PlayerId UnitSelection::owner(const sim::World& world) const {
    return m_units.empty() ? sim::kNoPlayer : unit_owner(world, m_units.front());
}

void UnitSelection::clear() {
    if (!m_units.empty()) {
        m_changed = true;
    }
    m_units.clear();
    m_offsets.clear();
    m_cycle_all.clear();
    m_cycle_step = 0;
}

void UnitSelection::reset() {
    clear();
    for (auto& group : m_groups) {
        group.clear();
    }
}

void UnitSelection::set(const sim::World& world, std::vector<UnitRef> units) {
    std::erase_if(units, [&](const UnitRef& r) { return !valid(world, r); });
    m_units = without_repeats(std::move(units));
    recentre(world);
    m_changed = true;
    if (!m_cycling) {
        m_cycle_all.clear();
        m_cycle_step = 0;
    }
}

bool UnitSelection::cycle_types(const sim::World& world) {
    if (m_cycle_all.empty()) {
        m_cycle_all = m_units;
        m_cycle_step = 0;
    }
    std::erase_if(m_cycle_all, [&](const UnitRef& r) { return !valid(world, r); });
    std::vector<std::uint16_t> types;
    for (const UnitRef& r : m_cycle_all) {
        types.push_back(figure_of(world, r));
    }
    std::sort(types.begin(), types.end());
    types.erase(std::unique(types.begin(), types.end()), types.end());
    if (types.size() < 2) {
        m_cycle_all.clear();
        m_cycle_step = 0;
        return false;
    }
    m_cycle_step = (m_cycle_step + 1) % (types.size() + 1);
    std::vector<UnitRef> units;
    for (const UnitRef& r : m_cycle_all) {
        if (m_cycle_step == 0 || figure_of(world, r) == types[m_cycle_step - 1]) {
            units.push_back(r);
        }
    }
    m_cycling = true;
    set(world, std::move(units));
    m_cycling = false;
    if (m_cycle_step == 0) {
        m_cycle_all.clear();
    }
    return true;
}

void UnitSelection::select(const sim::World& world, const UnitRef& ref) {
    set(world, {ref});
}

void UnitSelection::toggle(const sim::World& world, const UnitRef& ref) {
    if (contains(ref)) {
        remove(world, ref);
        return;
    }
    if (!m_units.empty() && m_units.front().kind == ref.kind && owner(world) == unit_owner(world, ref)) {
        std::vector<UnitRef> units = m_units;
        units.push_back(ref);
        set(world, std::move(units));
        return;
    }
    select(world, ref);
}

void UnitSelection::box(const sim::World& world, std::vector<UnitCandidate> candidates, sim::PlayerId local,
                        bool additive) {
    std::erase_if(candidates, [&](const UnitCandidate& c) { return !valid(world, c.ref); });
    if (candidates.empty()) {
        return;  // an empty rectangle changes nothing
    }
    const bool own = std::any_of(candidates.begin(), candidates.end(),
                                 [&](const UnitCandidate& c) { return unit_owner(world, c.ref) == local; });
    if (own) {
        std::erase_if(candidates, [&](const UnitCandidate& c) { return unit_owner(world, c.ref) != local; });
    }
    const UnitCandidate* best = &candidates.front();
    for (const UnitCandidate& c : candidates) {
        if (c.overlap >= best->overlap) {
            best = &c;
        }
    }
    if (!own) {
        select(world, best->ref);  // somebody else's units: one of them, to look at
        return;
    }
    const UnitRef::Kind kind = best->ref.kind;
    std::vector<UnitRef> units;
    if (additive && !m_units.empty() && m_units.front().kind == kind && owner(world) == local) {
        units = m_units;
    }
    for (const UnitCandidate& c : candidates) {
        if (c.ref.kind == kind) {
            units.push_back(c.ref);
        }
    }
    set(world, std::move(units));
}

void UnitSelection::select_alike(const sim::World& world, const std::vector<UnitCandidate>& candidates,
                                 const UnitRef& like) {
    const sim::PlayerId who = unit_owner(world, like);
    const std::uint16_t figure = figure_of(world, like);
    std::vector<UnitRef> units{like};
    for (const UnitCandidate& c : candidates) {
        if (c.ref.kind == like.kind && unit_owner(world, c.ref) == who && figure_of(world, c.ref) == figure) {
            units.push_back(c.ref);
        }
    }
    set(world, std::move(units));
}

void UnitSelection::remove(const sim::World& world, const UnitRef& ref) {
    std::vector<UnitRef> units = m_units;
    std::erase(units, ref);
    set(world, std::move(units));
}

bool UnitSelection::prune(const sim::World& world) {
    if (m_units.empty()) {
        return false;
    }
    const sim::PlayerId who = unit_owner(world, m_units.front());
    const auto gone = [&](const UnitRef& r) { return !valid(world, r) || unit_owner(world, r) != who; };
    if (std::none_of(m_units.begin(), m_units.end(), gone)) {
        return false;
    }
    std::vector<UnitRef> units = m_units;
    std::erase_if(units, gone);
    set(world, std::move(units));
    return true;
}

void UnitSelection::recentre(const sim::World& world) {
    m_offsets.assign(m_units.size(), Offset{});
    const auto c = centre(world);
    if (!c) {
        return;
    }
    for (std::size_t i = 0; i < m_units.size(); ++i) {
        if (const auto p = unit_position(world, m_units[i])) {
            m_offsets[i] = {to_offset((p->first - c->first) * 8.0), to_offset((p->second - c->second) * 8.0)};
        }
    }
}

std::optional<std::pair<double, double>> UnitSelection::centre(const sim::World& world) const {
    double x = 0.0;
    double y = 0.0;
    std::size_t n = 0;
    for (const UnitRef& r : m_units) {
        if (const auto p = unit_position(world, r)) {
            x += p->first;
            y += p->second;
            ++n;
        }
    }
    if (n == 0) {
        return std::nullopt;
    }
    return std::make_pair(x / static_cast<double>(n), y / static_cast<double>(n));
}

bool UnitSelection::any_moving(const sim::World& world) const {
    for (const UnitRef& r : m_units) {
        if (r.kind == UnitRef::Kind::Ship) {
            const sim::Ship* s = world.ship(r.id);
            if (s != nullptr && (!s->target.empty() || s->motion.path_index < s->motion.path.size() ||
                                 s->motion.phase == sim::ShipMotion::Phase::Run)) {
                return true;
            }
        } else if (const sim::Soldier* s = world.soldier(r.id); s != nullptr && !s->target.empty()) {
            return true;
        }
    }
    return false;
}

bool UnitSelection::any_white_flag(const sim::World& world) const {
    return std::any_of(m_units.begin(), m_units.end(), [&](const UnitRef& r) {
        const sim::Ship* s = r.kind == UnitRef::Kind::Ship ? world.ship(r.id) : nullptr;
        return s != nullptr && s->held;
    });
}

std::uint32_t UnitSelection::kind_mask(const sim::World& world, const assets::FigureTable& figures) const {
    std::uint32_t mask = 0;
    for (const UnitRef& r : m_units) {
        mask |= 1U << static_cast<std::uint32_t>(std::clamp(figure_kind(world, figures, r), 0, 31));
    }
    return mask;
}

bool UnitSelection::select_kind(const sim::World& world, const assets::FigureTable& figures,
                                const std::vector<UnitCandidate>& candidates, sim::PlayerId local, std::int32_t kind) {
    std::vector<UnitRef> units;
    for (const UnitCandidate& c : candidates) {
        if (c.ref.kind == UnitRef::Kind::Soldier && unit_owner(world, c.ref) == local &&
            figure_kind(world, figures, c.ref) == kind && std::find(units.begin(), units.end(), c.ref) == units.end()) {
            units.push_back(c.ref);
        }
    }
    if (units.empty()) {
        return false;
    }
    set(world, std::move(units));
    return true;
}

void UnitSelection::filter_kinds(const sim::World& world, const assets::FigureTable& figures, std::uint32_t mask,
                                 bool keep) {
    std::vector<UnitRef> units;
    for (const UnitRef& r : m_units) {
        const bool in =
            (mask & (1U << static_cast<std::uint32_t>(std::clamp(figure_kind(world, figures, r), 0, 31)))) != 0;
        if (in == keep) {
            units.push_back(r);
        }
    }
    set(world, std::move(units));
}

void UnitSelection::filter_wounded(const sim::World& world, const assets::FigureTable& figures, bool keep_wounded) {
    std::vector<UnitRef> units;
    for (const UnitRef& r : m_units) {
        const double h = unit_health(world, figures, r).value_or(1.0);
        const bool wounded = static_cast<std::uint32_t>(h * 128.0) <= kWoundedLimit;
        if (wounded == keep_wounded) {
            units.push_back(r);
        }
    }
    set(world, std::move(units));
}

void UnitSelection::formation(const sim::World& world, const assets::FigureTable& figures, std::size_t n) {
    const auto& formations = figures.formations();
    const auto c = centre(world);
    if (n >= formations.size() || formations[n].empty() || !c) {
        return;
    }
    const double scale = m_units.front().kind == UnitRef::Kind::Ship ? kShipFormationScale : 1.0;
    if (m_units.size() > formations[n].size()) {
        rows_behind(world, formations[n], scale);
        return;
    }
    const auto& slots = formations[n];
    std::vector<bool> taken(slots.size(), false);
    for (std::size_t i = 0; i < m_units.size(); ++i) {
        const auto p = unit_position(world, m_units[i]);
        if (!p) {
            continue;
        }
        std::size_t best = slots.size();
        double best_d = std::numeric_limits<double>::max();
        for (std::size_t k = 0; k < slots.size() && k < m_units.size(); ++k) {
            if (taken[k]) {
                continue;
            }
            const double dx = std::abs(c->first + scale * slots[k].x - p->first);
            const double dy = std::abs(c->second + scale * slots[k].y - p->second);
            const double d = std::max(dx, dy) + 0.25 * std::min(dx, dy);
            if (d < best_d) {
                best = k;
                best_d = d;
            }
        }
        if (best == slots.size()) {
            continue;  // more units than places: the offset stays
        }
        taken[best] = true;
        m_offsets[i] = {to_offset(scale * slots[best].x * 8.0), to_offset(scale * slots[best].y * 8.0)};
    }
    m_changed = true;
}

void UnitSelection::rows_behind(const sim::World& world, const std::vector<assets::FigureTable::FormationSlot>& slots,
                                double scale) {
    // Copies of the formation one behind the other, each as deep as the formation and a tile apart,
    // the whole centred; the units front to back by where they stand (in order for the same place),
    // so that none crosses the others to its place. Linear but for the sort, for thousands of units.
    std::int32_t top = slots.front().y;
    std::int32_t bottom = top;
    for (const auto& s : slots) {
        top = std::min(top, s.y);
        bottom = std::max(bottom, s.y);
    }
    const std::int32_t depth = bottom - top + 1;
    const std::size_t copies = (m_units.size() + slots.size() - 1) / slots.size();
    const double shift = static_cast<double>(depth) * static_cast<double>(copies - 1) / 2.0;
    std::vector<std::size_t> order(m_units.size());
    std::vector<double> along(m_units.size(), 0.0);
    for (std::size_t i = 0; i < m_units.size(); ++i) {
        order[i] = i;
        if (const auto p = unit_position(world, m_units[i])) {
            along[i] = p->second;
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return along[a] < along[b]; });
    for (std::size_t k = 0; k < order.size(); ++k) {
        const auto& s = slots[k % slots.size()];
        const double row = static_cast<double>(depth) * static_cast<double>(k / slots.size()) - shift;
        m_offsets[order[k]] = {to_offset(scale * s.x * 8.0), to_offset(scale * (s.y + row) * 8.0)};
    }
    m_changed = true;
}

void UnitSelection::scatter(const sim::World& world) {
    const auto c = centre(world);
    if (!c) {
        return;
    }
    for (std::size_t i = 0; i < m_units.size(); ++i) {
        if (const auto p = unit_position(world, m_units[i])) {
            m_offsets[i] = {to_offset((p->first - c->first) * kScatter), to_offset((p->second - c->second) * kScatter)};
        }
    }
    m_changed = true;
}

void UnitSelection::store_group(std::size_t g) {
    if (g >= kGroups) {
        return;
    }
    // A unit belongs to one group at a time (Group_Store 0x0044D1D0).
    std::unordered_set<std::uint64_t> in;
    for (const UnitRef& r : m_units) {
        in.insert(key_of(r));
    }
    for (auto& group : m_groups) {
        std::erase_if(group, [&](const Member& m) { return in.contains(key_of(m.ref)); });
    }
    m_groups[g].clear();
    for (std::size_t i = 0; i < m_units.size(); ++i) {
        m_groups[g].push_back(Member{m_units[i], m_offsets[i]});
    }
}

void UnitSelection::add_to_group(const sim::World& world, std::size_t g) {
    if (g >= kGroups) {
        return;
    }
    // The group and the selection together, their shape as they stand now; the selection stays.
    std::vector<UnitRef> units;
    for (const Member& m : m_groups[g]) {
        if (valid(world, m.ref)) {
            units.push_back(m.ref);
        }
    }
    units.insert(units.end(), m_units.begin(), m_units.end());
    units = without_repeats(std::move(units));
    std::unordered_set<std::uint64_t> in;
    for (const UnitRef& r : units) {
        in.insert(key_of(r));
    }
    for (auto& group : m_groups) {
        std::erase_if(group, [&](const Member& m) { return in.contains(key_of(m.ref)); });
    }
    UnitSelection shape;
    shape.set(world, units);
    for (std::size_t i = 0; i < shape.m_units.size(); ++i) {
        m_groups[g].push_back(Member{shape.m_units[i], shape.m_offsets[i]});
    }
}

bool UnitSelection::recall_group(const sim::World& world, std::size_t g) {
    if (g >= kGroups) {
        return false;
    }
    std::vector<Member>& group = m_groups[g];
    std::erase_if(group, [&](const Member& m) { return !valid(world, m.ref); });
    if (group.empty()) {
        return false;
    }
    m_units.clear();
    m_offsets.clear();
    for (const Member& m : group) {
        m_units.push_back(m.ref);
        m_offsets.push_back(m.offset);  // the shape the group had (Group_Recall 0x0044D2B0)
    }
    m_changed = true;
    return true;
}

std::optional<std::size_t> UnitSelection::group_of(const UnitRef& ref) const {
    for (std::size_t g = 0; g < kGroups; ++g) {
        if (std::any_of(m_groups[g].begin(), m_groups[g].end(), [&](const Member& m) { return m.ref == ref; })) {
            return g;
        }
    }
    return std::nullopt;
}

sim::UnitGroups UnitSelection::saved_groups() const {
    static_assert(std::tuple_size_v<sim::UnitGroups> == kGroups);
    sim::UnitGroups out;
    for (std::size_t g = 0; g < kGroups; ++g) {
        for (const Member& m : m_groups[g]) {
            out[g].push_back(
                sim::GroupMember{m.ref.kind == UnitRef::Kind::Ship, m.ref.id, m.offset.first, m.offset.second});
        }
    }
    return out;
}

void UnitSelection::restore_groups(const sim::UnitGroups& groups) {
    for (std::size_t g = 0; g < kGroups; ++g) {
        m_groups[g].clear();
        for (const sim::GroupMember& m : groups[g]) {
            const UnitRef ref{m.ship ? UnitRef::Kind::Ship : UnitRef::Kind::Soldier, m.id};
            m_groups[g].push_back(Member{ref, {m.dx, m.dy}});
        }
    }
}

std::vector<sim::Command> UnitSelection::move_orders(const sim::World& world, sim::PlayerId local, double x, double y,
                                                     bool queue, bool attack_move) const {
    std::vector<sim::Command> out;
    sim::SoldierOrder soldiers;
    soldiers.player = local;
    soldiers.queue = queue;
    soldiers.attack_move = attack_move;
    soldiers.target = sim::Target::spot(static_cast<std::uint32_t>(floor_tile(2.0 * x, 2 * world.width())),
                                        static_cast<std::uint32_t>(floor_tile(2.0 * y, 2 * world.height())));
    for (std::size_t i = 0; i < m_units.size(); ++i) {
        const UnitRef& r = m_units[i];
        if (!valid(world, r) || unit_owner(world, r) != local) {
            continue;  // only the player's own units take orders (0x0044D540)
        }
        const Offset& off = m_offsets[i];
        if (r.kind == UnitRef::Kind::Ship) {
            sim::ShipOrder o;
            o.player = local;
            o.ship = r.id;
            o.queue = queue;
            o.attack_move = attack_move;
            o.target =
                sim::Target::position(static_cast<std::uint32_t>(floor_tile(x + off.first / 8.0, world.width())),
                                      static_cast<std::uint32_t>(floor_tile(y + off.second / 8.0, world.height())));
            out.emplace_back(o);
        } else {
            soldiers.soldiers.push_back(r.id);
            soldiers.offsets.push_back(off);
        }
    }
    if (!soldiers.soldiers.empty()) {
        push_soldier_orders(out, std::move(soldiers));
    }
    return out;
}

std::vector<sim::Command> UnitSelection::target_orders(const sim::World& world, sim::PlayerId local,
                                                       const sim::Target& target, bool queue, bool attack) const {
    std::vector<sim::Command> out;
    sim::SoldierOrder soldiers;
    soldiers.player = local;
    soldiers.target = target;
    soldiers.queue = queue;
    soldiers.attack = attack;
    for (const UnitRef& r : m_units) {
        if (!valid(world, r) || unit_owner(world, r) != local) {
            continue;
        }
        if (r.kind == UnitRef::Kind::Ship) {
            if (target.kind == sim::Target::Kind::Ship && target.ship == r.id) {
                continue;  // not onto itself
            }
            sim::ShipOrder o;
            o.player = local;
            o.ship = r.id;
            o.target = target;
            o.queue = queue;
            o.attack = attack;
            out.emplace_back(o);
        } else if (!(target.kind == sim::Target::Kind::Soldier && target.ship == r.id)) {
            soldiers.soldiers.push_back(r.id);
        }
    }
    if (!soldiers.soldiers.empty()) {
        push_soldier_orders(out, std::move(soldiers));
    }
    return out;
}

std::vector<sim::Command> UnitSelection::hold_orders(const sim::World& world, sim::PlayerId local) const {
    std::vector<sim::Command> out;
    for (const UnitRef& r : m_units) {
        if (!valid(world, r) || unit_owner(world, r) != local) {
            continue;
        }
        if (r.kind == UnitRef::Kind::Ship) {
            sim::ShipCommand c;
            c.player = local;
            c.ship = r.id;
            c.op = sim::ShipCommand::Op::Hold;
            out.emplace_back(c);
        } else {
            sim::SoldierCommand c;
            c.player = local;
            c.soldier = r.id;
            c.op = sim::SoldierCommand::Op::Hold;
            out.emplace_back(c);
        }
    }
    return out;
}

std::vector<sim::Command> UnitSelection::stop_orders(const sim::World& world, sim::PlayerId local) const {
    std::vector<sim::Command> out;
    for (const UnitRef& r : m_units) {
        if (!valid(world, r) || unit_owner(world, r) != local) {
            continue;  // the original stops foreign ones too (Q10)
        }
        if (r.kind == UnitRef::Kind::Ship) {
            out.emplace_back(sim::ShipOrder{local, r.id, sim::Target{}});
        } else {
            sim::SoldierCommand c;
            c.player = local;
            c.soldier = r.id;
            c.op = sim::SoldierCommand::Op::Stop;
            out.emplace_back(c);
        }
    }
    return out;
}

}  // namespace o1602::ui
