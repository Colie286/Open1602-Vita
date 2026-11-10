// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/houses.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <set>

#include "o1602/sim/disasters.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/pathfinding.hpp"

namespace o1602::sim {

namespace {

namespace hs = house_service;

constexpr std::uint32_t kInhabitantUnits = 64;  // inhabitants are counted in 1/64
constexpr std::uint8_t kFull = kFullSupply;
constexpr std::uint8_t kNoMarket = 6;  // market distance class without (or far from) a market
constexpr std::size_t kMaxNeighbourTiles = 40;

// ---- Tables of 0x00478630 -------------------------------------------------------------------

/// 0x00403370: fixed point steps of 1/256 between (x0, y0) and (x1, y1), byte values.
template <std::size_t N>
void lerp(std::array<std::int32_t, N>& t, std::int32_t x0, std::int32_t x1, std::int32_t y0, std::int32_t y1) {
    const std::int32_t step = ((y1 - y0) * 256) / (x1 - x0);  // C division: towards zero
    std::int32_t acc = y0 * 256;
    for (std::int32_t x = x0; x < x1; ++x) {
        t[static_cast<std::size_t>(x)] = std::min(acc >> 8, 255);
        acc += step;
    }
    t[static_cast<std::size_t>(x1)] = y1 & 0xFF;
}

struct Tables {
    std::array<std::int32_t, 129> t1{};  // by food: growth damping (no effect, see houses.md)
    std::array<std::int32_t, 129> t3{};  // by satisfaction: decline
    std::array<std::int32_t, 129> t4{};  // by food: decline
    std::array<std::int32_t, 7> t2{};    // by market distance: growth
    std::array<std::int32_t, 7> t5{};    // by market distance: decline factor
    std::array<std::array<std::uint8_t, 17>, 17> distance{};

