// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <utility>
namespace o1602::ui {
// One input step per poll at most, even after a long frame. Deadzone uses
// hysteresis; sector hysteresis keeps small diagonal jitter from retriggering.
class StickRepeat {
public:
    explicit StickRepeat(unsigned delay=200,unsigned interval=80):m_delay(delay),m_interval(interval) {}
    std::pair<int,int> poll(float x,float y,std::uint64_t ms) noexcept {
        const double magnitude=std::hypot(x,y);
        if(magnitude<(m_sector<0?.30:.22)){reset();return {};}
        double angle=std::atan2(y,x);
        constexpr double sector_angle=3.14159265358979323846/4;
        int sector=static_cast<int>(std::floor(angle/sector_angle+.5));sector=(sector+8)%8;
        if(m_sector>=0){double d=std::remainder(angle-m_sector*sector_angle,sector_angle*8);
            if(std::abs(d)<sector_angle*.65)sector=m_sector;}
        bool step=false;
        if(sector!=m_sector){m_sector=sector;m_next=ms+m_delay;step=true;}
        else if(ms>=m_next){m_next=ms-m_next>=m_interval?ms+m_interval:m_next+m_interval;step=true;}
        if(!step)return {};
        constexpr int dx[]={1,1,0,-1,-1,-1,0,1},dy[]={0,1,1,1,0,-1,-1,-1};
        return {dx[sector],dy[sector]};
    }
    void reset() noexcept {m_sector=-1;m_next=0;}
    std::pair<int,int> direction() const noexcept {
        if(m_sector<0)return {};
        constexpr int dx[]={1,1,0,-1,-1,-1,0,1},dy[]={0,1,1,1,0,-1,-1,-1};
        return {dx[m_sector],dy[m_sector]};
    }
private:
    unsigned m_delay,m_interval;
    int m_sector=-1;
    std::uint64_t m_next=0;
};
}
