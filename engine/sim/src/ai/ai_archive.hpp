// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saving and loading the computer players' state with one description per record: the same
// `io(archive, record)` writes and reads, so both sides cannot drift apart.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "ai_state.hpp"

#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/state_io.hpp"

namespace o1602::sim::ai {

class Saver {
public:
    /// Writes the newest version; an older one only for the tests of the loader.
    explicit Saver(core::ByteWriter& out, std::uint32_t version = kStateVersion) : m_out(out), m_version(version) {}
    static constexpr bool kLoading = false;
    [[nodiscard]] std::uint32_t version() const { return m_version; }
    void operator()(bool& v) { m_out.boolean(v); }
    void operator()(std::uint8_t& v) { m_out.u8(v); }
    void operator()(std::uint16_t& v) { m_out.u16(v); }
    void operator()(std::uint32_t& v) { m_out.u32(v); }
    void operator()(std::uint64_t& v) { m_out.u64(v); }
    void operator()(std::int32_t& v) { m_out.i32(v); }
    void operator()(std::int64_t& v) { m_out.i64(v); }
    template <class T>
    void operator()(std::vector<T>& v) {
        m_out.u32(static_cast<std::uint32_t>(v.size()));
        for (T& e : v) {
            (*this)(e);
        }
    }
    template <class T, std::size_t N>
    void operator()(std::array<T, N>& v) {
        for (T& e : v) {
            (*this)(e);
        }
    }
    template <class T>
    void operator()(std::optional<T>& v) {
        bool has = v.has_value();
        (*this)(has);
        if (has) {
            (*this)(*v);
        }
    }
    template <class A, class B>
    void operator()(std::pair<A, B>& v) {
        (*this)(v.first);
        (*this)(v.second);
    }
    template <class T>
        requires requires(Saver& s, T& t) { io(s, t); }
    void operator()(T& v) {
        io(*this, v);
    }
    [[nodiscard]] bool ok() const { return true; }
    void fail() {}

private:
    core::ByteWriter& m_out;
    std::uint32_t m_version;
};

class Loader {
public:
    explicit Loader(StateIn& in, std::uint32_t version = kStateVersion) : m_in(in), m_version(version) {}
    static constexpr bool kLoading = true;
    /// The state version read.
    [[nodiscard]] std::uint32_t version() const { return m_version; }
    void operator()(bool& v) { v = m_in.boolean(); }
    void operator()(std::uint8_t& v) { v = m_in.u8(); }
    void operator()(std::uint16_t& v) { v = m_in.u16(); }
    void operator()(std::uint32_t& v) { v = m_in.u32(); }
    void operator()(std::uint64_t& v) { v = m_in.u64(); }
    void operator()(std::int32_t& v) { v = m_in.i32(); }
    void operator()(std::int64_t& v) { v = m_in.i64(); }
    template <class T>
    void operator()(std::vector<T>& v) {
        m_in.list(v, 1, [this](T& e) { (*this)(e); });
    }
    template <class T, std::size_t N>
    void operator()(std::array<T, N>& v) {
        for (T& e : v) {
            (*this)(e);
        }
    }
    template <class T>
    void operator()(std::optional<T>& v) {
        bool has = false;
        (*this)(has);
        if (has) {
            T t{};
            (*this)(t);
            v = std::move(t);
        } else {
            v.reset();
        }
    }
    template <class A, class B>
    void operator()(std::pair<A, B>& v) {
        (*this)(v.first);
        (*this)(v.second);
    }
    template <class T>
        requires requires(Loader& s, T& t) { io(s, t); }
    void operator()(T& v) {
        io(*this, v);
    }
    [[nodiscard]] bool ok() const { return m_in.ok(); }
    void fail() { m_in.fail(); }

private:
    StateIn& m_in;
    std::uint32_t m_version;
};

template <class A>
void io(A& a, SlotState& s) {
    a(s.count);
    a(s.attempts);
    a(s.fail_level);
}

/// The slots of a city record or of a player (kAllSlots). From state version 5 on, their number up to
/// the last one in use (at least kSlots) and then each; before, the original's kSlots.
template <class A, class T>
void slot_array(A& a, std::array<T, kAllSlots>& v) {
    std::uint32_t n = kSlots;
    if (a.version() >= kStateWithPackageSlots) {
        if constexpr (!A::kLoading) {
            for (std::uint32_t k = kAllSlots; k > kSlots; --k) {
                if (!(v[k - 1] == T{})) {
                    n = k;
                    break;
                }
            }
        }
        a(n);
        if (n < kSlots || n > kAllSlots) {
            a.fail();
            return;
        }
    }
    for (std::uint32_t k = 0; k < n; ++k) {
        a(v[k]);
    }
}

/// A value of 64 bits that versions before kStateWithWideFlags held in a `Narrow`.
template <class Narrow, class A>
void wide_from_6(A& a, std::uint64_t& v) {
    if (a.version() >= kStateWithWideFlags) {
        a(v);
        return;
    }
    auto narrow = static_cast<Narrow>(v);
    a(narrow);
    if constexpr (A::kLoading) {
        v = narrow;
    }
}

template <class A>
void io(A& a, GuardPost& g) {
    a(g.x);
    a(g.y);
}

template <class A>
void io(A& a, CityPlan& r) {
    a(r.island);
    a(r.city);
    a(r.sweep_x);
    a(r.sweep_y);
    a(r.tower_ring);
    a(r.paving_ring);
    a(r.stage);
    a(r.stage_ms);
    a(r.land);
    a(r.land_percent);
    wide_from_6<std::uint32_t>(a, r.flags);
    slot_array(a, r.slots);
    a(r.doctor);
    a(r.posts);
    a(r.post_cursor);
}

template <class A>
void io(A& a, Segment& s) {
    a(s.x0);
    a(s.y0);
    a(s.x1);
    a(s.y1);
    a(s.dx);
    a(s.dy);
    a(s.dir);
    a(s.length);
    a(s.open);
}

template <class A>
void io(A& a, Search& s) {
    a(s.segment);
    a(s.x);
    a(s.y);
    a(s.remaining);
    a(s.ref_x);
    a(s.ref_y);
    a(s.radius);
    a(s.building);
    wide_from_6<std::uint32_t>(a, s.service);
    a(s.slot);
    a(s.phase);
    a(s.best_x0);
    a(s.best_y0);
    a(s.best_x1);
    a(s.best_y1);
    a(s.best);
    a(s.extra);
    a(s.rotation);
    a(s.stub_x);
    a(s.stub_y);
    if (a.version() >= 4) {
        a(s.target_x);
        a(s.target_y);
    }
}

template <class A>
void io(A& a, Reservation& r) {
    a(r.money);
    a(r.wood);
    a(r.bricks);
    a(r.tools);
    a(r.cannons);
    a(r.island);
    a(r.city);
}

template <class A>
void io(A& a, Region& r) {
    a(r.x0);
    a(r.y0);
    a(r.x1);
    a(r.y1);
    a(r.area);
}

template <class A>
void io(A& a, Settlement& s) {
    a(s.active);
    a(s.failed);
    a(s.last_ms);
    a(s.need);
    a(s.floor);
    a(s.ship);
    a(s.cursor);
    a(s.pass);
    a(s.crop);
    a(s.previous_crop);
    a(s.island);
    a(s.site_x);
    a(s.site_y);
    a(s.site_rotation);
    a(s.retries);
    a(s.sailing);
    a(s.regions);
}

template <class A>
void io(A& a, CastleEntry& c) {
    a(c.x);
    a(c.y);
    a(c.rally_x);
    a(c.rally_y);
}

template <class A>
void io(A& a, AttackPlan::Candidate& c) {
    a(c.x);
    a(c.y);
    a(c.flag);
}

template <class A>
void io(A& a, AttackPlan& p) {
    a(p.active);
    a(p.island);
    a(p.city);
    a(p.ship);
    a(p.targets);
    a(p.phase);
    a(p.cursor);
    a(p.candidates);
    a(p.best_x);
    a(p.best_y);
    a(p.best_score);
    a(p.best_flag);
}

template <class A>
void io(A& a, ShipNote& n) {
    a(n.id);
    a(n.mission);
    a(n.param);
    a(n.counter);
    a(n.cooldown);
    a(n.patrol);
    a(n.patrol_island);
    a(n.last_stop);
    a(n.supply);
}

template <class A>
void io(A& a, IslandWatch& w) {
    if (a.version() < 2) {
        std::uint8_t state = 0;  // read nowhere
        a(state);
    }
    a(w.next_ms);
}

template <class A>
void io(A& a, AiPlayer& p) {
    a(p.player);
    a(p.pirates);
    a(p.t_main);
    a(p.t_10s);
    a(p.t_6s);
    a(p.init_ms);
    a(p.refresh_ms);
    a(p.cities);
    a(p.surveyed);
    a(p.stack);
    a(p.budget);
    a(p.settle);
    a(p.colony_failed);
    a(p.has_city);
    a(p.island);
    a(p.city);
    a(p.has_plan);
    a(p.city_cursor);
    a(p.home_valid);
    a(p.home_island);
    a(p.home_city);
    a(p.skip_layout);
    a(p.max_stage);
    a(p.score);
    slot_array(a, p.present);
    a(p.plans);
    a(p.search);
    a(p.segments);
    a(p.reserved);
    a(p.yard_valid);
    a(p.yard_island);
    a(p.yard_x);
    a(p.yard_y);
    a(p.castle_island);
    a(p.castles);
    a(p.castle_cursor);
    a(p.recruit_cursor);
    a(p.target_cursor);
    a(p.previous_mood);
    if (a.version() < 2) {
        std::vector<AttackPlan> attacks;  // never planned
        a(attacks);
    }
    a(p.diplomacy_ms);
    a(p.seen_treaties);
    a(p.diplomacy_due);
    a(p.ships);
    a(p.fleet_target);
    a(p.ships_per_city);
    if (a.version() >= 3) {
        a(p.yard_wanted);
    }
}

template <class A>
void io(A& a, State& s) {
    a(s.players);
    a(s.max_score);
    a(s.islands);
    a(s.island_cursor);
    a(s.initialised);
}

}  // namespace o1602::sim::ai
