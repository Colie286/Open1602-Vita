// SPDX-License-Identifier: GPL-3.0-or-later
// Independent, opt-in observer. No SDL calls, game locks, heap/profile snapshots
// or main-thread recovery; report the stalled operation and raw SDK wait state.
#include "o1602/platform/vita.hpp"
#include "startup.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <atomic>
#include <cstdio>
#include <thread>
namespace o1602::platform::vita {
namespace {
std::thread observer;
std::atomic<bool> stopped{false};
std::atomic<unsigned> current_frame{0},current_tick{0},current_zoom{0},heartbeat_ms{0};
std::atomic<const char*> phase_name{"not_started"};
SceUID main_id=-1;
bool started=false,attempted=false,test_stall=false;
unsigned now_ms(){return unsigned(sceKernelGetProcessTimeWide()/1000);}
void observe(){
    sceKernelChangeThreadPriority(0,0x10000120);
    unsigned records=0,last_report=0;bool reported=false,opened=false;
    while(!stopped.load(std::memory_order_acquire)){
        sceKernelDelayThread(500000);
        if(stopped.load(std::memory_order_acquire))break;
        // Read the heartbeat first so a concurrent frame cannot cause unsigned age underflow.
        const unsigned beat=heartbeat_ms.load(std::memory_order_acquire);
        const unsigned now=now_ms(),age=now-beat;
        if(age<5000&&!reported)continue;
        if(age>=5000&&reported&&now-last_report<10000)continue;
        if(records>=64)continue; // bounded even if the game cannot make progress
        SceKernelThreadInfo info{};info.size=sizeof(info);
        const int result=sceKernelGetThreadInfo(main_id,&info);
        char text[640];const int n=std::snprintf(text,sizeof(text),
            "%s session=%s version=1.50 commit=%s frame=%u tick_low32=%u zoom=%u no_frame_ms=%u phase=%s "
            "thread=%d info_rc=%d status=%u wait_type=%u wait_id=%d cpu=%d priority=%d affinity=%d "
            "run_clocks=%llu stack_bytes=%d stack_free=%d observer_ms=%u\n",
            age>=5000?"FREEZE_SUSPECT":"FRAME_PROGRESS_RESUMED",vita_boot_session(),O1602_VITA_COMMIT,
            current_frame.load(),current_tick.load(),current_zoom.load(),age,phase_name.load(),
            main_id,result,unsigned(info.status),unsigned(info.waitType),info.waitId,info.currentCpuId,
            info.currentPriority,info.currentCpuAffinityMask,(unsigned long long)info.runClocks,
            info.stackSize,sceKernelGetThreadStackFreeSize(main_id),now);
        if(!opened){
            sceIoRemove("ux0:data/open1602/freeze-report.previous.log");
            sceIoRename("ux0:data/open1602/freeze-report.log","ux0:data/open1602/freeze-report.previous.log");
            opened=true;
        }
        // Dedicated SDK stream: no dependency on a possibly blocked log/profile writer.
        const int fd=sceIoOpen("ux0:data/open1602/freeze-report.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
        if(fd>=0){
            const unsigned length=unsigned(n>0&&n<int(sizeof(text))?n:0);unsigned done=0;
            while(done<length){const int wrote=sceIoWrite(fd,text+done,length-done);if(wrote<=0)break;done+=unsigned(wrote);}
            sceIoSyncByFd(fd,0);sceIoClose(fd);
        }
        ++records;reported=age>=5000;last_report=now;
    }
}
}
const char* watch_phase(const char* literal){
    if(!started)return nullptr;
    return phase_name.exchange(literal,std::memory_order_relaxed);
}
void watch_frame(unsigned frame,std::uint64_t tick,unsigned zoom){
    if(!vita_boot_logging())return;
    if(!started){
        if(attempted)return;
        attempted=true;stopped=false;
        main_id=sceKernelGetThreadId();heartbeat_ms=now_ms();
        SceIoStat stat{};test_stall=sceIoGetstat("ux0:data/open1602/freeze-test.flag",&stat)>=0;
        try{observer=std::thread(observe);started=true;}
        catch(...){trace("FREEZE_OBSERVER unavailable=1");return;}
    }
    current_frame=frame;current_tick=unsigned(tick);current_zoom=zoom;
    phase_name="frame_begin";heartbeat_ms.store(now_ms(),std::memory_order_release);
    if(test_stall&&frame==180){WatchPhase test("qa_stall");sceKernelDelayThread(6500000);}
}
void watch_finish(){
    stopped.store(true,std::memory_order_release);
    if(observer.joinable())observer.join();
    started=false;
}
}
