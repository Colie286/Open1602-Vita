// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/block_reader.hpp"
#include "o1602/core/profile.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace o1602::core {
namespace {
constexpr std::size_t block_size=16*1024,per_reader=32,budget=2*1024*1024;
std::atomic<std::size_t> allocated{0};
struct Block {
    std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(block_size);
    std::atomic<bool> ready{false};
    bool valid=false; // published once, before ready's release store
    std::uint64_t used=0;
    ~Block(){profile::gauge(profile::SpriteBlockBytes,allocated.fetch_sub(block_size)-block_size);}
};
std::shared_ptr<Block> allocate(){
    auto used=allocated.load();
    do {if(used>budget-block_size)return {};}
    while(!allocated.compare_exchange_weak(used,used+block_size));
    try {auto b=std::make_shared<Block>();profile::gauge(profile::SpriteBlockBytes,used+block_size);return b;}
    catch(...){allocated.fetch_sub(block_size);return {};}
}
}
struct BlockReader::State {
    std::uint64_t size,clock=0;
    Read input;
    bool enabled;
    std::mutex mutex;
    std::map<std::uint64_t,std::shared_ptr<Block>> blocks;
    State(std::uint64_t n,Read read,bool on):size(n),input(std::move(read)),enabled(on){}
};
BlockReader::BlockReader(std::uint64_t size,Read read,bool enabled):m(std::make_unique<State>(size,std::move(read),enabled)){}
BlockReader::~BlockReader()=default;
std::size_t BlockReader::allocated_bytes() noexcept{return allocated.load();}
bool BlockReader::read(std::uint64_t offset,std::span<std::uint8_t> bytes){
    if(offset>m->size||bytes.size()>m->size-offset)return false;
    if(bytes.empty())return true;
    if(!m->enabled)return m->input(offset,bytes);
    while(!bytes.empty()){
        const auto index=offset/block_size,base=index*block_size;
        const auto within=static_cast<std::size_t>(offset-base);
        const auto count=std::min(bytes.size(),block_size-within);
        std::shared_ptr<Block> block;bool load=false;
        {
            std::unique_lock lock(m->mutex,std::try_to_lock);
            if(!lock.owns_lock())return m->input(offset,bytes);
            if(auto it=m->blocks.find(index);it!=m->blocks.end()){
                block=it->second;block->used=++m->clock;
                if(!block->ready.load(std::memory_order_acquire)){
                    // A speculative worker may be stalled. Required foreground
                    // reads must not inherit its completion wait. The callback
                    // supports independent offsets; neither buffer is shared.
                    lock.unlock();return m->input(offset,bytes);
                }
                if(!block->valid){m->blocks.erase(it);lock.unlock();return m->input(offset,bytes);}
                profile::increment(profile::SpriteBlockHits);
            }else{
                if(m->blocks.size()>=per_reader){
                    auto victim=m->blocks.end();
                    for(auto it=m->blocks.begin();it!=m->blocks.end();++it)
                        if(it->second.use_count()==1&&it->second->ready&&
                            (victim==m->blocks.end()||it->second->used<victim->second->used))victim=it;
                    if(victim!=m->blocks.end())m->blocks.erase(victim);
                }
                if(m->blocks.size()<per_reader)block=allocate();
                if(block){block->used=++m->clock;m->blocks.emplace(index,block);load=true;}
            }
        }
        // Pressure is a bounded direct-read fallback, never missing pixels.
        if(!block)return m->input(offset,bytes);
        if(load){
            bool valid=false;
            try{valid=m->input(base,std::span(block->bytes).first(static_cast<std::size_t>(std::min<std::uint64_t>(block_size,m->size-base))));}
            catch(...){valid=false;}
            // Publishing immutable bytes must not require reacquiring a mutex
            // which an optional worker could be holding. Readers acquire ready.
            block->valid=valid;block->ready.store(true,std::memory_order_release);
            if(!valid){
                std::unique_lock lock(m->mutex,std::try_to_lock);
                if(lock.owns_lock()){
                    auto it=m->blocks.find(index);
                    if(it!=m->blocks.end()&&it->second==block)m->blocks.erase(it);
                }
            }
        }
        if(!block->valid)return false;
        std::memcpy(bytes.data(),block->bytes.data()+within,count);
        offset+=count;bytes=bytes.subspan(count);
    }
    return true;
}
}