    Tables() {
        lerp(t1, 0, 128, 512, 0);
        lerp(t4, 0, 51, 192, 64);
        lerp(t4, 51, 88, 64, 19);
        lerp(t4, 88, 128, 19, 0);
        lerp(t3, 0, 25, 108, 70);
        lerp(t3, 25, 51, 70, 32);
        lerp(t3, 51, 88, 32, 12);
        lerp(t3, 88, 128, 12, 0);
        lerp(t5, 0, 3, 102, 128);
        lerp(t5, 3, 6, 128, 192);
        lerp(t2, 0, 6, 160, 115);
        // round(0.375 * distance): the market distance class (0x005A6AF0). In integers (ADR 0004):
        // the class is the largest v with 3 * sqrt(n) / 8 + 1 / 2 >= v, that is (8v - 4)^2 <= 9n.
        for (std::size_t i = 0; i < 17; ++i) {
            for (std::size_t j = 0; j < 17; ++j) {
                const std::size_t n9 = 9 * (i * i + j * j);
                std::uint8_t v = 0;
                while ((8U * (v + 1U) - 4U) * (8U * (v + 1U) - 4U) <= n9) {
                    ++v;
                }
                distance[i][j] = v;
            }
        }
    }
};

const Tables& tables() {
    static const Tables kTables;
    return kTables;
}

/// What a class of inhabitants needs to live in a house (0x0047BFA0): one of the services of
/// every entry. The market is no service bit of House::services; kMarketNeed stands for it.
constexpr std::uint16_t kMarketNeed = 1U << 15;
using ServiceNeeds = std::array<std::uint16_t, 6>;
constexpr std::array<ServiceNeeds, 5> kServiceNeeds = {{
    {kMarketNeed | hs::kKontor},                                                               // pioneers
    {kMarketNeed, hs::kKapelle | hs::kKirche},                                                 // settlers
    {kMarketNeed, hs::kWirtshaus, hs::kKapelle | hs::kKirche, hs::kSchule | hs::kHochschule},  // citizens
    {kMarketNeed, hs::kWirtshaus, hs::kKirche, hs::kSchule | hs::kHochschule, hs::kBadehaus},  // merchants
    {kMarketNeed, hs::kWirtshaus, hs::kKirche, hs::kHochschule, hs::kBadehaus, hs::kTheater},  // aristocrats
}};
static_assert(kServiceNeeds.size() == kResidentClasses, "a row of needs for each class haeuser.cod may have");
static_assert((kMarketNeed &
               (hs::kDisasterMask | hs::kKapelle | hs::kKirche | hs::kWirtshaus | hs::kSchule | hs::kBadehaus |
                hs::kTheater | hs::kHochschule | hs::kKlinik | hs::kKontor | hs::kGalgen | hs::kBrunnen)) == 0,
              "kMarketNeed must not be a bit of House::services");

/// Decline for plague and fire on the house (P7).
constexpr std::array<std::int32_t, 4> kDisasterLoss = {0, 256, 192, 0};

std::uint16_t service_bit(std::int32_t prod_kind) noexcept {
    switch (prod_kind) {
        case prod::kWirt:
            return hs::kWirtshaus;
        case prod::kKapelle:
            return hs::kKapelle;
        case prod::kKirche:
            return hs::kKirche;  // also the cathedral
        case prod::kBadehaus:
            return hs::kBadehaus;
        case prod::kTheater:
            return hs::kTheater;
        case prod::kKlinik:
            return hs::kKlinik;
        case prod::kSchule:
            return hs::kSchule;
        case prod::kHochschule:
            return hs::kHochschule;
        case prod::kGalgen:
            return hs::kGalgen;
        case prod::kBrunnen:
            return hs::kBrunnen;
        case prod::kKontor:
            return hs::kKontor;
        default:
            return 0;
    }
}

/// Gap between the centre column(s) of a building and the nearest column of a house, the
/// measure of 0x00482120 (an even width has two centre columns). The original assumes a 2 x 2
/// house; Open1602 uses its real size.
std::int32_t gap(std::int32_t origin, std::uint32_t size, std::int32_t house, std::uint32_t house_size) noexcept {
    const auto w = static_cast<std::int32_t>(size) - 1;
    const std::int32_t first = origin + w / 2;
    const std::int32_t last = first + (w & 1);
    const std::int32_t house_last = house + static_cast<std::int32_t>(house_size) - 1;
    if (house_last < first) {
        return first - house_last;
    }
    if (house > last) {
        return house - last;
    }
    return 0;
}

std::uint32_t max_inhabitants64(const assets::BuildingTable& buildings, std::uint8_t resident_class) {
    const auto& classes = buildings.resident_classes();
    return resident_class < classes.size() ? classes[resident_class].max_per_house * kInhabitantUnits : 0;
}

std::uint8_t satisfaction_of_class(const City& city, std::uint8_t c) noexcept {
    return c < city.satisfaction.size() ? std::min(city.satisfaction[c], kFull) : kFull;
}

std::uint32_t& city_count(City& city, std::uint8_t c) {
    if (c >= city.inhabitants.size()) {
        city.inhabitants.resize(std::size_t{c} + 1, 0);
    }
    return city.inhabitants[c];
}

/// Sets the inhabitants of a house and keeps the city's sum per class right.
void set_inhabitants(City& city, House& h, std::uint32_t inhabitants64) {
    std::uint32_t& n = city_count(city, h.resident_class);
    n -= std::min(n, h.inhabitants());
    h.inhabitants64 = inhabitants64;
    city_count(city, h.resident_class) += h.inhabitants();
}

City::UpgradeSlot& queue_slot(City& city, std::size_t c) {
    if (c >= city.upgrade_queue.size()) {
        city.upgrade_queue.resize(c + 1);
    }
    return city.upgrade_queue[c];
}

/// Frees any queue slot that names this house. Fix S-028: the original keeps the slot when the
/// queued house changes class.
void leave_queue(City& city, const House& h) {
    for (City::UpgradeSlot& slot : city.upgrade_queue) {
        if (slot.x == h.x && slot.y == h.y) {
            slot = City::UpgradeSlot{};
        }
    }
}

class HouseVisitor {
public:
    HouseVisitor(World& world, const assets::BuildingTable& buildings, std::vector<HouseEvent>* events)
        : m_world(world), m_buildings(buildings), m_events(events) {}

