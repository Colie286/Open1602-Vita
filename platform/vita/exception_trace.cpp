// SPDX-License-Identifier: GPL-3.0-or-later
// Optional private diagnostic build: zero file I/O on successful allocations.
#include "startup.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <typeinfo>
#include <unwind-arm-common.h>
namespace {
void record(const char* label,std::uintptr_t value){
    char line[128];const auto n=std::strlen(label);std::memcpy(line,label,n);line[n]=' ';
    for(unsigned i=0;i<8;++i)line[n+1+i]="0123456789abcdef"[(value>>(28-i*4))&15U];
    line[n+9]=0;vita_boot_write(line);
}
_Unwind_Reason_Code frame(_Unwind_Context* context,void* count){
    auto& n=*static_cast<unsigned*>(count);if(n++>=24)return _URC_END_OF_STACK;
    _uw pc=0;_Unwind_VRS_Get(context,_UVRSC_CORE,15,_UVRSD_UINT32,&pc);record("CXX_PC",pc);return _URC_NO_REASON;
}
}
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __wrap_malloc(std::size_t bytes){
    void* p=__real_malloc(bytes);
    if(!p&&bytes){record("MALLOC_FAILED_BYTES",bytes);record("MALLOC_CALLER",reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));vita_boot_flush();}
    return p;
}
extern "C" [[noreturn]] void __real___cxa_throw(void*,std::type_info*,void(*)(void*));
extern "C" [[noreturn]] void __wrap___cxa_throw(void* object,std::type_info* type,void(*destroy)(void*)){
    if(std::strcmp(type->name(),"St9bad_alloc")==0){
        record("CXX_BAD_ALLOC_CALLER",reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
        vita_boot_flush();unsigned count=0;_Unwind_Backtrace(frame,&count);vita_boot_flush();
    }
    __real___cxa_throw(object,type,destroy);
}
