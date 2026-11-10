// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/cursor_resource.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace o1602::assets {

namespace {

constexpr std::uint32_t kTypeCursor = 1;
constexpr std::uint32_t kTypeGroupCursor = 12;
constexpr std::uint32_t kSubdirectory = 0x80000000U;
constexpr std::size_t kMaxSide = 256;

core::Error damaged(const char* what) {
    return core::make_error(core::ErrorCode::InvalidFormat, std::string("cursor resource: ") + what);
}

/// Bounds-checked little-endian reads; nothing past the end.
struct Bytes {
    std::span<const std::uint8_t> d;

    [[nodiscard]] std::optional<std::uint16_t> u16(std::size_t at) const {
        if (at > d.size() || d.size() - at < 2) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(d[at] | (d[at + 1] << 8));
    }
    [[nodiscard]] std::optional<std::uint32_t> u32(std::size_t at) const {
        const auto lo = u16(at);
        const auto hi = u16(at + 2);
        if (!lo || !hi) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*lo) | (static_cast<std::uint32_t>(*hi) << 16);
    }
};

struct Section {
    std::uint32_t address = 0;  ///< virtual address
    std::uint32_t size = 0;
    std::uint32_t offset = 0;  ///< in the file
};

/// A resource tree of a PE file.
class Resources {
public:
    explicit Resources(std::span<const std::uint8_t> pe) : m_bytes{pe} {}

    core::Status open() {
        const auto lfanew = m_bytes.u32(0x3C);
        if (m_bytes.u16(0) != std::uint16_t{0x5A4D} || !lfanew || m_bytes.u32(*lfanew) != std::uint32_t{0x4550}) {
            return damaged("not a PE file");
        }
        const std::size_t coff = *lfanew + 4;
        const auto sections = m_bytes.u16(coff + 2);
        const auto optional_size = m_bytes.u16(coff + 16);
        const std::size_t optional = coff + 20;
        const auto magic = m_bytes.u16(optional);
        if (!sections || !optional_size || !magic || (*magic != 0x10B && *magic != 0x20B)) {
            return damaged("no optional header");
        }
        // The data directories: 96 bytes into PE32's optional header, 112 into PE32+'s; the third
        // one is the resources'.
        const std::size_t directories = optional + (*magic == 0x10B ? 96U : 112U);
        const auto resource_rva = m_bytes.u32(directories + 2 * 8);
        if (!resource_rva || *resource_rva == 0) {
            return damaged("no resources");
        }
        const std::size_t table = optional + *optional_size;
        for (std::size_t i = 0; i < *sections; ++i) {
            const std::size_t s = table + i * 40;
            const auto vsize = m_bytes.u32(s + 8);
            const auto address = m_bytes.u32(s + 12);
            const auto raw_size = m_bytes.u32(s + 16);
            const auto raw = m_bytes.u32(s + 20);
            if (!vsize || !address || !raw_size || !raw) {
                return damaged("section table cut off");
            }
            m_sections.push_back(Section{*address, std::max(*vsize, *raw_size), *raw});
        }
        const auto root = offset_of(*resource_rva);
        if (!root) {
            return damaged("resources outside the sections");
        }
        m_root = *root;
        return core::Ok{};
    }

    /// The data of resource type/id (the first language), as a span of the file.
    [[nodiscard]] std::optional<std::span<const std::uint8_t>> find(std::uint32_t type, std::uint32_t id) const {
        const auto types = entry(0, type);
        if (!types || (*types & kSubdirectory) == 0) {
            return std::nullopt;
        }
        const auto ids = entry(*types & ~kSubdirectory, id);
        if (!ids || (*ids & kSubdirectory) == 0) {
            return std::nullopt;
        }
        const auto languages = first_entry(*ids & ~kSubdirectory);
        if (!languages || (*languages & kSubdirectory) != 0) {
            return std::nullopt;
        }
        const auto rva = m_bytes.u32(m_root + *languages);
        const auto size = m_bytes.u32(m_root + *languages + 4);
        if (!rva || !size) {
            return std::nullopt;
        }
        const auto at = offset_of(*rva);
        if (!at || *at > m_bytes.d.size() || m_bytes.d.size() - *at < *size) {
            return std::nullopt;
        }
        return m_bytes.d.subspan(*at, *size);
    }

private:
    [[nodiscard]] std::optional<std::size_t> offset_of(std::uint32_t rva) const {
        for (const Section& s : m_sections) {
            if (rva >= s.address && rva - s.address < s.size) {
                return std::size_t{s.offset} + (rva - s.address);
            }
        }
        return std::nullopt;
    }