    void visit(City& city, std::uint16_t island, std::uint16_t x, std::uint16_t y) {
        House* h = m_world.house(island, x, y);
        if (h == nullptr) {
            return;
        }
        const std::int32_t rate = growth_rate(city, *h);
        // What the city remembers for its monuments (city + 0x1F8, + 0x1E4).
        if (rate > 0 && city.house_growth <= 0) {
            city.growing_since_ms = m_world.time_ms();
        }
        city.house_growth = rate;
        if (rate == 0) {
            return;
        }
        const std::uint32_t n = h->inhabitants();
        if (n == 0) {
            abandon(city, *h);  // also with a positive rate: there is nobody left to grow
            return;
        }
        if (rate < 0) {
            shrink(city, *h, n * static_cast<std::uint32_t>(-rate) * 4 / 128);
        } else {
            grow(city, *h, n * static_cast<std::uint32_t>(rate) * 6 / 128);
        }
    }

private:
    /// Houses of the same city reachable over roads, open land, ruins and other houses within
    /// the house's radius (14 x 14 tiles), nearest first; at most 40 tiles like the original.
    std::vector<House*> neighbours(const House& h) {
        std::vector<House*> result;
        const IslandMap& map = m_world.islands()[h.island].map;
        const MapCell& cell = map.top(h.x, h.y);
        const assets::BuildingType* type = cell.empty() ? nullptr : m_buildings.by_sprite(cell.sprite);
        if (type == nullptr) {
            return result;
        }
        const Catchment area = catchment_of(*type, cell.rotation, {h.x, h.y});
        m_finder.begin(map, m_buildings, area, WalkClass::Land, kUniformSpeed, false);
        const TilePos corner = m_finder.window_min();
        for (std::uint32_t j = 0; j < m_finder.window_height(); ++j) {
            for (std::uint32_t i = 0; i < m_finder.window_width(); ++i) {
                const TilePos t{corner.x + static_cast<std::int32_t>(i), corner.y + static_cast<std::int32_t>(j)};
                if (!map.contains(t.x, t.y)) {
                    continue;
                }
                const MapCell& c = map.top(static_cast<std::uint32_t>(t.x), static_cast<std::uint32_t>(t.y));
                const assets::BuildingType* b = c.empty() ? nullptr : m_buildings.by_sprite(c.sprite);
                if (b != nullptr && b->prod_kind == prod::kHouse && c.city == h.city) {
                    m_finder.set(t, PathFinder::Cell::Goal, 32);
                }
            }
        }
        std::size_t tiles = 0;
        m_finder.search({h.x, h.y}, [&](TilePos t) {
            const SubTile sub =
                map.sub_tile(m_buildings, static_cast<std::uint32_t>(t.x), static_cast<std::uint32_t>(t.y));
            if (House* o = m_world.house(h.island, static_cast<std::uint16_t>(sub.origin_x),
                                         static_cast<std::uint16_t>(sub.origin_y));
                o != nullptr && (o->x != h.x || o->y != h.y) &&
                std::find(result.begin(), result.end(), o) == result.end()) {
                result.push_back(o);
            }
            return ++tiles >= kMaxNeighbourTiles ? PathFinder::Answer::Accept : PathFinder::Answer::Cross;
        });
        return result;
    }

    /// Moves up to `amount` inhabitants to neighbours of class `c` (with `limit` as the most a
    /// neighbour may already hold); returns what nobody took.
    std::uint32_t hand_over(City& city, const std::vector<House*>& to, std::uint8_t c, std::uint32_t amount,
                            std::uint32_t limit) {
        const std::uint32_t cap = max_inhabitants64(m_buildings, c);
        for (House* o : to) {
            if (amount == 0) {
                break;
            }
            if (o->resident_class != c || o->inhabitants64 > limit || o->inhabitants64 >= cap) {
                continue;
            }
            const std::uint32_t t = std::min(amount, cap - o->inhabitants64);
            set_inhabitants(city, *o, o->inhabitants64 + t);
            amount -= t;
        }
        return amount;
    }

