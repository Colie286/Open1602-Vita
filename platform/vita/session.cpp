// SPDX-License-Identifier: GPL-3.0-or-later
#include "startup.h"
#include "o1602/platform/vita.hpp"
#include "o1602/core/profile.hpp"
#include <psp2/kernel/processmgr.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
namespace o1602::platform::vita {
namespace {
std::atomic<unsigned> first_zoom{99},requested_zoom{1},first_wait{0};
std::atomic<int> warm{-1};std::atomic<bool> completed{false};
std::mutex data_mutex;std::string edition="History",data_root="ux0:data/open1602/original";
std::string json_text(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\')out+='\\';if(c>=32)out+=char(c);}return out+'"';}
void copy_tail(const std::filesystem::path& from,const std::filesystem::path& to){
    std::ifstream in(from,std::ios::binary);if(!in)return;
    in.seekg(0,std::ios::end);const auto size=static_cast<std::streamoff>(in.tellg());constexpr std::streamoff limit=2*1024*1024;
    in.seekg(size>limit?size-limit:std::streamoff{0});std::ofstream out(to,std::ios::binary);out<<in.rdbuf();
}
}
void session_data(const std::string& root,unsigned zoom,bool history){
    if(!vita_boot_logging())return;
    std::lock_guard lock(data_mutex);data_root=root;edition=history?"History Edition":"Classic/Koenig or custom";requested_zoom=zoom;
}
void session_world(unsigned zoom,unsigned wait,bool cache_hit){
    if(!vita_boot_logging())return;
    unsigned expected=99;if(first_zoom.compare_exchange_strong(expected,zoom)){first_wait=wait;warm=cache_hit;}
}
void session_complete(){completed=true;}
std::string session_profile(std::string json){
    if(json.empty()||json.back()!='}')return json;
    std::lock_guard lock(data_mutex);json.pop_back();
    if(json.size()>1)json+=',';
    json+="\"session\":{\"id\":"+json_text(vita_boot_session())+",\"version\":\"1.10\",\"commit\":\"" O1602_VITA_COMMIT "\",\"completed\":"+(completed.load()?"true":"false")+
        ",\"elapsed_ms\":"+std::to_string(sceKernelGetProcessTimeWide()/1000)+",\"edition\":"+json_text(edition)+",\"data_root\":"+json_text(data_root)+
        ",\"requested_zoom\":"+std::to_string(requested_zoom.load())+",\"first_world_zoom\":"+std::to_string(first_zoom.load())+",\"first_world_wait_ms\":"+std::to_string(first_wait.load())+
        ",\"warm_cache\":"+(warm.load()<0?"null":warm.load()?"true":"false")+"}}";return json;
}
namespace {
void archive_logs(bool previous){
    if(!vita_boot_logging())return;
    try{
        const std::filesystem::path root="ux0:data/open1602",base=root/"sessions";
        std::string id=vita_boot_session(),profile;
        {std::ifstream f(root/"profile.json");profile.assign(std::istreambuf_iterator<char>(f),{});}
        if(previous){
            std::ifstream log(root/"client.log");std::string line;id.clear();
            for(unsigned n=0;n<32&&std::getline(log,line);++n){const auto at=line.find("SESSION id=");if(at!=std::string::npos){const auto begin=at+11;id=line.substr(begin,line.find(' ',begin)-begin);break;}}
            if(id.empty()){if(profile.empty())return;id="legacy-before-"+std::string(vita_boot_session());}
            // A fatal start may leave the preceding session's profile behind.
            // Never attach that older profile to a different identified client.
            if(!id.starts_with("legacy-")&&profile.find("\"id\":\""+id+"\"")==std::string::npos)profile.clear();
        }
        std::string old;{std::ifstream f(base/"0/session.json");old.assign(std::istreambuf_iterator<char>(f),{});}
        if(old.find("\"id\":\""+id+"\"")!=std::string::npos)return;
        std::error_code ec;std::filesystem::create_directories(base,ec);
        std::filesystem::remove_all(base/"2",ec);std::filesystem::rename(base/"1",base/"2",ec);ec.clear();
        std::filesystem::rename(base/"0",base/"1",ec);std::filesystem::create_directories(base/"0",ec);
        copy_tail(root/(previous?"startup.previous.log":"startup.log"),base/"0/startup.log");
        for(const auto* name:{"client.log","analog-snapshot.txt","analog-final.txt"})copy_tail(root/name,base/"0"/name);
        copy_tail(root/"logs/stderr.log",base/"0/stderr.log");
        if(!profile.empty()){std::ofstream f(base/"0/profile.json");f<<profile;}
        std::ofstream summary(base/"0/session.json");
        if(previous)summary<<"{\"session\":{\"id\":"<<json_text(id)<<",\"archived_at_next_start\":true,\"profile_available\":"<<(!profile.empty()?"true":"false")<<"}}";
        // The last capture was consumed by write_profile; still write a valid session identity.
        else summary<<session_profile("{}");
    }catch(...){vita_boot_write("WARN bounded session archive unavailable");}
}
}
void archive_previous_session(){archive_logs(true);}
void archive_session(){archive_logs(false);}
}