    /// The entry of a numeric id in the directory at `dir` (offset from the root).
    [[nodiscard]] std::optional<std::uint32_t> entry(std::uint32_t dir, std::uint32_t id) const {
        const auto named = m_bytes.u16(m_root + dir + 12);
        const auto numbered = m_bytes.u16(m_root + dir + 14);
        if (!named || !numbered) {
            return std::nullopt;
        }
        for (std::size_t k = *named; k < std::size_t{*named} + *numbered; ++k) {
            const std::size_t e = m_root + dir + 16 + k * 8;
            if (m_bytes.u32(e) == id) {
                return m_bytes.u32(e + 4);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> first_entry(std::uint32_t dir) const {
        const auto named = m_bytes.u16(m_root + dir + 12);
        const auto numbered = m_bytes.u16(m_root + dir + 14);
        if (!named || !numbered || *named + *numbered == 0) {
            return std::nullopt;
        }
        return m_bytes.u32(m_root + dir + 16 + 4);
    }

    Bytes m_bytes;
    std::vector<Section> m_sections;
    std::size_t m_root = 0;
};

}  // namespace

core::Result<CursorImage> read_cursor(std::span<const std::uint8_t> pe, std::uint16_t group) {
    Resources res(pe);
    if (auto opened = res.open(); !opened) {
        return opened.error();
    }
    // The group: reserved, type 2, count, then 14 bytes per image (w, h, planes, bits, size, id).
    const auto dir = res.find(kTypeGroupCursor, group);
    if (!dir) {
        return core::make_error(core::ErrorCode::NotFound, "cursor resource " + std::to_string(group) + " missing");
    }
    const Bytes g{*dir};
    const auto count = g.u16(4);
    const auto image_id = g.u16(6 + 12);
    if (g.u16(2) != std::uint16_t{2} || !count || *count == 0 || !image_id) {
        return damaged("not a cursor group");
    }
    const auto data = res.find(kTypeCursor, *image_id);
    if (!data) {
        return damaged("cursor image missing");
    }
    // The image: hot spot, then a BITMAPINFOHEADER with twice the height (picture and mask), the
    // two colours, the picture's rows and the mask's, from the bottom, each padded to 4 bytes.
    const Bytes c{*data};
    const auto hot_x = c.u16(0);
    const auto hot_y = c.u16(2);
    const auto header = c.u32(4);
    const auto width = c.u32(8);
    const auto double_height = c.u32(12);
    const auto bits = c.u16(4 + 14);
    if (!hot_x || !hot_y || !header || !width || !double_height || !bits) {
        return damaged("cursor header cut off");
    }
    if (*bits != 1) {
        return core::make_error(core::ErrorCode::Unsupported, "cursor resource: only black-and-white cursors");
    }
    const std::uint32_t height = *double_height / 2;
    if (*width == 0 || height == 0 || *width > kMaxSide || height > kMaxSide || *hot_x >= *width || *hot_y >= height) {
        return damaged("cursor size");
    }
    const std::size_t colours = 4 + std::size_t{*header};
    const std::size_t stride = ((*width + 31) / 32) * 4;
    const std::size_t picture = colours + 2 * 4;
    const std::size_t mask = picture + stride * height;
    if (data->size() < mask + stride * height) {
        return damaged("cursor bits cut off");
    }
    CursorImage out;
    out.width = *width;
    out.height = height;
    out.hot_x = *hot_x;
    out.hot_y = *hot_y;
    out.rgba.resize(std::size_t{*width} * height * 4);
    const auto bit = [&](std::size_t base, std::uint32_t x, std::uint32_t y) {
        const std::size_t row = base + (height - 1 - y) * stride;  // rows from the bottom
        return ((*data)[row + x / 8] >> (7 - x % 8)) & 1;
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < *width; ++x) {
            const int xor_bit = bit(picture, x, y);
            const bool transparent = bit(mask, x, y) == 1 && xor_bit == 0;
            // An inverted pixel (mask and picture set) cannot be inverted here: black.
            const std::size_t colour = colours + (bit(mask, x, y) == 1 ? 0U : static_cast<std::size_t>(xor_bit) * 4);
            std::uint8_t* p = &out.rgba[(std::size_t{y} * *width + x) * 4];
            p[0] = (*data)[colour + 2];  // the colour table is blue, green, red
            p[1] = (*data)[colour + 1];
            p[2] = (*data)[colour];
            p[3] = transparent ? 0 : 255;
        }
    }
    return out;
}

}  // namespace o1602::assets