    /// The tiles around a house tell which way it faces a road (0x00467940): left 2, right 0,
    /// top 3, bottom 1; none keeps the rotation.
    std::uint8_t road_rotation(const House& h, std::uint8_t current) const {
        const IslandMap& map = m_world.islands()[h.island].map;
        const MapCell& cell = map.top(h.x, h.y);
        const assets::BuildingType* type = m_buildings.by_sprite(cell.sprite);
        std::uint32_t w = 2;
        std::uint32_t hh = 2;
        if (type != nullptr) {
            rotated_size(*type, cell.rotation, w, hh);
        }
        const auto road = [&](std::int32_t x, std::int32_t y) {
            if (!map.contains(x, y)) {
                return false;
            }
            const MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            const assets::BuildingType* b = c.empty() ? nullptr : m_buildings.by_sprite(c.sprite);
            if (b == nullptr) {
                return false;
            }
            return b->kind == building_kind::kRoad || b->kind == building_kind::kBridge ||
                   (b->kind == building_kind::kGate && walkable(*b, c, WalkClass::Roads));
        };
        const auto x0 = static_cast<std::int32_t>(h.x);
        const auto y0 = static_cast<std::int32_t>(h.y);
        for (std::uint32_t j = 0; j < hh; ++j) {
            if (road(x0 - 1, y0 + static_cast<std::int32_t>(j))) {
                return 2;
            }
        }
        for (std::uint32_t j = 0; j < hh; ++j) {
            if (road(x0 + static_cast<std::int32_t>(w), y0 + static_cast<std::int32_t>(j))) {
                return 0;
            }
        }
        for (std::uint32_t i = 0; i < w; ++i) {
            if (road(x0 + static_cast<std::int32_t>(i), y0 - 1)) {
                return 3;
            }
        }
        for (std::uint32_t i = 0; i < w; ++i) {
            if (road(x0 + static_cast<std::int32_t>(i), y0 + static_cast<std::int32_t>(hh))) {
                return 1;
            }
        }
        return current;
    }

    /// Replaces the house's building by a random variant of the first house of class `c`.
    bool rebuild(House& h, std::uint8_t c) {
        const assets::BuildingType* first = m_buildings.first_house(c);
        if (first == nullptr) {
            return false;
        }
        const std::uint32_t variants = std::max<std::uint32_t>(first->rand_count, 1);
        const std::uint32_t pick = m_world.random(RandomStream::Houses).below(variants);
        const assets::BuildingType* variant = m_buildings.by_record(first->record + pick * first->rand_add);
        if (variant == nullptr) {
            variant = first;
        }
        IslandMap& map = m_world.island(h.island).map;
        const std::uint8_t rotation = road_rotation(h, map.top(h.x, h.y).rotation);
        if (!map.replace(*variant, h.x, h.y, rotation, 0).ok()) {
            return false;
        }
        h.resident_class = c;
        return true;
    }

    bool pay_materials(City& city, const assets::BuildingType& target) {
        const auto& m = m_buildings.materials();
        const auto& cost = target.build_cost;
        if (city.available(m.tools) < cost.tools || city.available(m.bricks) < cost.bricks ||
            city.available(m.wood) < cost.wood) {
            return false;
        }
        // 0x0047B160: min(stock, cost), nothing below a ton.
        const auto take = [&](std::uint8_t ware, std::uint32_t amount) {
            city.ensure_ware(ware);
            const std::uint32_t t = std::min(city.goods[ware], amount);
            if (t >= 32) {
                city.goods[ware] -= t;
                city.book(ware, GoodsFlow::Construction, t);
            }
        };
        take(m.tools, cost.tools);
        take(m.bricks, cost.bricks);
        take(m.wood, cost.wood);
        take(m.cannons, cost.cannons);
        if (Player* owner = m_world.player(city.owner); owner != nullptr) {
            owner->money = money_plus(owner->money, -std::int64_t{cost.money});
        }
        return true;
    }

