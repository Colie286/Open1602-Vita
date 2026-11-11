// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai_chains.hpp"

#include <algorithm>
#include <deque>

#include "ai_tables.hpp"

namespace o1602::sim::ai {

namespace {

/// A ware a package added: after the original's fields (goods.md).
bool package_ware(std::uint8_t w) {
    return w > ware::kLastField;
}

bool crop(std::uint8_t w) {
    return w >= ware::kCornField && w <= ware::kCocoaField;
}

std::uint32_t per_minute(std::uint32_t amount, std::uint32_t interval) {
    return static_cast<std::uint32_t>(std::uint64_t{amount} * 60 / interval);
}

/// How the computers place `b`, as `need`; false if no task of theirs can.
bool placement(const assets::BuildingType& b, PackageNeed& need) {
    const bool two_by_two = b.size_x == 2 && b.size_y == 2;
    if (b.raw_ware == 0 && b.raw_amount == 0 && b.raw2_amount == 0 &&
        (b.prod_kind == prod::kFarm || b.prod_kind == prod::kPasture)) {
        // A farm of a package that makes its good from nothing (raw = ""): no fields, placed as a
        // workshop is.
        need.task = kWeaving;
        return true;
    }
    switch (b.prod_kind) {
        case prod::kWorkshop:
            if (b.raw_ware != 0 && !is_trade_ware(b.raw_ware) && !package_ware(b.raw_ware)) {
                return false;
            }
            need.task = kWeaving;  // any workshop's task: the site search reads the record
            return true;
        case prod::kFarm:
            if (!two_by_two || !crop(b.raw_ware)) {
                return false;
            }
            need.task = kTobacco;  // any plantation's task
            need.field = b.raw_ware;
            need.fertile = b.raw_ware;
            return true;
        case prod::kPasture:
            if (!two_by_two || b.raw_ware != ware::kGrass) {
                return false;
            }
            need.task = kCattle;
            need.field = ware::kGrass;
            return true;
        default:
            return false;
    }
}

}  // namespace

std::uint8_t original_need_slot(std::uint8_t w) {
    switch (w) {
        case ware::kOre:
            return kSlotIronMine;
        case ware::kGold:
            return kSlotGoldMine;
        case ware::kWool:
            return kSlotSheep;  // Engine::need takes cotton where it grows fully
        default:
            break;
    }
    for (const ComputerSlot& c : original_computer_slots()) {
        if (c.kind == ComputerSlot::Kind::Producer && c.ware == w && c.building == 0) {
            return c.number;
        }
    }
    return kNoSlot;
}

std::vector<PackageNeed> package_needs(const assets::BuildingTable& table) {
    std::vector<PackageNeed> needs;
    std::vector<std::uint8_t> slot_of(256, kNoSlot);  // by ware: the slot of its Need
    std::vector<bool> tried(256, false);
    std::deque<std::uint8_t> goods;
    const auto want = [&](std::uint8_t w) {
        if (!tried[w]) {
            tried[w] = true;
            goods.push_back(w);
        }
    };
    for (const assets::ResidentClass& c : table.resident_classes()) {
        for (const assets::ResidentClass::Need& n : c.needs) {
            if (n.amount != 0 && package_ware(n.ware)) {
                want(n.ware);
            }
        }
    }
    // Every good gets its slot before the goods it is made of are looked at, so a producer whose
    // raw material comes later learns that slot below.
    std::vector<std::pair<std::size_t, std::uint8_t>> raws;  // Need, its raw material
    while (!goods.empty() && needs.size() < kMaxPackageNeeds) {
        const std::uint8_t w = goods.front();
        goods.pop_front();
        for (const assets::BuildingType& b : table.types()) {
            if (b.id == 0 || b.ware != w || b.interval == 0 || b.produce_amount == 0) {
                continue;
            }
            PackageNeed need;
            if (!placement(b, need)) {
                continue;
            }
            need.slot = static_cast<std::uint8_t>(kSlots + needs.size());
            need.building = b.id;
            need.ware = w;
            need.output = std::max<std::uint32_t>(1, per_minute(b.produce_amount, b.interval));
            need.input = per_minute(b.raw_amount, b.interval);
            slot_of[w] = need.slot;
            if (b.prod_kind == prod::kWorkshop && b.raw_ware != 0) {
                raws.emplace_back(needs.size(), b.raw_ware);
                if (package_ware(b.raw_ware)) {
                    want(b.raw_ware);
                }
            }
            needs.push_back(need);
            break;
        }
    }
    for (const auto& [i, raw] : raws) {
        needs[i].raw_slot = package_ware(raw) ? slot_of[raw] : original_need_slot(raw);
    }
    // A chain that comes round to itself would wait for itself: its first Need waits for none.
    for (PackageNeed& need : needs) {
        std::uint8_t k = need.raw_slot;
        for (std::size_t step = 0; step < needs.size() && k >= kSlots && k != kNoSlot; ++step) {
            if (k == need.slot) {
                need.raw_slot = kNoSlot;
                break;
            }
            k = needs[k - kSlots].raw_slot;
        }
    }
    return needs;
}

std::vector<std::uint32_t> package_wanted(const std::vector<PackageNeed>& needs,
                                          const std::vector<std::uint32_t>& demand) {
    std::vector<std::uint32_t> wanted(needs.size(), 0);
    // Each pass can only raise a count, and no count passes kMaxPackageProducers, so the passes end;
    // a chain of n Needs settles within n passes.
    for (std::size_t pass = 0; pass <= needs.size(); ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < needs.size(); ++i) {
            std::uint64_t use = i < demand.size() ? demand[i] : 0;
            for (std::size_t j = 0; j < needs.size(); ++j) {
                if (needs[j].raw_slot == needs[i].slot) {
                    use += std::uint64_t{wanted[j]} * needs[j].input;
                }
            }
            const std::uint64_t n = (use + needs[i].output - 1) / needs[i].output;
            const auto w = static_cast<std::uint32_t>(std::min<std::uint64_t>(n, kMaxPackageProducers));
            if (w != wanted[i]) {
                wanted[i] = w;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    return wanted;
}

}  // namespace o1602::sim::ai
