// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/sprite_hit_mask.hpp"

#include <algorithm>
#include <stdexcept>
#include "o1602/core/profile.hpp"

namespace o1602::render {

SpriteHitMask SpriteHitMask::build(const SpriteAtlas& atlas, std::uint32_t first, GraphicsProgress* progress) {
    core::profile::Scope timer(core::profile::Masks);
    SpriteHitMask mask;
    const auto count = static_cast<std::uint32_t>(atlas.sprite_count());
    mask.m_first = std::min(first, count);
    mask.m_entries.resize(count - mask.m_first);
    std::uint64_t bits = 0;
    for (std::uint32_t s = mask.m_first; s < count; ++s) {
        const AtlasRect& r = atlas.rect(s);
        Entry& e = mask.m_entries[s - mask.m_first];
        e.bit = bits;
        e.width = r.width;
        e.height = r.height;
        bits += std::uint64_t{r.width} * r.height;
    }
    mask.m_bits.assign(static_cast<std::size_t>((bits + 63) / 64), 0);
    if(atlas.paged()){
        for(std::uint32_t s=mask.m_first;s<count;++s){
            if(progress && s%64==0)progress->set(GraphicsPhase::Masks,s-mask.m_first,count-mask.m_first);
            const auto alpha=atlas.alpha(s);const auto& e=mask.m_entries[s-mask.m_first];
            const std::uint64_t pixels=std::uint64_t{e.width}*e.height;
            for(std::uint64_t k=0;k<pixels;k+=64){
                const auto n=std::min<std::uint64_t>(64,pixels-k);std::uint64_t word=0;
                for(unsigned j=0;j<(n+7)/8;++j)word|=std::uint64_t{alpha[static_cast<std::size_t>(k/8)+j]}<<(j*8);
                if(n<64)word&=(std::uint64_t{1}<<n)-1;
                const auto bit=e.bit+k;const auto index=static_cast<std::size_t>(bit/64);const unsigned shift=bit%64;
                mask.m_bits[index]|=word<<shift;
                if(shift && n>64-shift)mask.m_bits[index+1]|=word>>(64-shift);
            }}
        return mask;
    }
    const std::uint32_t ps = atlas.page_size();
    for(std::size_t page_index=0;page_index<atlas.pages().size();++page_index) {
      RgbaImage staging;
      if(atlas.paged()){
          auto loaded=atlas.read_page(page_index);
          if(!loaded)throw std::runtime_error(loaded.error().message);
          staging=std::move(*loaded);
      }
      const auto& page=atlas.paged()?staging.pixels:atlas.pages()[page_index].pixels;
      for (std::uint32_t s = mask.m_first; s < count; ++s) {
        const AtlasRect& r = atlas.rect(s);
        if (r.empty() || r.page != page_index) {
            continue;
        }
        if (page.empty()) {
            continue;
        }
        std::uint64_t bit = mask.m_entries[s - mask.m_first].bit;
        for (std::uint32_t y = 0; y < r.height; ++y) {
            const std::uint8_t* src = &page[((std::size_t{r.y} + y) * ps + r.x) * 4];
            for (std::uint32_t x = 0; x < r.width; ++x, ++bit) {
                if (src[std::size_t{x} * 4 + 3] != 0) {
                    mask.m_bits[static_cast<std::size_t>(bit / 64)] |= std::uint64_t{1} << (bit % 64);
                }
            }
        }
    }
    }
    return mask;
}

bool SpriteHitMask::visible(std::uint32_t sprite, std::int32_t x, std::int32_t y) const noexcept {
    if (x < 0 || y < 0) {
        return false;
    }
    if (sprite < m_first || sprite - m_first >= m_entries.size()) {
        return true;
    }
    const Entry& e = m_entries[sprite - m_first];
    if (x >= e.width || y >= e.height) {
        return false;
    }
    const std::uint64_t bit = e.bit + static_cast<std::uint64_t>(y) * e.width + static_cast<std::uint64_t>(x);
    return ((m_bits[static_cast<std::size_t>(bit / 64)] >> (bit % 64)) & 1U) != 0;
}

std::optional<std::array<std::int32_t, 4>> command_bounds(const DrawCommand& command, const SpriteAtlas& atlas) {
    if (command.overlay()) {
        return std::nullopt;
    }
    const AtlasRect& r = atlas.rect(command.sprite);
    const std::int32_t h = r.height - std::min<std::int32_t>(command.cut_bottom, r.height);
    if (r.width == 0 || h <= 0) {
        return std::nullopt;
    }
    return std::array<std::int32_t, 4>{command.x, command.y - r.height, r.width, h};
}

bool command_hit(const DrawCommand& command, const SpriteAtlas& atlas, const SpriteHitMask& mask, std::int32_t x,
                 std::int32_t y) {
    const auto b = command_bounds(command, atlas);
    if (!b || x < (*b)[0] || y < (*b)[1] || x >= (*b)[0] + (*b)[2] || y >= (*b)[1] + (*b)[3]) {
        return false;
    }
    return mask.visible(command.sprite, x - (*b)[0], y - (*b)[1]);
}

}  // namespace o1602::render