    void emit(HouseEvent::Kind kind, const House& h) {
        if (m_events != nullptr) {
            m_events->push_back(HouseEvent{kind, h.island, h.x, h.y, h.resident_class});
        }
    }

    /// 0x0047C080.
    void grow(City& city, House& h, std::uint32_t add) {
        const std::uint8_t c = h.resident_class;
        std::uint32_t v = h.inhabitants64 + add;
        const std::uint32_t cap = max_inhabitants64(m_buildings, c);
        const auto next = static_cast<std::uint8_t>(c + 1);
        if (v > cap && next < m_buildings.resident_classes().size()) {
            City::UpgradeSlot& slot = queue_slot(city, next);
            if (slot.inhabitants != 0 && slot.x == h.x && slot.y == h.y && city.epoch != h.queue_epoch) {
                // The queued house, one city tick later: try out the next class.
                const std::vector<House*> nb = neighbours(h);
                const std::uint32_t next_cap = max_inhabitants64(m_buildings, next);
                if (city.food_ratio >= kFull && satisfaction_of_class(city, next) >= kFull) {
                    if (city_count(city, next) != 0) {
                        // Half of the people move into houses of the next class first.
                        const std::uint32_t half = v / 2;
                        v = v - half + hand_over(city, nb, next, half, next_cap);
                    }
                    const assets::BuildingType* target = m_buildings.first_house(next);
                    if (!city.material_lock && v > cap && target != nullptr && has_services(h, m_buildings, next) &&
                        pay_materials(city, *target)) {
                        const std::uint32_t before = h.inhabitants();
                        std::uint32_t& old_count = city_count(city, c);
                        old_count -= std::min(old_count, before);
                        if (rebuild(h, next)) {
                            city_count(city, next) += before;
                            emit(HouseEvent::Kind::Upgraded, h);
                        } else {
                            city_count(city, c) += before;
                        }
                    }
                }
                const std::uint8_t now = h.resident_class;
                const std::uint32_t now_cap = max_inhabitants64(m_buildings, now);
                if (city_count(city, next) == 0 && v > now_cap) {
                    // Nobody of the next class yet: a quarter goes to emptier neighbours.
                    const std::uint32_t quarter = v / 4;
                    v = v - quarter + hand_over(city, nb, now, quarter, now_cap / 2);
                }
                slot = City::UpgradeSlot{};
            } else if (slot.inhabitants == 0) {
                slot = City::UpgradeSlot{v / kInhabitantUnits, h.x, h.y};
                h.queue_epoch = city.epoch;
            }
        }
        // Whatever does not fit is lost, as in the original.
        set_inhabitants(city, h, std::min(v, max_inhabitants64(m_buildings, h.resident_class)));
    }

    /// 0x0047BBC0.
    void shrink(City& city, House& h, std::uint32_t sub) {
        const std::uint8_t c = h.resident_class;
        std::uint32_t v = h.inhabitants64 > sub ? h.inhabitants64 - sub : 0;
        constexpr std::uint8_t kContent = 88;
        if (!h.services_complete || satisfaction_of_class(city, c) < kContent) {
            if (c != 0 && v <= max_inhabitants64(m_buildings, static_cast<std::uint8_t>(c - 1))) {
                const std::uint32_t before = h.inhabitants();
                std::uint32_t& old_count = city_count(city, c);
                old_count -= std::min(old_count, before);
                leave_queue(city, h);
                if (rebuild(h, static_cast<std::uint8_t>(c - 1))) {
                    h.services_complete = has_services(h, m_buildings, h.resident_class);
                    emit(HouseEvent::Kind::Downgraded, h);
                }
                city_count(city, h.resident_class) += before;
            }
        } else if (c != 0 && v < max_inhabitants64(m_buildings, c) / 2) {
            // Contented but too few: everybody moves to the neighbours of the same class.
            v = hand_over(city, neighbours(h), c, v, max_inhabitants64(m_buildings, c));
        }
        set_inhabitants(city, h, v);
    }

