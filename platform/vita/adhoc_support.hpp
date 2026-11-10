// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace probe {
using Ip = std::array<std::uint8_t,4>;
inline bool link_local(Ip ip) { return ip[0]==169 && ip[1]==254 && ip[2]>0 && ip[2]<255; }
inline bool remote_ip(Ip ip, Ip own) { return link_local(own) && link_local(ip) && ip!=own; }
inline std::uint32_t word(const std::byte* p) { std::uint32_t v;std::memcpy(&v,p,4);return v; }
struct PeerResult { std::array<Ip,8> ips{};unsigned count=0;const char* error=nullptr; };
// Wire view of the installed native SDK's ARM32 structure, never a host pointer cast.
// A firmware buffer that does not match that ABI stays diagnostic data, not an IP.
inline PeerResult peers(std::span<const std::byte> bytes,std::uint32_t base,unsigned count,
                        std::size_t changed_extent,Ip own) {
    PeerResult out;
    if(count>8 || bytes.size()<count*8u || changed_extent>count*8u) { out.error="SDK layout/count mismatch";return out; }
    if(!count)return out;
    std::array<unsigned,8> seen{};unsigned offset=0;
    for(unsigned i=0;i<count;++i) {
        if(offset%4 || offset>bytes.size()-8) { out.error="unaligned/outside next pointer";out.count=0;return out; }
        for(unsigned j=0;j<i;++j)if(seen[j]==offset){out.error="cyclic list";out.count=0;return out;}
        seen[i]=offset;Ip ip;std::memcpy(ip.data(),bytes.data()+offset+4,4);
        if(!remote_ip(ip,own)){out.error="invalid/self/non-link-local IPv4 field";out.count=0;return out;}
        for(unsigned j=0;j<out.count;++j)if(out.ips[j]==ip){out.error="duplicate peer";out.count=0;return out;}
        out.ips[out.count++]=ip;
        const auto next=word(bytes.data()+offset);
        if(i+1==count) { if(next){out.error="unaccounted next pointer";out.count=0;}break; }
        if(next<base || std::uint64_t(next)-base+8>count*8u){out.error="next outside owned result";out.count=0;return out;}
        offset=next-base;
    }
    return out;
}
// Observed on both physical devices in repeated 00.11 captures. This is a
// candidate ONLY: record stride/linkage are unknown. A matching nonce-bearing
// original game-discovery reply is required before presenting it as a game.
inline PeerResult single_firmware_candidate(std::span<const std::byte> bytes,unsigned count,
                                            std::size_t extent,Ip own) {
    PeerResult out;
    if(count!=1||bytes.size()<128||extent<56||extent>128){out.error="unknown physical record count/extent";return out;}
    Ip ip;std::memcpy(ip.data(),bytes.data(),4);
    if(!remote_ip(ip,own)){out.error="invalid physical IP candidate";return out;}
    out.ips[0]=ip;out.count=1;return out;
}
struct Sockets {
    std::array<int,16> ids;Sockets(){ids.fill(-1);}
    bool owns(int id)const { for(int v:ids)if(v>=0 && v==id)return true;return false; }
    bool add(int id){if(id<0 || owns(id))return false;for(int& v:ids)if(v<0){v=id;return true;}return false;}
    unsigned count()const { unsigned n=0;for(int v:ids)n+=v>=0;return n; }
    template<class Close> bool close(int id,Close fn){
        for(int& v:ids)if(v>=0 && v==id){if(fn(id)<0)return false;v=-1;return true;}
        return false;
    }
    template<class Close> bool clear(Close fn){bool ok=true;for(int v:ids)if(v>=0)ok=close(v,fn)&&ok;return ok;}
};
// Fixed 32-byte diagnostic datagram. Session nonce prevents stale echoes from
// proving a new attempt; explicit bytes avoid ABI, endian and padding guesses.
inline std::array<std::uint8_t,32> ping(std::uint32_t nonce,unsigned seq,bool echo){
    std::array<std::uint8_t,32> p{};std::memcpy(p.data(),"O16AD11",7);p[7]=echo?2:1;
    for(unsigned i=0;i<4;++i){p[8+i]=std::uint8_t(nonce>>(i*8));p[12+i]=std::uint8_t(seq>>(i*8));}
    for(unsigned i=16;i<p.size();++i)p[i]=std::uint8_t(i*37+seq);
    return p;
}
inline bool valid_ping(std::span<const std::uint8_t> p,std::uint32_t nonce,unsigned seq,bool echo){
    auto expected=ping(nonce,seq,echo);return p.size()==expected.size() && std::memcmp(p.data(),expected.data(),p.size())==0;
}
}
