// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the islands keep beyond their tiles: ISLE (deer, drought, craters, deposits) and XPLR (who
// explored them and knows their deposits).
#include <algorithm>
#include <utility>

#include "section_io.hpp"

namespace o1602::sim::save {

namespace {

template <class A, class I>
void io_island(A& a, I& i, std::uint16_t version) {
    if (version >= kWideIslands) {
        a.u16(i.number);
    } else {
        a.u8(i.number);
    }
    a.u8(i.deer_phase);
    for (auto& s : i.deer_spots) {
        a.boolean(s.used);
        io_coordinate(a, s.x, version);
        io_coordinate(a, s.y, version);
        a.u64(s.respawn_ms);
    }
    a.u8(i.drought.strength);
    a.u64(i.drought.end_ms);
    a.boolean(i.drought.curse_pending);
    a.list(i.craters, version >= kWideCoordinates ? 6 : 4, [&](auto& c) {
        io_coordinate(a, c.x, version);
        io_coordinate(a, c.y, version);
        a.u16(c.steps_left);
    });
    a.boolean(i.volcano_erupted);
    a.list(i.deposits, version >= kWideIslands ? 9 : 7, [&](auto& d) {
        a.u8(d.ware);
        if (version >= kWideIslands) {
            a.u16(d.x);
            a.u16(d.y);
        } else {
            a.u8(d.x);
            a.u8(d.y);
        }
        a.u8(d.mask);  // the first 8 players; all bits in XPLR
        a.u8(d.kind);
        a.u16(d.amount);
    });
    a.u32(i.fertility);
}

template <class A, class E>
void io_explored(A& a, E& e) {
    a.bools(e.explored);
    a.u32s(e.deposit_masks);
}

}  // namespace

/// The version of ISLE: kWideIslands if an island's number or a deposit lies beyond 8 bits, else
/// kWideCoordinates if a deer spot or a crater does.
std::uint16_t islands_version(const World& w) {
    for (const WorldIsland& i : w.islands()) {
        const bool deposits = std::any_of(i.header.deposits.begin(), i.header.deposits.end(),
                                          [](const assets::IslandHeader::Deposit& d) { return wide(d.x, d.y); });
        if (i.header.number > 0xFF || deposits) {
            return kWideIslands;
        }
    }
    for (const WorldIsland& i : w.islands()) {
        const bool spots =
            std::any_of(i.deer_spots.begin(), i.deer_spots.end(), [](const DeerSpot& s) { return wide(s.x, s.y); });
        const bool craters =
            std::any_of(i.craters.begin(), i.craters.end(), [](const Crater& c) { return wide(c.x, c.y); });
        if (spots || craters) {
            return kWideCoordinates;
        }
    }
    return 1;
}

void write_islands(Writer& out, const World& w, std::uint16_t version) {
    out.list(w.islands(), 2, [&](const WorldIsland& i) {
        Decoded::IslandPart part;
        part.number = i.header.number;
        part.deer_phase = i.deer_phase;
        part.deer_spots = i.deer_spots;
        part.drought = i.drought;
        part.craters = i.craters;
        part.volcano_erupted = i.volcano_erupted;
        part.deposits = i.header.deposits;
        part.fertility = i.header.fertility;
        io_island(out, std::as_const(part), version);
    });
}

std::vector<Decoded::IslandPart> read_islands(Reader& in, std::uint16_t version) {
    std::vector<Decoded::IslandPart> list;
    in.list(list, 2, [&](Decoded::IslandPart& i) { io_island(in, i, version); });
    return list;
}

void write_explored(Writer& out, const World& w) {
    out.list(w.islands(), 8, [&](const WorldIsland& i) {
        Decoded::ExplorePart part;
        part.explored = i.explored;
        for (const auto& d : i.header.deposits) {
            part.deposit_masks.push_back(d.mask);
        }
        io_explored(out, std::as_const(part));
    });
}

std::vector<Decoded::ExplorePart> read_explored(Reader& in) {
    std::vector<Decoded::ExplorePart> list;
    in.list(list, 8, [&](Decoded::ExplorePart& part) { io_explored(in, part); });
    return list;
}

}  // namespace o1602::sim::save