    /// Nobody lives there any more: every tile becomes a ruin (0x00463F40).
    void abandon(City& city, House& h) {
        const House copy = h;
        set_inhabitants(city, h, 0);
        leave_queue(city, h);
        IslandMap& map = m_world.island(copy.island).map;
        const MapCell& cell = map.top(copy.x, copy.y);
        const assets::BuildingType* type = cell.empty() ? nullptr : m_buildings.by_sprite(cell.sprite);
        if (type != nullptr && type->ruin != 0xFF) {
            std::uint32_t w = 2;
            std::uint32_t hh = 2;
            rotated_size(*type, cell.rotation, w, hh);
            for (std::uint32_t j = 0; j < hh; ++j) {
                for (std::uint32_t i = 0; i < w; ++i) {
                    const std::uint32_t tx = copy.x + i;
                    const std::uint32_t ty = copy.y + j;
                    const MapCell& ground = map.ground(tx, ty);
                    const assets::BuildingType* g = ground.empty() ? nullptr : m_buildings.by_sprite(ground.sprite);
                    // Ruins on a beach (STRAND .. STRANDVARI, 23..27) look different.
                    const bool beach = g != nullptr && g->kind >= kind::kBeach && g->kind <= kind::kBeachVariant;
                    const assets::BuildingType* ruin = m_buildings.ruin_record(type->ruin, beach);
                    if (ruin == nullptr) {
                        continue;
                    }
                    const std::uint32_t pick =
                        m_world.random(RandomStream::Houses).below(std::max<std::uint32_t>(ruin->rand_count, 1));
                    const assets::BuildingType* variant = m_buildings.by_record(ruin->record + pick * ruin->rand_add);
                    (void)map.replace(variant != nullptr ? *variant : *ruin, static_cast<std::int32_t>(tx),
                                      static_cast<std::int32_t>(ty), map.top(tx, ty).rotation, 0);
                }
            }
        }
        emit(HouseEvent::Kind::Abandoned, copy);
        forget_disaster(m_world, copy.island, {copy.x, copy.y});
        m_world.remove_house(copy.island, copy.x, copy.y);
    }

