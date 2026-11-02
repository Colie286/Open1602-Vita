// SPDX-License-Identifier: GPL-3.0-or-later
#include "templates.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <system_error>

#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr std::string_view kHeader = "# Open1602 template\n";
constexpr std::int32_t kMaxSide = 1024;  // larger than any island

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

/// The numbers of a line, separated by spaces; nothing if one is not a number.
std::optional<std::vector<std::int64_t>> numbers(std::string_view s) {
    std::vector<std::int64_t> out;
    while (!(s = trim(s)).empty()) {
        std::int64_t v = 0;
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || (end != s.data() + s.size() && *end != ' ' && *end != '\t')) {
            return std::nullopt;
        }
        out.push_back(v);
        s.remove_prefix(static_cast<std::size_t>(end - s.data()));
    }
    return out;
}

/// The number of a file "<n>.txt", 0 for any other.
std::uint32_t file_number(const std::string& file) {
    std::uint32_t n = 0;
    const auto [end, ec] = std::from_chars(file.data(), file.data() + file.size(), n);
    return ec == std::errc{} && std::string_view(end) == ".txt" ? n : 0;
}

}  // namespace

std::string clean_template_name(std::string_view name) {
    std::string out;
    for (const char c : trim(name)) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && out.size() < kMaxTemplateName) {
            out += c;
        }
    }
    return std::string(trim(out));
}

std::string write_template(const BuildTemplate& t) {
    std::string out(kHeader);
    out += "name = " + core::cp1252_to_utf8(t.name) + "\n";
    out += "size = " + std::to_string(t.layout.width) + " " + std::to_string(t.layout.height) + "\n";
    for (const BuildTool::Layout::Item& item : t.layout.items) {
        out += "item = " + std::to_string(item.building) + " " + std::to_string(item.x) + " " + std::to_string(item.y) +
               " " + std::to_string(item.rotation) + " " + std::to_string(item.width) + " " +
               std::to_string(item.height) + "\n";
    }
    return out;
}

std::optional<BuildTemplate> read_template(std::string_view text) {
    BuildTemplate t;
    bool sized = false;
    text = core::skip_utf8_bom(text);
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const std::size_t eq = line.find('=');
        if (line.empty() || line.front() == '#' || eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));
        if (key == "name") {
            t.name = clean_template_name(core::is_utf8(value) ? core::utf8_to_cp1252(value) : std::string(value));
        } else if (key == "size") {
            const auto v = numbers(value);
            if (v && v->size() == 2 && (*v)[0] > 0 && (*v)[1] > 0 && (*v)[0] <= kMaxSide && (*v)[1] <= kMaxSide) {
                t.layout.width = static_cast<std::int32_t>((*v)[0]);
                t.layout.height = static_cast<std::int32_t>((*v)[1]);
                sized = true;
            }
        } else if (key == "item" && t.layout.items.size() < kMaxTemplateItems) {
            const auto v = numbers(value);
            if (!v || v->size() != 6) {
                continue;
            }
            const auto& n = *v;
            if (n[0] <= 0 || n[0] > 0xFFFFFFFF || n[1] < 0 || n[2] < 0 || n[1] >= kMaxSide || n[2] >= kMaxSide ||
                n[3] < 0 || n[3] > 3 || n[4] < 1 || n[5] < 1 || n[4] > 16 || n[5] > 16) {
                continue;
            }
            t.layout.items.push_back(BuildTool::Layout::Item{
                static_cast<std::uint32_t>(n[0]), static_cast<std::int32_t>(n[1]), static_cast<std::int32_t>(n[2]),
                static_cast<std::uint8_t>(n[3]), static_cast<std::int32_t>(n[4]), static_cast<std::int32_t>(n[5])});
        }
    }
    if (!sized || t.layout.items.empty()) {
        return std::nullopt;
    }
    // Every item inside the piece: a file edited by hand may say otherwise.
    std::erase_if(t.layout.items, [&](const BuildTool::Layout::Item& i) {
        return i.x + i.width > t.layout.width || i.y + i.height > t.layout.height;
    });
    if (t.layout.items.empty()) {
        return std::nullopt;
    }
    return t;
}

