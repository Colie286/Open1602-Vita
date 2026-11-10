// SPDX-License-Identifier: GPL-3.0-or-later

#include "stock_notes.hpp"

#include <utility>

#include "o1602/sim/island_map.hpp"

namespace o1602::app {

namespace {

/// Text.cod [GAME]: "Lager:" and "Lager voll".
constexpr std::size_t kGameStock = 0;
constexpr std::size_t kGameStoreFull = 17;

}  // namespace

std::string stock_note_text(const assets::TextTable& texts, const sim::ProductionEvent& event) {
    const std::vector<std::string>& game = texts.section("GAME");
    switch (event.kind) {
        case sim::ProductionEvent::Kind::Produced:
            // sprintf("%s %d"); the table drops the blank at the end of the line.
            return kGameStock < game.size() ? game[kGameStock] + ' ' + std::to_string(event.amount) : std::string{};
        case sim::ProductionEvent::Kind::StoreFull:
            return kGameStoreFull < game.size() ? game[kGameStoreFull] : std::string{};
        default:
            return {};
    }
}

void StockNotes::add(const sim::World& world, const assets::BuildingTable& buildings, const assets::TextTable& texts,
                     std::span<const sim::ProductionEvent> events, sim::PlayerId local, double now_ms) {
    const std::vector<sim::Production>& list = world.productions();
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t previous = kNone;  // the note of the event before, if it made one
    for (std::size_t i = 0; i < events.size(); ++i) {
        const sim::ProductionEvent& e = events[i];
        const std::size_t note_of_previous = std::exchange(previous, kNone);
        if (e.production >= list.size()) {
            continue;
        }
        std::string text = stock_note_text(texts, e);
        if (text.empty()) {
            continue;
        }
        const sim::Production& p = list[e.production];
        if (!p.used || p.island >= world.islands().size()) {
            continue;
        }
        const sim::WorldIsland& island = world.islands()[p.island];
        const sim::MapCell& cell = island.map.top(p.x, p.y);
        const sim::City* city = world.city(p.island, cell.city);
        const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
        // Only the owner of the city sees them (0x0047DAF0: city + 0x1A against the local player, 0x005BACB0).
        if (city == nullptr || b == nullptr || local == sim::kNoPlayer || city->owner != local) {
            continue;
        }
        // The cycle that filled the store: its "Lager voll" overwrites the new ton.
        // finish_cycle emits the two one after the other.
        if (e.kind == sim::ProductionEvent::Kind::StoreFull && note_of_previous != kNone &&
            events[i - 1].kind == sim::ProductionEvent::Kind::Produced && events[i - 1].production == e.production) {
            m_notes[note_of_previous].text = std::move(text);
            continue;
        }
        if (m_notes.size() >= kMaxStockNotes) {
            continue;
        }
        std::uint32_t w = 1;
        std::uint32_t h = 1;
        sim::rotated_size(*b, cell.rotation, w, h);
        StockNote note;
        note.x = island.x + p.x + w * 0.5;
        note.y = island.y + p.y + h * 0.5;
        note.text = std::move(text);
        note.start_ms = now_ms;
        previous = m_notes.size();
        m_notes.push_back(std::move(note));
    }
}

void StockNotes::add_treasures(const sim::World& world, const assets::BuildingTable& buildings,
                               std::span<const sim::ConstructionEvent> events, sim::PlayerId local, double now_ms) {
    for (const sim::ConstructionEvent& e : events) {
        if (e.kind != sim::ConstructionEvent::Kind::TreasureFound || local == sim::kNoPlayer || e.player != local ||
            e.island >= world.islands().size() || m_notes.size() >= kMaxStockNotes) {
            continue;
        }
        const sim::WorldIsland& island = world.islands()[e.island];
        if (e.x < 0 || e.y < 0 || static_cast<std::uint32_t>(e.x) >= island.map.width() ||
            static_cast<std::uint32_t>(e.y) >= island.map.height()) {
            continue;
        }
        const sim::MapCell& cell = island.map.top(static_cast<std::uint32_t>(e.x), static_cast<std::uint32_t>(e.y));
        const assets::BuildingType* b = e.type != nullptr ? e.type : buildings.by_sprite(cell.sprite);
        std::uint32_t w = 1;
        std::uint32_t h = 1;
        if (b != nullptr) {
            sim::rotated_size(*b, cell.rotation, w, h);
        }
        StockNote note;
        note.x = static_cast<double>(island.x) + e.x + w * 0.5;
        note.y = static_cast<double>(island.y) + e.y + h * 0.5;
        note.text = "+" + std::to_string(e.money);
        note.start_ms = now_ms;
        m_notes.push_back(std::move(note));
    }
}

void StockNotes::expire(double now_ms) {
    std::erase_if(m_notes,
                  [&](const StockNote& n) { return now_ms >= n.start_ms + kStockNoteMs || now_ms < n.start_ms; });
}

}  // namespace o1602::app
