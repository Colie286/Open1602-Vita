// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/build_menu.hpp"

#include <algorithm>
#include <limits>
#include <system_error>
#include <utility>

#include "o1602/assets/game_content.hpp"
#include "o1602/assets/original_names.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/ui/package_icons.hpp"
#include "o1602/ui/panel.hpp"

namespace o1602::ui {

std::size_t BuildCategory::size() const noexcept {
    std::size_t n = 0;
    for (const auto& row : rows) {
        n += row.size();
    }
    return n;
}

std::optional<std::uint32_t> BuildCategory::at(std::size_t index) const noexcept {
    const BuildButton* b = button_at(index);
    return b != nullptr ? std::optional<std::uint32_t>(b->id) : std::nullopt;
}

std::int32_t BuildButton::sprite(std::int32_t gfx_bauhaus) const {
    if (!picture.empty()) {
        if (const auto own = package_icon_sprite(picture)) {
            return *own;
        }
    }
    return gfx_bauhaus + icon;
}

const BuildButton* BuildCategory::button_at(std::size_t index) const noexcept {
    for (const auto& row : rows) {
        if (index < row.size()) {
            return &row[index];
        }
        index -= row.size();
    }
    return nullptr;
}

std::optional<std::size_t> BuildCategory::index_of(std::uint32_t id) const noexcept {
    std::size_t i = 0;
    for (const auto& row : rows) {
        for (const BuildButton& b : row) {
            if (b.id == id) {
                return i;
            }
            ++i;
        }
    }
    return std::nullopt;
}

std::string BuildCategory::name(std::size_t index, const std::vector<std::string>& section) const {
    const BuildButton* b = button_at(index);
    if (b == nullptr) {
        return {};
    }
    if (b->text < 0) {
        return b->name;
    }
    const auto line = static_cast<std::size_t>(b->text);
    return line < section.size() ? section[line] : std::string();
}

const BuildCategory* BuildMenuDefinition::category(std::int32_t button) const noexcept {
    for (const BuildCategory& c : categories) {
        if (c.button == button) {
            return &c;
        }
    }
    return nullptr;
}

const BuildCategory* BuildMenuDefinition::category_of(std::uint32_t id) const noexcept {
    for (const BuildCategory& c : categories) {
        if (c.index_of(id)) {
            return &c;
        }
    }
    return nullptr;
}

namespace {

/// A category of the original: its buildings' icons follow one after the other from
/// `icon_offset`, and its Text.cod section names them in the order of the list.
struct OriginalCategory {
    std::int32_t button = 0;
    bool demolish = false;
    const char* names = "";
    std::int32_t icon_offset = 0;
    std::vector<std::vector<std::uint32_t>> rows;
};

BuildCategory from_original(const OriginalCategory& o) {
    BuildCategory c;
    c.button = o.button;
    c.demolish = o.demolish;
    c.names = o.names;
    std::int32_t position = 0;
    for (const auto& ids : o.rows) {
        std::vector<BuildButton>& row = c.rows.emplace_back();
        for (const std::uint32_t id : ids) {
            row.push_back(BuildButton{id, o.icon_offset + position, position, {}, {}});
            ++position;
        }
    }
    return c;
}

}  // namespace

const BuildMenuDefinition& original_build_menu() {
    static const BuildMenuDefinition menu = [] {
        const std::vector<OriginalCategory> original = {
            {31001, false, "MILITAR", 5, {{22521, 21404, 21405}, {22501, 20304, 20305, 20303}, {20311, 20313, 20315}}},
            {31002, false, "STRASSE", 15, {{20411, 20401, 21601, 21603}, {20421, 20422, 20423, 20424}}},
            {31003, false, "BERGWERK", 23, {{22407, 22401, 22403, 22405}}},
            {31004, true, "ABRISS", 0, {}},
            {31005,
             false,
             "HANDWERK",
             27,
             {{20507, 20511, 20501, 20503},
              {20525, 20519, 20505, 20531},
              {20513, 20509, 20521, 20515},
              {20533, 20517, 20529, 20527}}},
            {31006,
             false,
             "FARM",
             44,
             {{22003, 22001, 21303},
              {21503, 21301, 21515, 21513},
              {21507, 21335, 21501, 21341},
              {21517, 21343, 21511, 21339},
              {21505, 21331, 21509, 21337}}},
            {31007, false, "HAFEN", 63, {{22101, 22103, 22105, 22107}, {21075, 21073, 22115, 22111}}},
            {31008,
             false,
             "DIVERS",
             71,
             {{20605, 20809, 20803, 20805},
              {20801, 20807, 20817, 20821},
              {20819, 20813, 20815, 20811},
              {20831, 20841, 20851, 20853}}},
        };
        BuildMenuDefinition m;
        for (const OriginalCategory& o : original) {
            m.categories.push_back(from_original(o));
        }
        return m;
    }();
    return menu;
}

namespace {

BuildMenuDefinition& current_build_menu() {
    static BuildMenuDefinition menu = original_build_menu();
    return menu;
}

constexpr std::string_view kBuildMenuFile = "build_menu.toml";

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// Takes `id` out of every category; rows left empty go.
void remove_building(BuildMenuDefinition& menu, std::uint32_t id) {
    for (BuildCategory& c : menu.categories) {
        for (auto& row : c.rows) {
            std::erase_if(row, [id](const BuildButton& b) { return b.id == id; });
        }
        std::erase_if(c.rows, [](const std::vector<BuildButton>& row) { return row.empty(); });
    }
}

/// One table of build_menu.toml applied to `menu`; a message why not, else empty.
std::string apply_table(BuildMenuDefinition& menu, const core::DataTable& table, const assets::BuildingTable& buildings,
                        std::span<const assets::OwnBuilding> own, const std::string& at,
                        std::vector<std::string>& warnings) {
    std::optional<std::int64_t> id;
    std::optional<std::int64_t> category;
    std::optional<std::int64_t> row;
    std::optional<std::int64_t> position;
    std::optional<std::int64_t> icon;
    std::optional<std::string> picture;
    std::optional<std::string> name;
    bool remove = false;
    for (const core::DataEntry& e : table.entries) {
        const std::string field = line_of(at, e.value.line) + "'" + e.key + "' ";
        const std::int64_t* n = e.value.integer();
        if (const std::string* icon_id = e.value.string(); e.key == "icon" && icon_id != nullptr) {
            // A package's icon (icons.toml).
            if (!package_icon_sprite(*icon_id)) {
                return field + "names no icon of a package (icons.toml, <package>:<name>)";
            }
            picture = *icon_id;
        } else if (e.key == "id" || e.key == "category" || e.key == "row" || e.key == "position" || e.key == "icon") {
            if (n == nullptr || *n < 0 || *n > std::numeric_limits<std::int32_t>::max()) {
                return field + "is a number from 0";
            }
            (e.key == "id"         ? id
             : e.key == "category" ? category
             : e.key == "row"      ? row
             : e.key == "position" ? position
                                   : icon) = *n;
        } else if (e.key == "building") {
            // A building by its string id: one a package added, or one of the original's by name.
            if (e.value.string() == nullptr) {
                return field + "is a string, the id of a building";
            }
            const std::string& named = *e.value.string();
            if (const assets::OwnBuilding* added = assets::find_own_building(own, named); added != nullptr) {
                id = added->number;
            } else if (const auto number = assets::original_building_id(named)) {
                id = *number;
            } else {
                return field + "names no building (anno1602:<number or name> or <package>:<name>)";
            }
        } else if (e.key == "name") {
            if (e.value.string() == nullptr) {
                return field + "is a string";
            }
            name = core::utf8_to_cp1252(*e.value.string());
        } else if (e.key == "remove") {
            if (e.value.boolean() == nullptr) {
                return field + "is true or false";
            }
            remove = *e.value.boolean();
        } else {
            warnings.push_back(field + "is no field of the build menu and is passed over");
        }
    }
    const std::string here = line_of(at, table.line);
    if (!id) {
        return here + "names no building ('id')";
    }
    const auto building = static_cast<std::uint32_t>(*id);
    if (remove) {
        remove_building(menu, building);
        return {};
    }
    if (buildings.by_id(building) == nullptr) {
        return here + "haeuser.cod has no HAUS with Id " + std::to_string(building);
    }
    if (!category) {
        return here + "names no category ('category', the button of BAU.GAD)";
    }
    BuildMenuDefinition changed = menu;
    // A building listed already keeps its icon and name unless the table gives others.
    BuildButton button{building, 0, -1, {}, {}};
    bool listed = false;
    if (const BuildCategory* from = menu.category_of(building)) {
        if (const BuildButton* b = from->button_at(from->index_of(building).value_or(0)); b != nullptr) {
            button = *b;
            listed = true;
        }
    }
    remove_building(changed, building);
    BuildCategory* to = nullptr;
    for (BuildCategory& c : changed.categories) {
        if (c.button == *category && !c.demolish) {
            to = &c;
        }
    }
    if (to == nullptr) {
        return here + "there is no category " + std::to_string(*category) + " with buildings";
    }
    if (icon) {
        button.icon = static_cast<std::int32_t>(*icon);
        button.picture.clear();
    } else if (picture) {
        button.picture = *picture;
    } else if (!listed) {
        return here + "a building new to the menu needs an 'icon'";
    }
    if (name) {
        button.name = *name;
        button.text = -1;
    } else if (!listed) {
        return here + "a building new to the menu needs a 'name'";
    }
    // Without a row: the last one while it has room, else a new one.
    std::size_t r = to->rows.size() + 1;
    if (row) {
        r = static_cast<std::size_t>(*row);
    } else if (!to->rows.empty() && to->rows.back().size() < kMaxRowIcons) {
        r = to->rows.size();
    }
    if (r < 1 || r > to->rows.size() + 1 || r > kMaxPopupRows) {
        return here + "row " + std::to_string(r) + ": the category has " + std::to_string(to->rows.size()) +
               " rows, one past the last starts a new one, at most " + std::to_string(kMaxPopupRows);
    }
    if (r > to->rows.size()) {
        to->rows.emplace_back();
    }
    std::vector<BuildButton>& into = to->rows[r - 1];
    if (into.size() >= kMaxRowIcons) {
        return here + "row " + std::to_string(r) + " is full (" + std::to_string(kMaxRowIcons) + " buildings)";
    }
    const std::size_t p = position ? static_cast<std::size_t>(*position) : into.size() + 1;
    if (p < 1 || p > into.size() + 1) {
        return here + "position " + std::to_string(p) + ": the row has " + std::to_string(into.size()) + " buildings";
    }
    into.insert(into.begin() + static_cast<std::ptrdiff_t>(p - 1), std::move(button));
    menu = std::move(changed);
    return {};
}

}  // namespace

const BuildMenuDefinition& build_menu() {
    return current_build_menu();
}

void set_build_menu(BuildMenuDefinition menu) {
    current_build_menu() = std::move(menu);
}

core::Status apply_build_menu_package(BuildMenuDefinition& menu, std::string_view package, std::string_view utf8,
                                      const assets::BuildingTable& buildings, std::vector<std::string>& warnings,
                                      std::span<const assets::OwnBuilding> own) {
    const std::string where = std::string(package) + "/" + std::string(kBuildMenuFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        return core::make_error(tables.error().code, where + " " + tables.error().message);
    }
    for (const core::DataTable& table : *tables) {
        if (table.name.empty()) {
            warnings.push_back(line_of(where, table.entries.front().value.line) +
                               "fields before the first [building] belong to none and are passed over");
            continue;
        }
        if (std::string problem = apply_table(menu, table, buildings, own, where, warnings); !problem.empty()) {
            warnings.push_back(std::move(problem) + "; [" + table.name + "] is passed over");
        }
    }
    return core::Ok{};
}

BuildMenuDefinition load_build_menu_mods(const BuildMenuDefinition& menu, std::span<const assets::ModPackage> packages,
                                         const assets::BuildingTable& buildings, std::vector<std::string>& warnings,
                                         std::span<const assets::OwnBuilding> own) {
    BuildMenuDefinition out = menu;
    for (const assets::ModPackage& package : packages) {
        const auto bytes = core::read_file(package.folder / kBuildMenuFile);
        if (!bytes) {
            continue;  // a package that leaves the menu as it is
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (const auto status = apply_build_menu_package(out, package.id, text, buildings, warnings, own); !status) {
            warnings.push_back(status.error().message);
        }
    }
    return out;
}

namespace {

assets::Gadget piece(std::int32_t id, assets::GadgetKind kind, std::int32_t gfx, std::int32_t x, std::int32_t y,
                     std::int32_t w, std::int32_t h) {
    assets::Gadget g;
    g.id = id;
    g.kind = kind;
    g.gfx = gfx;
    g.pos = {x, y};
    g.size = {w, h};
    g.no_select = true;
    return g;
}

}  // namespace

assets::GadgetLayout build_popup(const BuildCategory& category, const std::vector<bool>& available,
                                 const assets::Gadget& button, std::int32_t gfx_bauhaus,
                                 std::int32_t locked_icon_offset) {
    // Rows with something to pick; none at all: the last row, all "?" (0x00432D82).
    struct Row {
        std::size_t first = 0;  ///< position of its first building in the list
        std::size_t count = 0;
    };
    std::vector<Row> shown;
    std::size_t first = 0;
    for (const auto& row : category.rows) {
        bool any = false;
        for (std::size_t k = 0; k < row.size(); ++k) {
            any = any || (first + k < available.size() && available[first + k]);
        }
        if (any) {
            shown.push_back(Row{first, row.size()});
        }
        first += row.size();
    }
    if (shown.empty() && !category.rows.empty()) {
        shown.push_back(Row{first - category.rows.back().size(), category.rows.back().size()});
    }
    if (shown.size() > kMaxPopupRows) {
        shown.resize(kMaxPopupRows);  // a menu not checked by apply_build_menu_package()
    }

    using assets::GadgetKind;
    assets::GadgetLayout layout;
    auto& g = layout.gadgets;
    // Behind everything, the whole panel: a press there closes the pop-up.
    assets::Gadget backdrop = piece(kPopupBackdrop, GadgetKind::Area, 0, 0, 0, kDesignWidth, kDesignHeight);
    backdrop.no_select = false;
    backdrop.menu = true;
    g.push_back(backdrop);

    constexpr std::int32_t kX = 8;
    constexpr std::int32_t kWidth = 241;
    constexpr std::int32_t kShadow = 4;
    const std::int32_t bottom = button.pos[1];
    const auto rows = static_cast<std::int32_t>(shown.size());
    const std::int32_t top = bottom - kPopupRowStep * rows;
    const auto row_y = [&](std::size_t k) { return bottom - kPopupRowStep * (static_cast<std::int32_t>(k) + 1); };
    // The frame: a strip above, the rows, a strip below; first their shadows.
    for (const std::int32_t d : {kShadow, 0}) {
        const GadgetKind kind = d != 0 ? GadgetKind::GraphicRaster : GadgetKind::Graphic;
        g.push_back(piece(d != 0 ? 32952 : 32953, kind, gfx_bauhaus + 0, kX + d, top - 5 + d, kWidth, 5));
        for (std::size_t k = 0; k < shown.size(); ++k) {
            const auto r = static_cast<std::int32_t>(k);
            g.push_back(piece((d != 0 ? 32921 : 32901) + r, kind, gfx_bauhaus + 1, kX + d, row_y(k) + d, kWidth, 60));
        }
        g.push_back(piece(d != 0 ? 32954 : 32955, kind, gfx_bauhaus + 2, kX + d, bottom + 1 + d, kWidth, 4));
    }
    // Icons, then the frames to click over them.
    std::vector<assets::Gadget> slots;
    for (std::size_t k = 0; k < shown.size(); ++k) {
        for (std::size_t j = 0; j < std::min(shown[k].count, kMaxRowIcons); ++j) {
            const std::size_t i = shown[k].first + j;
            const BuildButton* b = category.button_at(i);
            const bool ok = b != nullptr && i < available.size() && available[i];
            const auto ii = static_cast<std::int32_t>(i);
            const std::int32_t x = kX + 8 + kPopupIconStep * static_cast<std::int32_t>(j);
            const std::int32_t y = row_y(k) + 5;
            const std::int32_t icon = ok ? b->sprite(gfx_bauhaus) : gfx_bauhaus + locked_icon_offset;
            g.push_back(piece(kPopupIcon + ii, GadgetKind::Graphic, icon, x, y, 53, 50));
            assets::Gadget slot = piece(kPopupSlot + ii, GadgetKind::Graphic, gfx_bauhaus + 3, x, y, 53, 50);
            slot.press_offset = 1;
            slot.select_on_move = true;
            slot.no_select = !ok;
            slots.push_back(slot);
        }
    }
    const auto base = static_cast<std::int32_t>(g.size());
    for (auto& slot : slots) {
        slot.radio = true;
        slot.base = base;
        g.push_back(slot);
    }
    return layout;
}

}  // namespace o1602::ui
