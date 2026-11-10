// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "o1602/render/sprite_atlas.hpp"
struct SDL_Renderer;
struct SDL_Texture;
namespace o1602::render {
// A fixed number of reusable GPU pages containing only visible original sprites.
// Geometry remains in painter order; only the UV locations change.
class PagedSpriteCache {
public:
    static std::shared_ptr<PagedSpriteCache> acquire(SDL_Renderer*,unsigned page_size=2048,unsigned pages=3);
    ~PagedSpriteCache();
    bool prepare(const SpriteAtlas&,const std::vector<DrawCommand>&,double ox,double oy,double scale,int width,int height);
    const AtlasRect& rect(std::uint32_t sprite) const;
    SDL_Texture* texture(unsigned page) const;
    const std::string& error() const noexcept { return m_error; }
    std::uint64_t misses() const noexcept { return m_misses; }
    std::uint64_t uploads() const noexcept { return m_uploads; }
    std::uint64_t bytes() const noexcept;
    void invalidate();
    void enable_prefetch(bool);
    void storage_order(bool enabled) noexcept {m_storage_order=enabled;}
    void prefetch(const SpriteAtlas&,const std::vector<std::uint32_t>&);
    struct PrefetchStats {std::uint64_t bytes=0,hits=0,jobs=0,pending=0,running=0;};
    PrefetchStats prefetch_stats() const;
    /// Explicit test fault, never enabled by normal gameplay.
    void test_prefetch_delay(unsigned ms,bool hold_queue=false);
    bool prefetch_test_active() const;
    void fail_next_prepare() noexcept {m_fail_next=true;}
private:
    struct ReadQueue;
    std::unique_ptr<ReadQueue> m_reads;
    PagedSpriteCache(SDL_Renderer*,unsigned,unsigned);
    bool prepare_impl(const SpriteAtlas&,const std::vector<std::uint32_t>&,bool compact);
    struct Page { SDL_Texture* texture=nullptr;unsigned x=0,y=0,height=0;std::uint64_t used=0;bool pinned=false;std::vector<std::uint32_t> sprites; };
    SDL_Renderer* m_renderer;
    unsigned m_size;
    std::vector<Page> m_pages;
    std::vector<AtlasRect> m_entries;
    std::vector<bool> m_valid,m_seen;
    std::vector<std::uint32_t> m_needed;
    struct Missing {std::uint32_t sprite;AtlasRect dest;};
    std::vector<Missing> m_missing;
    std::uint64_t m_identity=0,m_frame=0,m_misses=0,m_uploads=0,m_evictions=0;
    bool m_fail_next=false,m_storage_order=true;
    std::string m_error;
};
}
