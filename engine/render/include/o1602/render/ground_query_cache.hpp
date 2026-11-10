// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <optional>
#include <tuple>
#include "o1602/render/camera.hpp"
namespace o1602::render {
// Exact cursor coordinates and camera transform, not a rounded tile or stale time window.
// Every elevation candidate is validated by its map block revision and discovery state.
class GroundQueryCache {
public:
    using Key=std::tuple<double,double,double,double,double,unsigned,unsigned,unsigned,unsigned,const void*,unsigned>;
    struct Revision {
        std::uint64_t block=0;std::uint16_t island=0;bool discovered=false;
        bool operator==(const Revision&) const = default;
    };
    template<class ReadRevision,class Compute>
    std::optional<TilePos> get(const Key& key,ReadRevision revision,Compute compute){
        if(m_valid&&key==m_key){
            bool same=true;for(unsigned i=0;i<m_count&&same;++i)same=revision(m_points[i].tile)==m_points[i].revision;
            if(same)return m_result;
        }
        m_valid=true;m_count=0;m_key=key;
        m_result=compute([&](TilePos tile){
            if(m_count<m_points.size())m_points[m_count++]={tile,revision(tile)};
            else m_valid=false; // bounded storage: a large candidate set simply uses the uncached path
        });
        return m_result;
    }
private:
    struct Point { TilePos tile{};Revision revision{}; };
    Key m_key{};bool m_valid=false;unsigned m_count=0;
    std::array<Point,64> m_points{};
    std::optional<TilePos> m_result;
};
}