    World& m_world;
    const assets::BuildingTable& m_buildings;
    std::vector<HouseEvent>* m_events;
    PathFinder m_finder;
};

}  // namespace

bool has_services(const House& house, std::uint8_t resident_class) noexcept {
    // The needs of 0x0047BFA0 by class; classes beyond the table need what the last one needs.
    const auto s = static_cast<std::uint16_t>(house.services | (house.market ? kMarketNeed : 0U));
    const ServiceNeeds& needs = kServiceNeeds[std::min<std::size_t>(resident_class, kServiceNeeds.size() - 1)];
    return std::all_of(needs.begin(), needs.end(), [s](std::uint16_t any) { return any == 0 || (s & any) != 0; });
}

std::uint8_t service_class(const assets::BuildingTable& buildings, std::uint8_t resident_class) noexcept {
    const auto& classes = buildings.resident_classes();
    return resident_class < classes.size() ? classes[resident_class].look : resident_class;
}

bool has_services(const House& house, const assets::BuildingTable& buildings, std::uint8_t resident_class) noexcept {
    return has_services(house, service_class(buildings, resident_class));
}

std::uint32_t panel_services(const House& house) noexcept {
    namespace ps = panel_service;
    struct Map {
        std::uint16_t house;
        std::uint32_t panel;
    };
    static constexpr std::array<Map, 10> kMap{{
        {hs::kKapelle, ps::kKapelle},
        {hs::kSchule, ps::kSchule},
        {hs::kWirtshaus, ps::kWirtshaus},
        {hs::kTheater, ps::kTheater},
        {hs::kKirche, ps::kKirche | ps::kKapelle},
        {hs::kBadehaus, ps::kBadehaus},
        {hs::kHochschule, ps::kHochschule | ps::kSchule},
        {hs::kKlinik, ps::kKlinik},
        {hs::kGalgen, ps::kGalgen},
        {hs::kBrunnen, ps::kBrunnen},
    }};
    std::uint32_t panel = house.market ? ps::kMarkt : 0U;
    for (const Map& m : kMap) {
        if ((house.services & m.house) != 0) {
            panel |= m.panel;
        }
    }
    return panel;
}

std::uint32_t required_panel_services(std::uint8_t resident_class) noexcept {
    if (resident_class >= kServiceNeeds.size()) {
        return 0;
    }
    std::uint32_t panel = 0;
    for (const std::uint16_t any : kServiceNeeds[resident_class]) {
        if (any == 0) {
            continue;
        }
        if ((any & kMarketNeed) != 0) {
            panel |= panel_service::kMarkt;
            continue;
        }
        // The lowest bit is the lesser building; of what it shows (a church also as a chapel), its
        // own icon is the highest bit.
        House one;
        one.services = static_cast<std::uint16_t>(any & (0U - any));
        panel |= std::bit_floor(panel_services(one));
    }
    return panel;
}

namespace {

/// update_services of one island; with `cities`, only for the houses of those cities.
void services_on(World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                 const std::set<std::uint8_t>* cities) {
    struct Source {
        std::uint8_t city;
        std::int32_t x;
        std::int32_t y;
        std::uint32_t w;
        std::uint32_t h;
        std::uint8_t radius;
        std::uint16_t bit;  // 0 for markets
    };
    std::vector<Source> sources;
    const IslandMap& map = world.islands()[island].map;
    for (const Production& p : world.productions()) {
        if (!p.used || p.island != island) {
            continue;
        }
        const MapCell& cell = map.top(p.x, p.y);
        if (cities != nullptr && cities->count(cell.city) == 0) {
            continue;
        }
        const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
        if (b == nullptr) {
            continue;
        }
        const std::uint16_t bit = service_bit(b->prod_kind);
        if (bit == 0 && b->prod_kind != prod::kMarket) {
            continue;
        }
        std::uint32_t w = 1;
        std::uint32_t h = 1;
        rotated_size(*b, cell.rotation, w, h);
        sources.push_back(Source{cell.city, p.x, p.y, w, h, b->radius, bit});
    }
    // By city: a house looks only at its own city's (review point 19). The result does not depend
    // on the order (bits are or-ed, the market distance is a minimum).
    std::sort(sources.begin(), sources.end(), [](const Source& a, const Source& b) { return a.city < b.city; });
    const auto by_city = [](const Source& s, std::uint8_t c) { return s.city < c; };
    const Tables& t = tables();
    for (House& house : world.houses()) {
        if (house.island != island || (cities != nullptr && cities->count(house.city) == 0)) {
            continue;
        }
        const MapCell& cell = map.top(house.x, house.y);
        const assets::BuildingType* type = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
        std::uint32_t hw = 2;
        std::uint32_t hh = 2;
        if (type != nullptr) {
            rotated_size(*type, cell.rotation, hw, hh);
        }
        // Fix S-030: the original keeps the fountain bit when it recomputes.
        house.services &= hs::kDisasterMask;
        house.market = false;
        house.market_distance = kNoMarket;
        for (auto it = std::lower_bound(sources.begin(), sources.end(), house.city, by_city);
             it != sources.end() && it->city == house.city; ++it) {
            const Source& s = *it;
            const std::int32_t dx = gap(s.x, s.w, house.x, hw);
            const std::int32_t dy = gap(s.y, s.h, house.y, hh);
            const auto r = static_cast<std::int32_t>(s.radius);
            if (dx > r || dy > r) {
                continue;
            }
            const std::vector<std::uint8_t>& circle = circle_table(s.radius);
            if (dx > circle[static_cast<std::size_t>(dy)]) {
                continue;
            }
            if (s.bit != 0) {
                house.services = static_cast<std::uint16_t>(house.services | s.bit);
            } else {
                house.market = true;
                const std::uint8_t d =
                    t.distance[static_cast<std::size_t>(std::min(dx, 16))][static_cast<std::size_t>(std::min(dy, 16))];
                house.market_distance = std::min(house.market_distance, d);
            }
        }
        house.services_complete = has_services(house, buildings, house.resident_class);
    }
}

}  // namespace

void update_services(World& world, const assets::BuildingTable& buildings, std::uint16_t island) {
    services_on(world, buildings, island, nullptr);
}

void update_services(World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                     const std::set<std::uint8_t>& cities) {
    services_on(world, buildings, island, &cities);
}

void update_services(World& world, const assets::BuildingTable& buildings) {
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        update_services(world, buildings, i);
    }
}

