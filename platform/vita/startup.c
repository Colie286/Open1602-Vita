// SPDX-License-Identifier: GPL-3.0-or-later
// Runs before newlib's heap/reentrancy/stdio and before any C++ constructors.
// No SDL, GXM, malloc, printf, TLS or C++ in this file. Each record is opened,
// written, synced and closed through kernel I/O, including both sides of calls.
#include "startup.h"
#include "boot_locale.h"
#include "logging_config.h"
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

// No allocation or libc calls: the normal catalogue is projected at build time.
static int boot_german=0;
static int logging_enabled=0;
int vita_boot_logging(void){return logging_enabled;}
static void early_logging(void){
    struct VitaLoggingConfig cfg={0};char block[512];
    const int fd=sceIoOpen("ux0:data/open1602/settings.cfg",SCE_O_RDONLY,0);
    if(fd<0)return;int n;while((n=sceIoRead(fd,block,sizeof(block)))>0)
        for(int i=0;i<n;++i)vita_logging_byte(&cfg,block[i]);
    sceIoClose(fd);vita_logging_line(&cfg);logging_enabled=cfg.enabled;
}
void vita_boot_language(const char* code){
    boot_german=code && (code[0]=='d'||code[0]=='D') && (code[1]=='e'||code[1]=='E') &&
        (!code[2]||code[2]=='-'||code[2]=='_'||code[2]=='.'||code[2]=='@'||code[2]=='\n'||code[2]==' '||code[2]=='\r');
}
const char* vita_boot_tr(const char* key){
    for(unsigned i=0;i<sizeof(boot_locale)/sizeof(boot_locale[0]);++i){
        const char *a=key,*b=boot_locale[i].key;while(*a&&*a==*b){++a;++b;}
        if(!*a&&!*b)return boot_german?boot_locale[i].de:boot_locale[i].en;
    }
    return key;
}
static void early_language_file(const char* path){
    // Only an early hint; installation detection later remains the authority.
    char cfg[8192]={0};int fd=sceIoOpen(path,SCE_O_RDONLY,0);
    if(fd<0)return;int n=sceIoRead(fd,cfg,sizeof(cfg)-8);sceIoClose(fd);if(n<=0)return;
    for(int i=0;i<n;){
        char* line=cfg+i;while(i<n&&cfg[i]!='\n')++i;if(i<n)cfg[i++]=0;
        while(*line==' '||*line=='\t')++line;
        const char* word="language";while(*word&&*word==*line){++word;++line;}
        if(*word)continue;while(*line==' '||*line=='\t')++line;if(*line++!='=')continue;
        while(*line==' '||*line=='\t')++line;
        if(line[0]=='a'&&line[1]=='u'&&line[2]=='t'&&line[3]=='o'){vita_boot_language(0);return;}
        vita_boot_language(line);return;
    }
}

static void early_language(void){
    early_language_file("ux0:data/open1602/menu-preferences.cfg");
    early_language_file("ux0:data/open1602/settings.cfg");
}

