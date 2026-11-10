// SPDX-License-Identifier: GPL-3.0-or-later
//
// Orders of ships and units beyond the current one: QUEU (orders waiting behind it, Shift + right
// click) and STNC (attack move and hold position, ADR 0010). Both list only the ships and units
// that have any, by id: first the ships, then the units.
#include <iterator>

#include "section_io.hpp"

namespace o1602::sim::save {

namespace {

template <class A, class Q>
void io_queue(A& a, Q& queue) {
    // The commands keep a queue at kMaxQueuedOrders, so its length fits the byte.
    static_assert(kMaxQueuedOrders <= 0xFF);
    auto count = static_cast<std::uint8_t>(queue.size());
    a.u8(count);
    if constexpr (A::kReading) {
        queue.resize(count);
    }
    for (auto& t : queue) {
        io_target(a, t);
    }
    if constexpr (A::kReading) {
        if (queue.size() > kMaxQueuedOrders) {
            queue.resize(kMaxQueuedOrders);
        }
    }
}

/// Queues by id; each takes at least 5 bytes (id and length).
template <class A, class L>
void io_queues(A& a, L& list) {
    a.list(list, 5, [&](auto& item) {
        a.u32(item.first);
        io_queue(a, item.second);
    });
}

/// Stances by id: bit 0 attack move, bit 1 hold position.
template <class A, class L>
void io_stances(A& a, L& list) {
    a.list(list, 5, [&](auto& item) {
        a.u32(item.first);
        a.u8(item.second);
    });
}

template <class Units>
Decoded::Queues queues_of(const Units& units) {
    Decoded::Queues list;
    for (const auto& u : units) {
        if (!u.queue.empty()) {
            list.emplace_back(u.id, u.queue);
        }
    }
    return list;
}

template <class Units>
Decoded::Stances stances_of(const Units& units) {
    Decoded::Stances list;
    for (const auto& u : units) {
        const auto flags = static_cast<std::uint8_t>((u.attack_move ? 1U : 0U) | (u.hold ? 2U : 0U));
        if (flags != 0) {
            list.emplace_back(u.id, flags);
        }
    }
    return list;
}

}  // namespace

void write_queues(Writer& out, const World& w) {
    const Decoded::Queues ships = queues_of(w.ships());
    const Decoded::Queues soldiers = queues_of(w.soldiers());
    io_queues(out, ships);
    io_queues(out, soldiers);
}

// The lists read are added to those of the section read before, if there was one: a file with
// the section twice applies both, the later last.
void read_queues(Reader& in, Decoded::Queues& ships, Decoded::Queues& soldiers) {
    for (Decoded::Queues* to : {&ships, &soldiers}) {
        Decoded::Queues list;
        io_queues(in, list);
        to->insert(to->end(), std::make_move_iterator(list.begin()), std::make_move_iterator(list.end()));
    }
}

void write_stances(Writer& out, const World& w) {
    const Decoded::Stances ships = stances_of(w.ships());
    const Decoded::Stances soldiers = stances_of(w.soldiers());
    io_stances(out, ships);
    io_stances(out, soldiers);
}

void read_stances(Reader& in, Decoded::Stances& ships, Decoded::Stances& soldiers) {
    for (Decoded::Stances* to : {&ships, &soldiers}) {
        Decoded::Stances list;
        io_stances(in, list);
        to->insert(to->end(), list.begin(), list.end());
    }
}

}  // namespace o1602::sim::save
