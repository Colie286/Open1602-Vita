// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/workers.hpp"
#include <algorithm>
namespace o1602::core {
namespace {std::atomic<unsigned> lanes{0};std::atomic<WorkerHook> initialize{nullptr};}
void set_worker_policy(unsigned count,WorkerHook hook) noexcept {initialize.store(hook);lanes.store(std::min(count,8U));}
unsigned worker_parallelism(unsigned fallback) noexcept {const auto n=lanes.load();return n?n:std::clamp(fallback,1U,8U);}
void worker_enter(const char* role,unsigned index) noexcept {if(const auto hook=initialize.load())hook(role,index);}
BatchWorkers::BatchWorkers(unsigned count,const char* role):m_role(role){
    for(unsigned n=1;n<std::clamp(count,1U,8U);++n){
        try{m_threads.emplace_back([this,n]{work(n);});}catch(...){break;}
    }
}
BatchWorkers::~BatchWorkers(){
    {std::lock_guard lock(m_mutex);m_stop=true;}m_wake.notify_all();
    for(auto& t:m_threads)t.join();
}
void BatchWorkers::consume(unsigned index) noexcept {
    try{for(auto n=m_next.fetch_add(1,std::memory_order_relaxed);n<m_count&&!m_failed.load(std::memory_order_relaxed);n=m_next.fetch_add(1,std::memory_order_relaxed))m_job(n,index);}
    catch(...){std::lock_guard lock(m_mutex);if(!m_error)m_error=std::current_exception();m_failed=true;}
}
void BatchWorkers::work(unsigned index){
    worker_enter(m_role,index-1);
    unsigned epoch=0;std::unique_lock lock(m_mutex);
    for(;;){m_wake.wait(lock,[&]{return m_stop||m_epoch!=epoch;});if(m_stop)return;
        epoch=m_epoch;lock.unlock();consume(index);lock.lock();if(--m_pending==0)m_done.notify_one();}
}
void BatchWorkers::run(std::size_t count,const std::function<void(std::size_t,unsigned)>& job){
    {std::lock_guard lock(m_mutex);m_job=job;m_count=count;m_next=0;m_error=nullptr;m_failed=false;m_pending=static_cast<unsigned>(m_threads.size());++m_epoch;}
    m_wake.notify_all();consume(0);
    std::unique_lock lock(m_mutex);m_done.wait(lock,[&]{return m_pending==0;});m_job={};
    if(m_error)std::rethrow_exception(m_error);
}
}
