// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
namespace o1602::ui {
// Full samples, not remembered axis events. After an interruption/context change
// an already held direction cannot leak into the next view: release to re-arm.
class StickState {
public:
    void interrupt() noexcept { m_armed=false;m_active=false; }
    std::pair<float,float> sample(float x,float y,bool valid=true) noexcept {
        if(!valid||!std::isfinite(x)||!std::isfinite(y)){interrupt();return {};}
        x=std::clamp(x,-1.f,1.f);y=std::clamp(y,-1.f,1.f);
        const float length=std::hypot(x,y);
        if(length<.22f){m_active=false;m_armed=true;return {};}
        if(!m_armed||length<(m_active?.22f:.30f)){m_active=false;return {};}
        m_active=true;return {x,y};
    }
    bool armed() const noexcept {return m_armed;}
private:
    bool m_armed=true,m_active=false;
};
class PadFreshness {
public:
    bool sample(int result,std::uint64_t timestamp,std::uint64_t now) noexcept {
        if(result<=0)return false;
        if(!m_seen||timestamp!=m_timestamp){m_timestamp=timestamp;m_last=now;m_seen=true;}
        return now-m_last<=500;
    }
private:
    bool m_seen=false;std::uint64_t m_timestamp=0,m_last=0;
};
}