#ifdef O1602_BOOT_LAUNCHER
#define BOOT_LOG "ux0:data/open1602/launcher.log"
#define BOOT_PREVIOUS "ux0:data/open1602/launcher.previous.log"
#define HEAP_PHASE "BEGIN newlib heap 2 MiB / minimal launcher"
#define BOOT_HEADER "OPEN1602 VITA - MINIMAL STARTER 1.11"
#define LOG_HEADER "LOG: UX0:DATA/OPEN1602/LAUNCHER.LOG"
#else
#define BOOT_LOG "ux0:data/open1602/startup.log"
#define BOOT_PREVIOUS "ux0:data/open1602/startup.previous.log"
#define HEAP_PHASE "BEGIN newlib heap 128 MiB / full client"
#define BOOT_HEADER "OPEN1602 VITA - FULL CLIENT 1.11"
#define LOG_HEADER "LOG: UX0:DATA/OPEN1602/STARTUP.LOG"
#endif
static const char log_path[] = BOOT_LOG;
static char session_text[32]="vita-00000000";
const char* vita_boot_session(void){return session_text;}
static void init_session(void){
    unsigned int count=0;int fd=sceIoOpen("ux0:data/open1602/session.counter",SCE_O_RDONLY,0);
    if(fd>=0){if(sceIoRead(fd,&count,sizeof(count))!=(int)sizeof(count))count=0;sceIoClose(fd);}
    ++count;
#ifndef O1602_BOOT_LAUNCHER
    fd=sceIoOpen("ux0:data/open1602/session.counter",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    if(fd>=0){sceIoWrite(fd,&count,sizeof(count));sceIoSyncByFd(fd,0);sceIoClose(fd);}
#endif
    const char digits[]="0123456789abcdef";for(unsigned i=0;i<8;++i)session_text[5+i]=digits[(count>>((7-i)*4))&15];session_text[13]=0;
}
static SceUID display_uid = -1;
static SceUID heap_uid = -1;
static unsigned int *pixels;
static int automatic, handed_over, booting, log_failed;
static const char *injected_phase;
static unsigned int line;
static char last_failed_allocation[512];
static unsigned int length(const char *s) { unsigned int n=0; if(s) while(s[n]) ++n; return n; }
static int same(const char *a,const char *b) {
    if(!a||!b)return 0; while(*a&&*a==*b){++a;++b;} return *a==*b;
}
static void append(char *s,unsigned int *n,const char *text) {
    if(text)while(*text&&*n<490)s[(*n)++]=*text++; s[*n]=0;
}
static void hex(char *s,unsigned int *n,unsigned int value) {
    const char digits[]="0123456789abcdef";
    append(s,n,"0x");for(int i=7;i>=0;--i)if(*n<490)s[(*n)++]=digits[(value>>(i*4))&15];s[*n]=0;
}
// Buffered only after the first SDL frame. Startup phases/fatal reports still
// force durability; normal gameplay never syncs per event or per frame.
static char runtime_log[65536],runtime_client[65536],runtime_stderr[32768];
static char drained_log[65536],drained_client[65536],drained_stderr[32768];
static unsigned int runtime_used,runtime_client_used,runtime_stderr_used,dropped_records;
static volatile int log_lock,drain_lock;
static void lock_log(void){while(__sync_lock_test_and_set(&log_lock,1))sceKernelDelayThread(100);}
static void unlock_log(void){__sync_lock_release(&log_lock);}
static void raw_write(const char *path,const char *text,unsigned int size) {
    SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
    if(fd<0){log_failed=1;return;}
    if(sceIoLseek32(fd,0,SCE_SEEK_END)>2*1024*1024){
        sceIoClose(fd);fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
        if(fd<0){log_failed=1;return;}
        const char note[]="LOG_LIMIT: earlier runtime bytes discarded; session=";
        sceIoWrite(fd,note,sizeof(note)-1);sceIoWrite(fd,session_text,length(session_text));sceIoWrite(fd,"\n",1);
    }
    unsigned int done=0;
    while(done<size){int n=sceIoWrite(fd,text+done,size-done);if(n<=0){log_failed=1;break;}done+=(unsigned int)n;}
    sceIoSyncByFd(fd,0);sceIoClose(fd);
}
static void record_bytes(const char *path,char *buffer,unsigned int *used,unsigned int capacity,const char *text,unsigned int n,int newline) {
    if(!logging_enabled)return;
    lock_log();
    if(handed_over){
        if(n+(unsigned int)newline<=capacity-*used){for(unsigned int i=0;i<n;++i)buffer[(*used)++]=text[i];if(newline)buffer[(*used)++]='\n';}
        else ++dropped_records;
        unlock_log();
    }else{unlock_log();raw_write(path,text,n);if(newline)raw_write(path,"\n",1);}
}
void vita_boot_write(const char *text){record_bytes(log_path,runtime_log,&runtime_used,sizeof(runtime_log),text,length(text),1);}
void vita_boot_client_write(const char *text){record_bytes("ux0:data/open1602/client.log",runtime_client,&runtime_client_used,sizeof(runtime_client),text,length(text),1);}
int vita_boot_stderr_write(void *cookie,const char *text,int n){
    (void)cookie;if(n>0)record_bytes("ux0:data/open1602/logs/stderr.log",runtime_stderr,&runtime_stderr_used,sizeof(runtime_stderr),text,(unsigned int)n,0);return n;
}
void vita_boot_flush(void){
    if(!logging_enabled)return;
    // Only the writer (or a deliberate fatal/exit fence) does file I/O. The
    // producer lock is held for bounded RAM copies, never across a kernel write.
    while(__sync_lock_test_and_set(&drain_lock,1))sceKernelDelayThread(100);
    lock_log();
    const unsigned int a=runtime_used,b=runtime_client_used,c=runtime_stderr_used,dropped=dropped_records;
    for(unsigned int i=0;i<a;++i)drained_log[i]=runtime_log[i];
    for(unsigned int i=0;i<b;++i)drained_client[i]=runtime_client[i];
    for(unsigned int i=0;i<c;++i)drained_stderr[i]=runtime_stderr[i];
    runtime_used=runtime_client_used=runtime_stderr_used=dropped_records=0;
    unlock_log();
    if(a)raw_write(log_path,drained_log,a);
    if(b)raw_write("ux0:data/open1602/client.log",drained_client,b);
    if(c)raw_write("ux0:data/open1602/logs/stderr.log",drained_stderr,c);
    if(dropped){const char note[]="LOG_RING overflow: verbose records dropped\n";raw_write(log_path,note,sizeof(note)-1);}
    __sync_lock_release(&drain_lock);
}
// Small original 5x7 diagnostic font. No original game resources are required.
static const unsigned char glyphs[][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}
};
static void text_line(const char *text,unsigned int color) {
    if(!pixels)return;
    // Keep the heading and the last 22 records, rather than drawing beyond the framebuffer.
    if(line>=25){for(unsigned int i=960*54;i<960*544;++i)pixels[i]=0xff201008;line=2;}
    unsigned int x=24,y=18+line++*20;
    while(*text&&x<936){unsigned char c=(unsigned char)*text++;if(c>='a'&&c<='z')c-=32;
        int index=c>='A'&&c<='Z'?c-'A':c>='0'&&c<='9'?26+c-'0':-1;
        for(unsigned int row=0;row<7;++row){unsigned int bits=index>=0?glyphs[index][row]:
            c=='.'?(row==6?4:0):c==':'?((row==2||row==5)?4:0):
            c=='-'?(row==3?14:0):c=='/'?(1U<<row/2):c=='_'?(row==6?31:0):0;
            for(unsigned int col=0;col<5;++col)if(bits&(16U>>col))
                for(unsigned int dy=0;dy<2;++dy)for(unsigned int dx=0;dx<2;++dx)
                    pixels[(y+row*2+dy)*960+x+col*2+dx]=color;
        }x+=12;
    }
}
extern SceUID __real_sceKernelAllocMemBlock(const char*,SceKernelMemBlockType,SceSize,const SceKernelAllocMemBlockOpt*);
static void screen(void) {
    // 2 MiB CDRAM, direct scanout; no renderer, textures, stdio or heap needed.
    if(!pixels)display_uid=__real_sceKernelAllocMemBlock("o1602_boot",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,2*1024*1024,0);
    if(display_uid<0){vita_boot_write("FAIL raw display allocation");return;}
    void *base=0;
    if(sceKernelGetMemBlockBase(display_uid,&base)<0){vita_boot_write("FAIL raw display base");return;}
    pixels=base;for(unsigned int i=0;i<960*544;++i)pixels[i]=0xff201008;
    SceDisplayFrameBuf fb={0};fb.size=sizeof(fb);fb.base=pixels;fb.pitch=960;
    fb.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;fb.width=960;fb.height=544;
    const int result=sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME);
    line=0;text_line(BOOT_HEADER,0xffffffff);
    if(logging_enabled)text_line(LOG_HEADER,0xff80e0ff);
    vita_boot_write(result<0?"FAIL raw display SetFrameBuf":"OK raw display SetFrameBuf");
    sceDisplayWaitVblankStart();
}
void vita_boot_memory(const char *phase) {
    if(!logging_enabled)return;
    SceKernelFreeMemorySizeInfo info={0};info.size=sizeof(info);
    const int result=sceKernelGetFreeMemorySize(&info);
    char s[512];unsigned int n=0;append(s,&n,"MEM ");append(s,&n,phase);
    append(s,&n," result=");hex(s,&n,result);append(s,&n," main=");hex(s,&n,info.size_user);
    append(s,&n," cdram=");hex(s,&n,info.size_cdram);append(s,&n," phycont=");hex(s,&n,info.size_phycont);
    vita_boot_write(s);
}
void vita_boot_phase(const char *phase) {
    if(!handed_over)vita_boot_flush();
    vita_boot_write(phase);if(!handed_over)text_line(phase,0xffffffff);
    vita_boot_memory(phase);
    if(!handed_over)vita_boot_flush();
    if(same(phase,injected_phase))vita_boot_fatal("Injected startup failure (test only)");
}
void vita_boot_options(int auto_start,const char *fail_phase) {automatic=auto_start;injected_phase=fail_phase;}
void vita_boot_wait(void) {
    vita_boot_phase(vita_boot_tr("vita.boot.ready"));
    if(log_failed)text_line(vita_boot_tr("vita.boot.log_error"),0xff8080ff);
    if(automatic){sceKernelDelayThread(750000);return;}
    SceCtrlData pad={0};
    while(1){sceCtrlPeekBufferPositive(0,&pad,1);if(pad.buttons&SCE_CTRL_CROSS)break;
        if(pad.buttons&SCE_CTRL_CIRCLE)sceKernelExitProcess(0);sceKernelDelayThread(20000);}
    while(pad.buttons&SCE_CTRL_CROSS){sceCtrlPeekBufferPositive(0,&pad,1);sceKernelDelayThread(20000);}
}
void vita_boot_presented(void) {
    if(handed_over)return;handed_over=1;vita_boot_write("OK first SDL frame presented");
    sceDisplayWaitVblankStart();
    // Retain this bounded 2 MiB buffer for fatal reports. A display queue can still
    // refer to previous scanout after RenderPresent returns; freeing it here is unsafe.
    vita_boot_memory("first SDL frame");
}
void vita_boot_fatal(const char *message) {
    vita_boot_flush();vita_boot_write("FATAL - client stopped");vita_boot_write(message);vita_boot_memory("fatal");vita_boot_flush();
    // If SDL already owns scanout, restore a direct framebuffer. Do not run SDL
    // cleanup on a partially initialized GXM context or perform more heap allocations.
    if(handed_over||!pixels){screen();handed_over=0;}
    // A fatal report must never scroll its own cause off the bottom of the phase history.
    if(pixels){for(unsigned int i=960*54;i<960*544;++i)pixels[i]=0xff201008;line=2;}
    text_line(vita_boot_tr("vita.boot.failed"),0xff8080ff);
    // Wrap long messages into the diagnostic screen's 76-column area.
    while(*message){char row[77];unsigned int n=0;while(*message&&*message!='\n'&&n<76)row[n++]=*message++;
        if(*message=='\n')++message;row[n]=0;text_line(row,0xffffffff);}
    if(last_failed_allocation[0]) {
        text_line(vita_boot_tr("vita.boot.memblock"),0xff80e0ff);
        const char *failure=last_failed_allocation;
        while(*failure){char row[77];unsigned int n=0;while(*failure&&n<76)row[n++]=*failure++;
            row[n]=0;text_line(row,0xffffffff);}
    }
    if(logging_enabled)text_line(vita_boot_tr("vita.boot.send_logs"),0xff80e0ff);
    text_line(vita_boot_tr("vita.boot.exit"),0xffffffff);
    // A previously queued SDL display callback may finish after we take over.
    // Reassert scanout after allowing that queue to retire, without calling GXM.
    if(pixels){
        sceKernelDelayThread(150000);
        SceDisplayFrameBuf fb={0};fb.size=sizeof(fb);fb.base=pixels;fb.pitch=960;
        fb.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;fb.width=960;fb.height=544;
        sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME);
    }
    if(automatic){sceKernelDelayThread(2000000);sceKernelExitProcess(1);}
    SceCtrlData pad={0};while(1){sceCtrlPeekBufferPositive(0,&pad,1);
        if(pad.buttons&SCE_CTRL_CIRCLE)sceKernelExitProcess(1);sceKernelDelayThread(20000);}
}
extern void __real__init_vita_heap(void);
void __wrap__init_vita_heap(void) {
    sceIoMkdir("ux0:data",0777);sceIoMkdir("ux0:data/open1602",0777);
    early_language();
    early_logging();
    if(logging_enabled){
        init_session();
        sceIoRemove(BOOT_PREVIOUS);
        sceIoRename(log_path,BOOT_PREVIOUS);
    }
    vita_boot_write("BEGIN pre-newlib heap / build 1.11 / upstream 62a3683f");
    vita_boot_write(session_text);
    // Normal boot is silent; retain the durable pre-heap log. Only the explicit
    // diagnostic marker or a real fatal error takes over scanout.
    int diagnostic=sceIoOpen("ux0:data/open1602/diagnostic-start.flag",SCE_O_RDONLY,0);
    if(diagnostic>=0){sceIoClose(diagnostic);screen();}
    booting=1;vita_boot_phase(HEAP_PHASE);
    __real__init_vita_heap();booting=0;
    void *heap_base=0;
    if(heap_uid<0||sceKernelGetMemBlockBase(heap_uid,&heap_base)<0||!heap_base)
        vita_boot_fatal("newlib heap initialization failed (mutex, allocation or base)");
    vita_boot_phase("OK newlib heap");
}
extern void __real___libc_init_array(void);
void __wrap___libc_init_array(void) {
    vita_boot_phase("BEGIN C/C++ constructors");__real___libc_init_array();
    vita_boot_phase("OK C/C++ constructors");
}
SceUID __wrap_sceKernelAllocMemBlock(const char *name,SceKernelMemBlockType type,SceSize size,
                                    const SceKernelAllocMemBlockOpt *opt) {
    char s[512];unsigned int n=0;if(logging_enabled){append(s,&n,"ALLOC begin ");append(s,&n,name);
    append(s,&n," type=");hex(s,&n,type);append(s,&n," bytes=");hex(s,&n,size);vita_boot_write(s);}
    const int injected=injected_phase&&length(injected_phase)>6&&
        injected_phase[0]=='A'&&injected_phase[1]=='L'&&injected_phase[2]=='L'&&
        injected_phase[3]=='O'&&injected_phase[4]=='C'&&injected_phase[5]==':'&&same(name,injected_phase+6);
    if(injected)vita_boot_write("TEST ONLY: injected memblock failure");
    int over_budget=0;
#ifndef O1602_BOOT_LAUNCHER
    // Vita3K accepts memblocks larger than its reported free pool. A successful
    // emulator allocation must not bypass the hardware compatibility budget.
    SceKernelFreeMemorySizeInfo info={0};info.size=sizeof(info);
    if(sceKernelGetFreeMemorySize(&info)>=0) {
        if(type==SCE_KERNEL_MEMBLOCK_TYPE_USER_RW||type==SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE)
            over_budget=size>(unsigned int)info.size_user;
        else if(type==SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
            over_budget=size>(unsigned int)info.size_cdram;
        else if(type==SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW)
            over_budget=size>(unsigned int)info.size_phycont;
    }
    if(over_budget)vita_boot_write("BUDGET rejected: request exceeds reported free pool");
#endif
    SceUID uid=(injected||over_budget)?SCE_KERNEL_ERROR_NO_MEMORY:__real_sceKernelAllocMemBlock(name,type,size,opt);
    if(booting)heap_uid=uid;
    if(logging_enabled){n=0;append(s,&n,"ALLOC end ");append(s,&n,name);append(s,&n," uid=");hex(s,&n,uid);vita_boot_write(s);}
    if(uid<0){
        unsigned int saved=0;append(last_failed_allocation,&saved,name);append(last_failed_allocation,&saved," bytes=");
        hex(last_failed_allocation,&saved,size);append(last_failed_allocation,&saved," code=");hex(last_failed_allocation,&saved,uid);
        vita_boot_memory("allocation failed");if(booting)vita_boot_fatal("newlib heap allocation failed before main");
    }
    return uid;
}
extern SceUID __real_sceKernelCreateThread(const char*,SceKernelThreadEntry,int,SceSize,SceUInt,int,const SceKernelThreadOptParam*);
SceUID __wrap_sceKernelCreateThread(const char *name,SceKernelThreadEntry entry,int priority,SceSize stack,
                                    SceUInt attr,int affinity,const SceKernelThreadOptParam *opt) {
    if(!logging_enabled)return __real_sceKernelCreateThread(name,entry,priority,stack,attr,affinity,opt);
    char s[512];unsigned int n=0;append(s,&n,"THREAD begin ");append(s,&n,name);
    append(s,&n," stack=");hex(s,&n,stack);vita_boot_write(s);
    const SceUID uid=__real_sceKernelCreateThread(name,entry,priority,stack,attr,affinity,opt);
    n=0;append(s,&n,"THREAD end ");append(s,&n,name);append(s,&n," uid=");hex(s,&n,uid);vita_boot_write(s);return uid;
}

extern int __real_sceKernelFreeMemBlock(SceUID);
int __wrap_sceKernelFreeMemBlock(SceUID uid) {
    if(!logging_enabled)return __real_sceKernelFreeMemBlock(uid);
    char s[512];unsigned int n=0;
    append(s,&n,"FREE begin uid=");hex(s,&n,uid);vita_boot_write(s);
    const int result=__real_sceKernelFreeMemBlock(uid);
    n=0;append(s,&n,"FREE end uid=");hex(s,&n,uid);append(s,&n," result=");hex(s,&n,result);vita_boot_write(s);
    vita_boot_memory("after free");return result;
}