std::int32_t growth_rate(const City& city, const House& house) noexcept {
    const Tables& t = tables();
    const std::uint8_t c = house.resident_class;
    const std::uint8_t s =
        house.market ? satisfaction_of_class(city, c) : (c == 0 ? satisfaction_of_class(city, 0) : 0);
    const bool fed = house.market || (house.services & hs::kKontor) != 0;
    const std::uint8_t food = fed ? std::min(city.food_ratio, kFull) : 0;
    const std::uint16_t disaster = house.services & hs::kDisasterMask;
    const std::size_t dist = std::min<std::size_t>(house.market_distance, kNoMarket);
    if (food > 87 && s > 107 && house.services_complete && disaster == 0) {
        // A fire or plague anywhere in the city stops all growth (city + 0x1FE).
        if (food >= kFull && s >= kFull && city.disasters == 0) {
            return (128 - t.t1[food]) * t.t2[dist] / 128;
        }
        return 0;  // nearly content: the house neither grows nor shrinks
    }
    return -(((t.t3[s] + t.t4[food]) * t.t5[dist]) >> 7) - (house.services_complete ? 0 : 64) - kDisasterLoss[disaster];
}

void advance_houses(World& world, const assets::BuildingTable& buildings, std::uint32_t dt_ms,
                    std::vector<HouseEvent>* events) {
    HouseClocks& clocks = world.house_clocks();
    std::array<bool, HouseClocks::kGroups> ticked{};
    bool any = false;
    for (std::size_t g = 0; g < HouseClocks::kGroups; ++g) {
        clocks.time_ms[g] += dt_ms;
        const std::uint32_t period = house_group_period_ms(static_cast<std::uint32_t>(g));
        if (clocks.time_ms[g] >= period) {
            // Fix T-014: the original drops the rest of the step (growth depends on the frame rate).
            clocks.time_ms[g] -= period;
            clocks.epoch[g] = static_cast<std::uint8_t>((clocks.epoch[g] + 1) & 7U);
            ticked[g] = true;
            any = true;
        }
    }
    if (!any) {
        return;
    }
    // Positions first: visits may remove houses.
    struct Due {
        std::uint16_t island;
        std::uint16_t x;
        std::uint16_t y;
    };
    std::vector<Due> due;
    for (House& h : world.houses()) {
        const std::size_t g = h.timer_group % HouseClocks::kGroups;
        if (ticked[g] && h.epoch != clocks.epoch[g]) {
            h.epoch = clocks.epoch[g];
            due.push_back(Due{h.island, h.x, h.y});
        }
    }
    HouseVisitor visitor(world, buildings, events);
    for (const Due& d : due) {
        const House* h = world.house(d.island, d.x, d.y);
        if (h == nullptr) {
            continue;
        }
        City* city = world.city(h->island, h->city);
        const Player* owner = city != nullptr ? world.player(city->owner) : nullptr;
        if (owner == nullptr || (owner->role != PlayerRole::Human && owner->role != PlayerRole::Computer)) {
            continue;
        }
        visitor.visit(*city, d.island, d.x, d.y);
    }
}

}  // namespace o1602::sim
