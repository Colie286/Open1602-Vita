// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/gad.hpp"

#include <algorithm>
#include <cctype>

#include "o1602/assets/cod_parser.hpp"
#include "o1602/core/file.hpp"

namespace o1602::assets {

namespace {

constexpr std::size_t kMaxIncludeDepth = 4;
/// The text of a layout with all its includes: the original's are a few kilobytes.
constexpr std::size_t kMaxExpandedBytes = 1U << 20;

// Symbols the GAD loader defines before parsing (0x004054D6..0x00405582).
constexpr CodPredefinedSymbol kPredefined[] = {
    {"GADGET", 1},      {"MAXGADGET", 300}, {"GAD_UNUSED", 0},  {"GAD_NIX", 1},   {"GAD_GFX", 2},
    {"GAD_GFXRAST", 3}, {"GAD_GFXFILL", 4}, {"GAD_TEXTL", 5},   {"GAD_TEXTZ", 6}, {"GAD_TEXTR", 7},
    {"GAD_FILL", 8},    {"GAD_BALKEN", 9},  {"GAD_TEXTFL", 10},
};

/// "Include: "FILE"" lines replaced by the file's text, as the original parses an include into the
/// same symbol table at that point (0x004057DE).
std::string expand_includes(std::string_view text,
                            const std::function<std::optional<std::string>(std::string_view)>& include,
                            std::vector<std::string>& warnings, std::size_t depth) {
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        end = end == std::string_view::npos ? text.size() : end + 1;
        const std::string_view line = text.substr(pos, end - pos);
        pos = end;
        const std::size_t first = line.find_first_not_of(" \t");
        constexpr std::string_view kKey = "include";
        bool handled = false;
        if (first != std::string_view::npos && line.size() >= first + kKey.size()) {
            std::string head(line.substr(first, kKey.size()));
            std::transform(head.begin(), head.end(), head.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const std::size_t open = line.find('"', first);
            const std::size_t close = open == std::string_view::npos ? open : line.find('"', open + 1);
            if (head == kKey && close != std::string_view::npos) {
                handled = true;
                const std::string name(line.substr(open + 1, close - open - 1));
                const auto body = include ? include(name) : std::nullopt;
                if (!body) {
                    warnings.push_back("include not found: " + name);
                } else if (depth >= kMaxIncludeDepth) {
                    warnings.push_back("includes nested too deeply: " + name);
                } else {
                    out += expand_includes(*body, include, warnings, depth + 1);
                    out += '\n';
                    if (out.size() > kMaxExpandedBytes) {
                        // A file that includes itself again and again would grow without end.
                        warnings.push_back("includes too large, the rest is left out: " + name);
                        return out;
                    }
                }
            }
        }
        if (!handled) {
            out.append(line);
        }
    }
    return out;
}

std::int16_t i16(std::int32_t v) {
    return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
}

bool flag(const cod::Object& o, std::string_view key) {
    return (o.integer(key) & 1) != 0;
}

}  // namespace

const Gadget* GadgetLayout::find(std::int32_t id) const noexcept {
    const auto i = index_of(id);
    return i ? &gadgets[*i] : nullptr;
}

std::optional<std::size_t> GadgetLayout::index_of(std::int32_t id) const noexcept {
    for (std::size_t i = 0; i < gadgets.size(); ++i) {
        if (gadgets[i].id == id && gadgets[i].kind != GadgetKind::Unused) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::int32_t> GadgetLayout::constant(std::string_view name) const {
    const auto it = constants.find(name);
    return it != constants.end() ? std::optional<std::int32_t>(it->second) : std::nullopt;
}

GadgetLayout parse_gad(std::string_view text,
                       const std::function<std::optional<std::string>(std::string_view)>& include) {
    GadgetLayout layout;
    const std::string expanded = expand_includes(text, include, layout.warnings, 0);
    cod::ParseOptions options;
    options.min_values = 2;  // records copy whole symbol slots (pairs keep their second value)
    const cod::Document doc = cod::parse(expanded, kPredefined, options);
    for (const auto& d : doc.diagnostics) {
        layout.warnings.push_back("line " + std::to_string(d.line) + ": " + d.message);
    }
    for (const auto& [name, symbol] : doc.symbols.all()) {
        if (symbol.defined && symbol.count >= 1 && symbol.values[0].type == cod::ValueType::Number) {
            layout.constants.emplace(name, symbol.values[0].integer);
        }
    }
    const auto* records = doc.root.records("GADGET");
    const auto highest = doc.highest_nummer.find("GADGET");
    if (records == nullptr || highest == doc.highest_nummer.end() || highest->second < 0) {
        return layout;
    }
    // As many gadgets as the highest Nummer says (ObjFill's default records beyond it do not count).
    std::int32_t last = highest->second;
    if (last >= kMaxGadgets) {
        layout.warnings.push_back("gadget " + std::to_string(last) + ": more than " + std::to_string(kMaxGadgets) +
                                  " gadgets, the rest is left out");
        last = kMaxGadgets - 1;
    }
    layout.gadgets.resize(static_cast<std::size_t>(last) + 1);
    for (const auto& [nummer, r] : *records) {
        if (nummer < 0 || nummer > last) {
            continue;
        }
        Gadget& g = layout.gadgets[static_cast<std::size_t>(nummer)];
        g.id = r.integer("Id");
        const std::int32_t kind = r.integer("Kind");
        g.kind = kind >= 0 && kind <= static_cast<std::int32_t>(GadgetKind::TextFlow) ? static_cast<GadgetKind>(kind)
                                                                                      : GadgetKind::Unused;
        if (kind != static_cast<std::int32_t>(g.kind)) {
            layout.warnings.push_back("gadget " + std::to_string(nummer) + ": unknown kind " + std::to_string(kind));
        }
        g.base = r.integer("Basenr");
        g.colour = {r.integer("Color", 0), r.integer("Color", 1)};
        g.radio = flag(r, "Reiheflg");
        g.toggle = flag(r, "Flipflg");
        g.no_select = flag(r, "Noselflg");
        g.select_on_move = flag(r, "Selmoveflg");
        g.menu = flag(r, "Menuflg");
        g.draw_base = flag(r, "Baseflg");
        g.base_when_selected = flag(r, "Baseselflg");
        g.clip = flag(r, "Clipflg");
        g.hidden = flag(r, "Nodrawflg");
        g.disabled = flag(r, "Rasterflg");
        g.half = flag(r, "Alphaflg");
        g.slider_x = flag(r, "Slidhorflg");
        g.slider_y = flag(r, "Slidverflg");
        g.slider = flag(r, "Sliderflg") || g.slider_x || g.slider_y;
        g.slider_offset = {i16(r.integer("Slidoffs", 0)), i16(r.integer("Slidoffs", 1))};
        g.slider_size = {i16(r.integer("Slidsize", 0)), i16(r.integer("Slidsize", 1))};
        g.offset = {i16(r.integer("Posoffs", 0)), i16(r.integer("Posoffs", 1))};
        g.press_offset = r.integer("Pressoff");
        g.pos = {r.integer("Pos", 0), r.integer("Pos", 1)};
        g.size = {r.integer("Size", 0), r.integer("Size", 1)};
        g.block = r.integer("Blocknr");
        g.gfx = r.integer("Gfxnr");
        // Sliders: a range of 0 is the gadget's size (0x00406492).
        if (g.slider) {
            for (std::size_t a = 0; a < 2; ++a) {
                if (g.slider_size[a] == 0) {
                    g.slider_size[a] = i16(g.size[a]);
                }
            }
        }
    }
    return layout;
}

core::Result<GadgetLayout> load_gad(const GameDirectory& game, std::string_view name) {
    const std::string relative = "GADDATA/" + std::string(name);
    const auto path = game.resolve(relative);
    if (!path) {
        return core::make_error(core::ErrorCode::NotFound, relative + " not found");
    }
    auto bytes = core::read_file(*path);
    if (!bytes) {
        return bytes.error();
    }
    const auto include = [&](std::string_view file) -> std::optional<std::string> {
        const auto p = game.resolve("GADDATA/" + std::string(file));
        if (!p) {
            return std::nullopt;
        }
        auto b = core::read_file(*p);
        return b ? std::optional<std::string>(std::string(b->begin(), b->end())) : std::nullopt;
    };
    return parse_gad(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), include);
}

}  // namespace o1602::assets
