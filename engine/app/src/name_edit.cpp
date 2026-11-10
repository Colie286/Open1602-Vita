// SPDX-License-Identifier: GPL-3.0-or-later
#include "name_edit.hpp"

#include <utility>

namespace o1602::app {

namespace {

constexpr int kBackspace = 8;
constexpr int kEnter = 13;
constexpr int kEscape = 27;

[[nodiscard]] std::string_view trimmed(std::string_view s) noexcept {
    const std::size_t first = s.find_first_not_of(' ');
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

}  // namespace

NameTarget NameTarget::of(const sim::Ship& ship) noexcept {
    NameTarget t;
    t.kind = Kind::Ship;
    t.ship = ship.id;
    return t;
}

NameTarget NameTarget::of(const sim::City& city) noexcept {
    NameTarget t;
    t.kind = Kind::City;
    t.island = city.island;
    t.city = city.index;
    return t;
}

const std::string* name_of(const sim::World& world, const NameTarget& target) {
    switch (target.kind) {
        case NameTarget::Kind::Ship: {
            const sim::Ship* s = world.ship(target.ship);
            return s != nullptr ? &s->name : nullptr;
        }
        case NameTarget::Kind::City: {
            const sim::City* c = world.city(target.island, target.city);
            return c != nullptr ? &c->name : nullptr;
        }
    }
    return nullptr;
}

bool can_rename(const sim::World& world, sim::PlayerId player, const NameTarget& target) {
    if (player == sim::kNoPlayer) {
        return false;
    }
    switch (target.kind) {
        case NameTarget::Kind::Ship: {
            const sim::Ship* s = world.ship(target.ship);
            return s != nullptr && s->owner == player && !s->sinking;
        }
        case NameTarget::Kind::City: {
            const sim::City* c = world.city(target.island, target.city);
            return c != nullptr && c->owner == player;
        }
    }
    return false;
}

sim::Command rename_command(sim::PlayerId player, const NameTarget& target, std::string name) {
    if (target.kind == NameTarget::Kind::Ship) {
        sim::ShipCommand o;
        o.player = player;
        o.ship = target.ship;
        o.op = sim::ShipCommand::Op::Rename;
        o.name = std::move(name);
        return o;
    }
    sim::CityOrder o;
    o.player = player;
    o.island = target.island;
    o.city = target.city;
    o.op = sim::CityOrder::Op::Rename;
    o.name = std::move(name);
    return o;
}

std::optional<sim::Command> NameEdit::start(const sim::World& world, sim::PlayerId player, const NameTarget& target) {
    m_clicked_name = true;
    if (m_target == target && m_player == player) {
        return std::nullopt;  // a second click on the name being typed
    }
    std::optional<sim::Command> kept = commit();
    const std::string* name = name_of(world, target);
    if (name == nullptr || !can_rename(world, player, target)) {
        return kept;
    }
    m_target = target;
    m_player = player;
    m_original = *name;
    m_text = name->substr(0, kMaxLength);
    m_shown = true;
    return kept;
}

std::string NameEdit::shown(const NameTarget& target, std::string_view name) {
    if (m_target != target) {
        return std::string(name);
    }
    m_shown = true;
    return m_text + kCursor;
}

void NameEdit::type(std::string_view text) {
    if (!m_target) {
        return;
    }
    for (const char c : text) {
        const auto b = static_cast<unsigned char>(c);
        if (b < 0x20 || b == 0x7F) {
            continue;  // the keys that edit come through key()
        }
        if (m_text.size() >= kMaxLength) {
            break;
        }
        m_text.push_back(c);
    }
}

std::optional<sim::Command> NameEdit::key(int key) {
    if (!m_target) {
        return std::nullopt;
    }
    switch (key) {
        case kBackspace:
            if (!m_text.empty()) {
                m_text.pop_back();
            }
            return std::nullopt;
        case kEnter:
            return commit();
        case kEscape:
            cancel();
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

std::optional<sim::Command> NameEdit::commit() {
    if (!m_target) {
        return std::nullopt;
    }
    const NameTarget target = *m_target;
    const sim::PlayerId player = m_player;
    // Spaces at the ends are dropped; an empty name keeps the old one (the original's input sends it).
    const std::string name(trimmed(m_text));
    const bool changed = name != m_original;
    cancel();
    if (!changed || !sim::valid_name(name, kMaxLength)) {
        return std::nullopt;
    }
    return rename_command(player, target, name);
}

void NameEdit::cancel() noexcept {
    m_target.reset();
    m_player = sim::kNoPlayer;
    m_text.clear();
    m_original.clear();
    m_shown = false;
}

std::optional<sim::Command> NameEdit::end_click() {
    if (!m_target || m_clicked_name) {
        return std::nullopt;
    }
    return commit();
}

std::optional<sim::Command> NameEdit::end_frame(const sim::World& world) {
    if (!m_target) {
        return std::nullopt;
    }
    if (name_of(world, *m_target) == nullptr || !can_rename(world, m_player, *m_target)) {
        cancel();  // sunk, sold or taken: nobody may name it any more
        return std::nullopt;
    }
    if (!std::exchange(m_shown, false)) {
        return commit();  // the window was closed or shows something else
    }
    return std::nullopt;
}

}  // namespace o1602::app