TemplateEntry template_entry(std::string name, const BuildTool::Layout& layout,
                             const assets::BuildingTable& buildings) {
    using Kind = TemplateEntry::Piece::Kind;
    TemplateEntry e;
    e.name = std::move(name);
    e.width = layout.width;
    e.height = layout.height;
    for (const BuildTool::Layout::Item& item : layout.items) {
        const assets::BuildingType* type = buildings.by_id(item.building);
        if (type == nullptr) {
            continue;
        }
        e.cost.add(type->build_cost);
        Kind kind = Kind::Building;
        if (type->kind == sim::kind::kRoad || type->kind == sim::kind::kBridge || type->kind == sim::kind::kSquare ||
            type->kind == sim::kind::kGate || type->kind == sim::kind::kWall || type->kind == sim::kind::kWallBeach) {
            kind = Kind::Road;
        } else if (type->kind == sim::kind::kForest) {
            kind = Kind::Forest;
        } else if (type->prod_kind == sim::prod::kGrowing || type->prod_kind == sim::prod::kRipe) {
            kind = Kind::Field;
        } else if (type->prod_kind == sim::prod::kHouse) {
            kind = Kind::House;
        }
        if (kind == Kind::Building || kind == Kind::House) {
            ++e.buildings;
        }
        e.view.push_back(TemplateEntry::Piece{item.x, item.y, item.width, item.height, kind});
    }
    return e;
}

void TemplateStore::load() {
    m_templates.clear();
    std::error_code ec;
    for (std::filesystem::directory_iterator it(m_folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::filesystem::path& path = it->path();
        if (path.extension() != ".txt" || !it->is_regular_file(ec)) {
            continue;
        }
        const auto bytes = core::read_file(path);
        if (!bytes) {
            continue;
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (auto t = read_template(text)) {
            t->file = core::path_to_utf8(path.filename());
            if (t->name.empty()) {
                t->name = core::path_to_utf8(path.stem());
            }
            m_templates.push_back(std::move(*t));
        }
    }
    sort();
}

void TemplateStore::sort() {
    std::stable_sort(m_templates.begin(), m_templates.end(),
                     [](const BuildTemplate& a, const BuildTemplate& b) { return a.name < b.name; });
}

bool TemplateStore::write(const BuildTemplate& t) const {
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    const std::string text = write_template(t);
    return core::write_file_atomic(m_folder / t.file,
                                   std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()))
        .ok();
}

bool TemplateStore::add(std::string_view name, BuildTool::Layout layout) {
    if (layout.items.empty()) {
        return false;
    }
    // The next free number, also past files of templates that were deleted meanwhile.
    std::uint32_t n = 1;
    for (const BuildTemplate& t : m_templates) {
        n = std::max(n, file_number(t.file) + 1);
    }
    std::error_code ec;
    while (std::filesystem::exists(m_folder / (std::to_string(n) + ".txt"), ec)) {
        ++n;
    }
    BuildTemplate t;
    t.name = clean_template_name(name);
    if (t.name.empty()) {
        t.name = next_name();
    }
    t.layout = std::move(layout);
    t.layout.from = {};
    t.file = std::to_string(n) + ".txt";
    if (!write(t)) {
        return false;
    }
    m_templates.push_back(std::move(t));
    sort();
    return true;
}

bool TemplateStore::rename(std::size_t index, std::string name) {
    name = clean_template_name(name);
    if (index >= m_templates.size() || name.empty()) {
        return false;
    }
    BuildTemplate t = m_templates[index];
    t.name = std::move(name);
    if (!write(t)) {
        return false;
    }
    m_templates[index] = std::move(t);
    sort();
    return true;
}

bool TemplateStore::remove(std::size_t index) {
    if (index >= m_templates.size()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(m_folder / m_templates[index].file, ec);
    if (ec) {
        return false;
    }
    m_templates.erase(m_templates.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::string TemplateStore::next_name() const {
    for (std::size_t n = m_templates.size() + 1;; ++n) {
        std::string name = ui::tr("template.default", {std::to_string(n)});
        if (std::none_of(m_templates.begin(), m_templates.end(),
                         [&](const BuildTemplate& t) { return t.name == name; })) {
            return name;
        }
    }
}

}  // namespace o1602::app
