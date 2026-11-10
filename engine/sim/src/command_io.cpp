// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/command_io.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "o1602/sim/state_io.hpp"

namespace o1602::sim {

namespace {

// One description per command serves writing and reading (`io` below): the writer and the reader
// visit the same fields in the same order, so the two cannot drift apart.

// The fields of each command, in the order they travel. Changing an order changes the format.

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, TilePos>
{
    a(t.x);
    a(t.y);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, Target>
{
    a.enumeration(t.kind, Target::Kind::Spot);
    a(t.island);
    a(t.city);
    a(t.x);
    a(t.y);
    a(t.ship);
    a(t.raw);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, BuildItem>
{
    a(t.building);
    a(t.x);
    a(t.y);
    a(t.rotation);
    a(t.anim_phase);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, RouteEntry>
{
    a(t.ware);
    a(t.unload);
    a(t.amount);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, BuildOrder>
{
    a(t.player);
    a(t.island);
    a(t.items);
    a(t.ship);
    a(t.orient_to_road);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, DemolishOrder>
{
    a(t.player);
    a(t.island);
    a(t.from);
    a(t.to);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, ShipOrder>
{
    a(t.player);
    a(t.ship);
    a(t.target);
    a(t.attack);
    a(t.attack_move);
    a(t.queue);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, CargoTransfer>
{
    a(t.player);
    a(t.ship);
    a(t.ware);
    a(t.to_ship);
    a(t.amount);
    a(t.hold);
    a(t.kontor_island);
    a(t.kontor_city);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, RouteEdit>
{
    a(t.player);
    a(t.ship);
    a.enumeration(t.op, RouteEdit::Op::CopyFrom);
    a(t.stop);
    a(t.entry);
    a(t.target);
    a(t.value);
    a(t.on);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, TradeSetting>
{
    a(t.player);
    a(t.island);
    a(t.city);
    a(t.ware);
    a.enumeration(t.mode, TradeSetting::Mode::Buy);
    a(t.price);
    a(t.amount);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, ShipyardOrder>
{
    a(t.player);
    a(t.island);
    a(t.x);
    a(t.y);
    a.enumeration(t.op, ShipyardOrder::Op::Repair);
    a(t.ship_type);
    a(t.on);
    a(t.ship);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, ExploreOrder>
{
    a(t.player);
    a(t.ship);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, DiplomacyOrder>
{
    a(t.player);
    a(t.partner);
    a.enumeration(t.kind, DiplomacyOrder::Kind::AskFounding);
    a.enumeration(t.value, Treaty::Signed);
    a(t.friendly);
    a(t.gold);
    a(t.ship);
    a(t.target);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, SoldierOrder>
{
    a(t.player);
    a(t.soldiers);
    a(t.target);
    a(t.attack);
    a(t.attack_move);
    a(t.formation);
    a(t.offsets);
    a(t.queue);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, SoldierCommand>
{
    a(t.player);
    a(t.soldier);
    a.enumeration(t.op, SoldierCommand::Op::Hold);
    a(t.index);
    a(t.point);
    a(t.on);
    a(t.one_way);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, CastleOrder>
{
    a(t.player);
    a(t.island);
    a(t.x);
    a(t.y);
    a.enumeration(t.op, CastleOrder::Op::ReleaseAll);
    a(t.figure);
    a(t.slot);
    a(t.spot);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, UnloadOrder>
{
    a(t.player);
    a(t.ship);
    a(t.hold);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, ProductionSwitch>
{
    a(t.player);
    a(t.island);
    a(t.x);
    a(t.y);
    a(t.on);
    a.enumeration(t.op, ProductionSwitch::Op::Pickup);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, CityOrder>
{
    a(t.player);
    a(t.island);
    a(t.city);
    a.enumeration(t.op, CityOrder::Op::MaterialLock);
    a(t.resident_class);
    a(t.rate);
    a(t.on);
    a(t.name);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, ShipCommand>
{
    a(t.player);
    a(t.ship);
    a.enumeration(t.op, ShipCommand::Op::ToPirates);
    a(t.hold);
    a(t.factor);
    a(t.name);
    a(t.on);
    a(t.x);
    a(t.y);
    a(t.count);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, GameSpeedOrder>
{
    a(t.player);
    a.enumeration(t.op, GameSpeedOrder::Op::Resume);
    a(t.speed);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, PlayerRename>
{
    a(t.player);
    a(t.name);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, PlayerControl>
{
    a(t.player);
    a(t.computer);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, GateOrder>
{
    a(t.player);
    a(t.closed);
}

template <class A, class T>
void io(A& a, T& t)
    requires std::is_same_v<std::remove_const_t<T>, CartRequest>
{
    a(t.player);
    a(t.island);
    a(t.city);
    a(t.x);
    a(t.y);
}

class Writer {
public:
    explicit Writer(core::ByteWriter& out) noexcept : m_out(out) {}

    void operator()(const bool& v) { m_out.boolean(v); }
    void operator()(const std::uint8_t& v) { m_out.u8(v); }
    void operator()(const std::uint16_t& v) { m_out.u16(v); }
    void operator()(const std::uint32_t& v) { m_out.u32(v); }
    void operator()(const std::int16_t& v) { m_out.u16(static_cast<std::uint16_t>(v)); }
    void operator()(const std::int32_t& v) { m_out.i32(v); }
    void operator()(const std::string& v) { m_out.string(v); }
    /// An enum with values 0 to `last`.
    template <class E>
    void enumeration(const E& v, E /*last*/) {
        m_out.u8(static_cast<std::uint8_t>(v));
    }
    template <class T>
    void operator()(const std::optional<T>& v) {
        m_out.boolean(v.has_value());
        if (v) {
            (*this)(*v);
        }
    }
    template <class T>
    void operator()(const std::vector<T>& v) {
        m_out.u32(static_cast<std::uint32_t>(v.size()));
        for (const T& item : v) {
            (*this)(item);
        }
    }
    template <class A, class B>
    void operator()(const std::pair<A, B>& v) {
        (*this)(v.first);
        (*this)(v.second);
    }
    template <class T>
        requires requires(Writer& w, const T& t) { io(w, t); }
    void operator()(const T& v) {
        io(*this, v);
    }

private:
    core::ByteWriter& m_out;
};

class Reader {
public:
    explicit Reader(core::ByteReader& in) noexcept : m_in(in), m_state(in) {}

    [[nodiscard]] bool ok() const noexcept { return m_state.ok(); }
    void fail() noexcept { m_state.fail(); }

    void operator()(bool& v) {
        const std::uint8_t b = m_state.u8();
        if (b > 1) {
            fail();
        }
        v = b != 0;
    }
    void operator()(std::uint8_t& v) { v = m_state.u8(); }
    void operator()(std::uint16_t& v) { v = m_state.u16(); }
    void operator()(std::uint32_t& v) { v = m_state.u32(); }
    void operator()(std::int16_t& v) { v = static_cast<std::int16_t>(m_state.u16()); }
    void operator()(std::int32_t& v) { v = m_state.i32(); }
    void operator()(std::string& v) {
        const std::uint32_t n = m_state.u32();
        if (n > kMaxCommandText) {
            fail();
            return;
        }
        const auto bytes = m_in.bytes(n);
        if (!bytes) {
            fail();
            return;
        }
        v.assign(bytes->begin(), bytes->end());
    }
    template <class E>
    void enumeration(E& v, E last) {
        const std::uint8_t raw = m_state.u8();
        if (raw > static_cast<std::uint8_t>(last)) {
            fail();
        }
        v = static_cast<E>(raw);
    }
    template <class T>
    void operator()(std::optional<T>& v) {
        bool present = false;
        (*this)(present);
        if (present) {
            T value{};
            (*this)(value);
            v = std::move(value);
        } else {
            v.reset();
        }
    }
    template <class T>
    void operator()(std::vector<T>& v) {
        const std::uint32_t n = m_state.u32();
        if (n > kMaxCommandItems || n > m_in.remaining()) {
            fail();
            return;
        }
        v.assign(n, T{});
        for (T& item : v) {
            (*this)(item);
            if (!ok()) {
                return;
            }
        }
    }
    template <class A, class B>
    void operator()(std::pair<A, B>& v) {
        (*this)(v.first);
        (*this)(v.second);
    }
    template <class T>
        requires requires(Reader& r, T& t) { io(r, t); }
    void operator()(T& v) {
        io(*this, v);
    }

private:
    core::ByteReader& m_in;
    StateIn m_state;
};

/// Reads the alternative `I` of the variant, or the one the kind names.
template <std::size_t I = 0>
std::optional<Command> read_kind(Reader& r, std::uint8_t kind) {
    if constexpr (I < std::variant_size_v<Command>) {
        if (kind == I) {
            std::variant_alternative_t<I, Command> c{};
            r(c);
            if (!r.ok()) {
                return std::nullopt;
            }
            return Command{std::move(c)};
        }
        return read_kind<I + 1>(r, kind);
    } else {
        return std::nullopt;  // a kind of a newer version
    }
}

}  // namespace

void write_command(core::ByteWriter& out, const Command& command) {
    static_assert(std::variant_size_v<Command> < 256);
    out.u8(static_cast<std::uint8_t>(command.index()));
    Writer w(out);
    std::visit([&](const auto& c) { w(c); }, command);
}

std::optional<Command> read_command(core::ByteReader& in) {
    const auto kind = in.u8();
    if (!kind) {
        return std::nullopt;
    }
    Reader r(in);
    return read_kind(r, *kind);
}

void write_scheduled(core::ByteWriter& out, const ScheduledCommand& command) {
    out.u64(command.tick);
    write_command(out, command.command);
}

std::optional<ScheduledCommand> read_scheduled(core::ByteReader& in) {
    const auto tick = in.u64();
    if (!tick) {
        return std::nullopt;
    }
    auto command = read_command(in);
    if (!command) {
        return std::nullopt;
    }
    return ScheduledCommand{*tick, std::move(*command)};
}

std::vector<std::uint8_t> encode_command(const Command& command) {
    core::ByteWriter out;
    write_command(out, command);
    return out.take();
}

std::optional<Command> decode_command(std::span<const std::uint8_t> bytes) {
    core::ByteReader in(bytes);
    auto command = read_command(in);
    if (!command || !in.at_end()) {
        return std::nullopt;
    }
    return command;
}

std::optional<std::size_t> command_size(const Command& command) {
    const std::vector<std::uint8_t> bytes = encode_command(command);
    if (!decode_command(bytes)) {
        return std::nullopt;
    }
    return bytes.size();
}

}  // namespace o1602::sim
