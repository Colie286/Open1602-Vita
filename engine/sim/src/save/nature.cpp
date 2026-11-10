// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animals, fires and plagues, volcanoes: ANIM, DISA, VOLC.
#include <algorithm>

#include "section_io.hpp"

namespace o1602::sim::save {

namespace {

template <class A, class T>
void io_animal(A& a, T& v) {
    a.u16(v.island);
    a.u16(v.x);
    a.u16(v.y);
    a.u16(v.figure);
}

template <class A, class D>
void io_disaster(A& a, D& d) {
    a.u8(d.kind);
    a.u16(d.island);
    a.u16(d.x);
    a.u16(d.y);
    a.u8(d.elapsed);
    a.u8(d.duration);
    a.u32(d.id);
}

template <class A, class C>
void io_crater(A& a, C& c, std::uint16_t version) {
    a.u16(c.island);
    io_coordinate(a, c.x, version);
    io_coordinate(a, c.y, version);
}

template <class A, class V>
void io_volcanoes(A& a, V& v, std::uint16_t version) {
    a.u64(v.next_ms);
    // The crater that erupted last: whether there is one, and the crater (0, 0, 0 without one).
    bool has_last = v.last.has_value();
    CraterRef last = v.last.value_or(CraterRef{});
    a.boolean(has_last);
    io_crater(a, last, version);
    if constexpr (A::kReading) {
        if (has_last) {
            v.last = last;
        }
    }
    a.list(v.eruptions, version >= kWideCoordinates ? 17 : 15, [&](auto& e) {
        io_crater(a, e.crater, version);
        a.u16(e.seed);
        a.u8(e.step);
        a.u64(e.next_us);
    });
    a.list(v.lava, 86, [&](auto& p) {
        a.u16(p.island);
        a.i64(p.x);
        a.i64(p.y);
        a.i64(p.z);
        a.i64(p.dx);
        a.i64(p.dy);
        a.i64(p.dz);
        a.i64(p.speed);
        a.u64(p.launch_us);
        a.u64(p.leg_start_us);
        a.u64(p.leg_end_us);
        a.u8(p.heading);
        a.u8(p.tilt);
        a.boolean(p.grounded);
        a.u8(p.anim);
    });
}

}  // namespace

void write_animals(Writer& out, const World& w) {
    out.list(w.animals(), 8, [&](const Animal& v) { io_animal(out, v); });
}

std::vector<Animal> read_animals(Reader& in) {
    std::vector<Animal> list;
    in.list(list, 8, [&](Animal& v) { io_animal(in, v); });
    return list;
}

void write_disasters(Writer& out, const World& w) {
    out.list(w.disasters(), 13, [&](const Disaster& d) { io_disaster(out, d); });
}

std::vector<Disaster> read_disasters(Reader& in) {
    std::vector<Disaster> list;
    in.list(list, 13, [&](Disaster& d) { io_disaster(in, d); });
    return list;
}

/// The version of VOLC: kWideCoordinates if the last or an erupting crater lies beyond 8 bits.
std::uint16_t volcanoes_version(const World& w) {
    const VolcanoState& v = w.volcanoes();
    const bool last = v.last && wide(v.last->x, v.last->y);
    const bool running = std::any_of(v.eruptions.begin(), v.eruptions.end(),
                                     [](const Eruption& e) { return wide(e.crater.x, e.crater.y); });
    return last || running ? kWideCoordinates : 1;
}

void write_volcanoes(Writer& out, const World& w, std::uint16_t version) {
    io_volcanoes(out, w.volcanoes(), version);
}

VolcanoState read_volcanoes(Reader& in, std::uint16_t version) {
    VolcanoState v;
    io_volcanoes(in, v, version);
    return v;
}

}  // namespace o1602::sim::save
