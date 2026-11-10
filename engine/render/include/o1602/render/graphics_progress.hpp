// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
namespace o1602::render {
enum class GraphicsPhase : unsigned { Reading, Building, Loading, Saving, Masks, Ready };
// CPU producers and one UI consumer. A single atomic snapshot prevents torn pairs;
// concurrent Building updates cannot move completed work backwards. Reading resets a new run.
// Percent is weighted work completion, never an estimate based on elapsed time.
class GraphicsProgress {
public:
    void set(GraphicsPhase phase, std::uint64_t done, std::uint64_t total) noexcept {
        unsigned start=0,weight=10;
        switch(phase){
            case GraphicsPhase::Reading: break;
            case GraphicsPhase::Building: case GraphicsPhase::Loading: start=10;weight=78;break;
            case GraphicsPhase::Saving:start=88;weight=6;break;
            case GraphicsPhase::Masks:start=94;weight=5;break;
            case GraphicsPhase::Ready:start=100;weight=0;break;
        }
        const unsigned percent=start+(total?static_cast<unsigned>(weight*std::min(done,total)/total):0);
        const unsigned next=(static_cast<unsigned>(phase)<<8)|percent;
        if(phase==GraphicsPhase::Building){
            auto previous=m_state.load(std::memory_order_relaxed);
            do {if((previous>>8)==static_cast<unsigned>(phase)&&(previous&255)>percent)return;}
            while(!m_state.compare_exchange_weak(previous,next,std::memory_order_relaxed));
        }else m_state.store(next,std::memory_order_relaxed);
    }
    [[nodiscard]] unsigned percent() const noexcept { return snapshot()&255; }
    [[nodiscard]] unsigned snapshot() const noexcept { return m_state.load(std::memory_order_relaxed); }
private:
    std::atomic<unsigned> m_state{0};
};
}
