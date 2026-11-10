// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal hardware boot probe. This is a launcher, never a second game engine.
#include "startup.h"
#include <psp2/appmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

unsigned int _newlib_heap_size_user = 2 * 1024 * 1024;
int main(int argc,char **argv) {
    for(int i=1;i+1<argc;++i)if(strcmp(argv[i],"--language")==0)vita_boot_language(argv[i+1]);
    int automatic=0;
    for(int i=1;i<argc;++i)if(strcmp(argv[i],"--vita-boot-auto")==0)automatic=1;
    vita_boot_options(automatic,0);
    vita_boot_phase("OK minimal launcher main / heap 2 MiB");
    vita_boot_phase("NO SDL / GXM / AUDIO / NETWORK / GAME ASSETS");
    for(int i=1;i<argc;++i)if(strcmp(argv[i],"--vita-memory-probe")==0) {
        SceKernelFreeMemorySizeInfo info={0};info.size=sizeof(info);
        if(sceKernelGetFreeMemorySize(&info)<0)vita_boot_fatal("Memory probe: free-memory query failed");
        // Diagnostic only, never a default startup allocation. Do not touch the block.
        const unsigned int requested=((unsigned int)info.size_user&~4095U)+1024U*1024U;
        const SceUID uid=sceKernelAllocMemBlock("overcommit_probe",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,requested,0);
        char message[160];snprintf(message,sizeof(message),"PROBE reported_free=%u requested=%u uid=0x%08x",(unsigned int)info.size_user,requested,(unsigned int)uid);
        vita_boot_phase(message);
        vita_boot_phase(uid<0?"PROBE rejected as expected":"PROBE ACCEPTED beyond reported free pool");
        if(uid>=0)sceKernelFreeMemBlock(uid);
        sceKernelDelayThread(2000000);sceKernelExitProcess(0);
    }
    // Normal start proceeds immediately. A private marker restores the visible gate.
    if(FILE* flag=fopen("ux0:data/open1602/diagnostic-start.flag","r")){fclose(flag);vita_boot_wait();}
    // SDK crt0 permits at most 31 arguments, including the executable name.
    char *next[32];int count=0;
    next[count++]="--vita-client-from-launcher";
    for(int i=1;i<argc&&count<30;++i)next[count++]=argv[i];
    next[count]=0;
    vita_boot_phase("BEGIN LoadExec app0:/client.self");
    const int result=sceAppMgrLoadExec("app0:/client.self",next,0);
    if(result>=0){
        vita_boot_phase("OK LoadExec accepted / waiting for client entry");
        for(;;)sceKernelDelayThread(100000);
    }
    char message[120];snprintf(message,sizeof(message),"LoadExec returned 0x%08x - full client not started",(unsigned int)result);
    vita_boot_fatal(message);
}
