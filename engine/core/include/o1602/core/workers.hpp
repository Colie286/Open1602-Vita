// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
namespace o1602::core {
using WorkerHook=void(*)(const char*,unsigned);
// Configure before creating workers; zero restores the caller's platform default.
void set_worker_policy(unsigned parallelism,WorkerHook hook=nullptr) noexcept;
unsigned worker_parallelism(unsigned fallback=1) noexcept;
void worker_enter(const char* role,unsigned index=0) noexcept;
// One owner, persistent helpers, bounded external job storage. The caller also
// works; run() publishes results only after every helper has completed the batch.
class BatchWorkers {
public:
    explicit BatchWorkers(unsigned parallelism,const char* role);
    ~BatchWorkers();
    BatchWorkers(const BatchWorkers&)=delete;
    BatchWorkers& operator=(const BatchWorkers&)=delete;
    void run(std::size_t count,const std::function<void(std::size_t,unsigned)>& job);
    unsigned size() const noexcept{return 1+static_cast<unsigned>(m_threads.size());}
private:
    void work(unsigned index);
    void consume(unsigned index) noexcept;
    std::vector<std::thread> m_threads;
    const char* m_role;
    std::mutex m_mutex;
    std::condition_variable m_wake,m_done;
    std::function<void(std::size_t,unsigned)> m_job;
    std::atomic<std::size_t> m_next{0};
    std::atomic<bool> m_failed{false};
    std::size_t m_count=0;
    unsigned m_epoch=0,m_pending=0;
    bool m_stop=false;
    std::exception_ptr m_error;
};
}
