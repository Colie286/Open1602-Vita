// SPDX-License-Identifier: GPL-3.0-or-later
// The game uses its original ENet sessions. Only this native socket boundary changes.
#include "o1602/net/connectivity.hpp"
#include "o1602/core/profile.hpp"
#include "o1602/platform/vita.hpp"
#include "adhoc_support.hpp"
#include <enet/enet.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/netcheck_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace o1602::platform::vita {
bool initialize_network();
bool terminate_network();
namespace {
using net::NetworkMode;
probe::Sockets sockets;
std::array<int,16> bound=[] {std::array<int,16> a;a.fill(-1);return a;}();
bool test_loopback=false;
probe::Ip bytes(SceNetInAddr a){probe::Ip b;std::memcpy(b.data(),&a.s_addr,4);return b;}
std::string text(probe::Ip ip){char s[16];std::snprintf(s,sizeof(s),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);return s;}
bool parse(const std::string& s,probe::Ip& ip){
    unsigned a,b,c,d;char tail;
    if(std::sscanf(s.c_str(),"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail)!=4||a>255||b>255||c>255||d>255)return false;
    ip={std::uint8_t(a),std::uint8_t(b),std::uint8_t(c),std::uint8_t(d)};return text(ip)==s;
}
int native_close(int id){int rc=sceNetSocketClose(id);trace("ADHOC_SOCKET_CLOSE id=%d rc=0x%08x",id,unsigned(rc));if(rc>=0)for(int& b:bound)if(b==id)b=-1;return rc;}
class Radio final : public net::Connectivity {
public:
    net::ConnectivityState view;
    bool dialog=false,attempted=false;
    Radio(){view.supported=true;view.ready=true;}
    net::ConnectivityState state()const override {auto s=view;s.sockets=sockets.count();return s;}
    bool input_blocked()const override {return dialog;}
    core::Status failure(const char* key,int rc){view.status_key=key;char s[32];std::snprintf(s,sizeof(s),"0x%08x",unsigned(rc));view.error=s;trace("ADHOC_ERROR key=%s rc=%s",key,s);return core::make_error(core::ErrorCode::PlatformError,s);}
    bool stop(){
        view.ready=false;
        if(dialog){const int a=sceNetCheckDialogAbort();const int t=sceNetCheckDialogTerm();trace("ADHOC_DIALOG_ABORT rc=%d term=%d",a,t);if(t<0)return false;dialog=false;}
        sockets.clear(native_close);
        trace("ADHOC_SOCKETS_REMAIN count=%u",sockets.count());
        if(sockets.count())return false;
        if(attempted){
            int state=-1;const int sr=sceNetCtlAdhocGetState(&state);
            const int rc=sr>=0&&state==SCE_NETCTL_STATE_DISCONNECTED?0:sceNetCtlAdhocDisconnect();
            trace("ADHOC_DISCONNECT rc=0x%08x state_rc=%d state=%d",unsigned(rc),sr,state);
            if(rc<0)return false;attempted=false;
        }
        return terminate_network();
    }
    core::Status select(NetworkMode mode)override {
        core::profile::Scope selecting(core::profile::NetworkSelect);
        if(net::network_in_use())return failure("vita.net.busy",-1);
        if(!stop())return failure("vita.net.cleanup_failed",-1);
        view={};view.supported=true;view.mode=mode;
        if(mode==NetworkMode::Internet){view.ready=true;view.status_key="vita.net.wlan";trace("CONNECTIVITY mode=wlan deferred=1");return core::Ok{};}
        view.ready=false;
        if(!initialize_network())return failure("vita.net.init_failed",-1);
        if(test_loopback){view.ready=true;view.own_ip="127.0.0.1";view.candidates={"127.0.0.1"};view.status_key="vita.net.emulator";trace("ADHOC_TEST_LOOPBACK explicit=1 radio_proof=0");return core::Ok{};}
        SceNetCheckDialogParam p;sceNetCheckDialogParamInit(&p);p.mode=SCE_NETCHECK_DIALOG_MODE_ADHOC_CONN;
        std::memcpy(p.npCommunicationId.data,"O1602GAME",9);p.timeoutUs=60*1000000;
        const int rc=sceNetCheckDialogInit(&p);trace("ADHOC_DIALOG group=O1602GAME mode=1 rc=0x%08x",unsigned(rc));
        if(rc<0){terminate_network();return failure("vita.net.dialog_failed",rc);}
        dialog=true;attempted=true;view.pending=true;view.status_key="vita.net.dialog";return core::Ok{};
    }
    bool valid_peer(const std::string& s)const override {
        probe::Ip ip,own;if(!parse(s,ip)||!parse(view.own_ip,own))return false;
        return test_loopback?ip==probe::Ip{127,0,0,1}:probe::remote_ip(ip,own);
    }
    void refresh()override {
        if(view.mode!=NetworkMode::Adhoc||dialog||test_loopback)return;
        int state=-1;SceNetInAddr own{};const int sr=sceNetCtlAdhocGetState(&state),ar=sceNetCtlAdhocGetInAddr(&own);
        view.ready=sr>=0&&ar>=0&&state==SCE_NETCTL_STATE_CONNECTED&&probe::link_local(bytes(own));view.own_ip=text(bytes(own));view.candidates.clear();
        if(!view.ready){view.status_key="vita.net.not_connected";return;}
        // Two independent captures. SDK8 chains are bounded; physical records of unknown
        // stride are ONLY accepted as a single candidate, never as a guessed array.
        struct alignas(16) Capture {std::array<std::byte,16384> data;std::array<std::byte,64> guard;};
        // No permanently reserved radio query buffers in the single-player process.
        std::unique_ptr<std::array<Capture,2>> captures(new(std::nothrow) std::array<Capture,2>);
        if(!captures){view.status_key="vita.net.manual_hint";trace("ADHOC_PEER_QUERY allocation_failed=1 bytes=%u",unsigned(sizeof(std::array<Capture,2>)));return;}
        std::array<probe::PeerResult,2> decoded;std::array<unsigned,2> counts{};
        for(unsigned pass=0;pass<2;++pass){
            auto& b=(*captures)[pass];b.data.fill(std::byte{});b.guard.fill(std::byte{0xa5});unsigned n=0;const int qr=sceNetCtlAdhocGetPeerList(&n,nullptr);const unsigned requested=n;
            int rc=qr;if(qr>=0&&n>0&&n<=8)rc=sceNetCtlAdhocGetPeerList(&n,b.data.data());
            const bool guarded=std::all_of(b.guard.begin(),b.guard.end(),[](std::byte v){return v==std::byte{0xa5};});std::size_t extent=0;
            for(std::size_t i=0;i<b.data.size();++i)if(b.data[i]!=std::byte{})extent=i+1;
            trace("ADHOC_PEER_QUERY pass=%u query_rc=%08x requested=%u returned=%u rc=%08x capacity=16384 changed=%u guard=%u sdk_size=8",pass,unsigned(qr),requested,n,unsigned(rc),unsigned(extent),unsigned(guarded));
            for(unsigned at=0;at<std::min<unsigned>(128,std::max<unsigned>(16,unsigned(extent)));at+=16){char hex[49]{};for(unsigned j=0;j<16;++j)std::snprintf(hex+j*3,4,"%02x ",unsigned(b.data[at+j]));trace("ADHOC_PEER_RAW pass=%u offset=%u bytes=%s",pass,at,hex);}
            if(!guarded){view.ready=false;view.status_key="vita.net.peer_invalid";return;}
            if(qr<0||rc<0||n>8||requested!=n)continue;
            counts[pass]=n;decoded[pass]=probe::peers(b.data,unsigned(reinterpret_cast<std::uintptr_t>(b.data.data())),n,extent,bytes(own));
            if(decoded[pass].error&&n==1&&extent>=56&&extent<=128){
                decoded[pass]=probe::single_firmware_candidate(b.data,n,extent,bytes(own));
                if(decoded[pass].count)trace("ADHOC_PEER_CANDIDATE format=physical-single offset=0 validated_game_answer_required=1");
            }
        }
        if(counts[0]==counts[1]&&decoded[0].count==decoded[1].count){
            bool same=true;for(unsigned i=0;i<decoded[0].count;++i)same=same&&decoded[0].ips[i]==decoded[1].ips[i];
            if(same)for(unsigned i=0;i<decoded[0].count;++i)view.candidates.push_back(text(decoded[0].ips[i]));
        }
        view.status_key=view.candidates.empty()?"vita.net.manual_hint":"vita.net.ready";
        trace("ADHOC_READY own=%s candidates=%u status=%s",view.own_ip.c_str(),unsigned(view.candidates.size()),view.status_key.c_str());
    }
    void update()override {
        if(!dialog)return;
        if(sceNetCheckDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return;
        SceNetCheckDialogResult r{};const int rc=sceNetCheckDialogGetResult(&r);const int term=sceNetCheckDialogTerm();
        trace("ADHOC_DIALOG_RESULT rc=%d result=%d term=%d",rc,r.result,term);
        if(term<0){failure("vita.net.dialog_failed",term);return;}
        dialog=false;view.pending=false;
        if(rc<0||r.result!=0){view.ready=false;failure("vita.net.cancelled",rc<0?rc:r.result);return;}
        refresh();
    }
} radio;
bool adhoc(){return radio.view.mode==NetworkMode::Adhoc;}
SceNetSockaddrIn address(const ENetAddress* a){SceNetSockaddrIn s{};s.sin_len=sizeof(s);s.sin_family=SCE_NET_AF_INET;s.sin_port=sceNetHtons(SCE_NET_ADHOC_PORT);s.sin_vport=sceNetHtons(a?a->port:0);if(a)s.sin_addr.s_addr=a->host;return s;}
void socket_error(const char* op,int rc){const int e=*sceNetErrnoLoc();char msg[80];std::snprintf(msg,sizeof(msg),"%s: 0x%08x / errno %d",op,unsigned(rc),e);radio.view.error=msg;radio.view.status_key=e==SCE_NET_EADDRINUSE||unsigned(rc)==0x80410130u?"vita.net.port_busy":"vita.net.socket_failed";trace("ADHOC_SOCKET_ERROR operation=%s rc=%08x errno=%d",op,unsigned(rc),e);}
bool is_bound(int id){return std::find(bound.begin(),bound.end(),id)!=bound.end();}
void mark_bound(int id){for(int& b:bound)if(b<0){b=id;return;}}
}
void setup_connectivity(){net::install_connectivity(&radio);}
void finish_connectivity(){if(!net::network_in_use())radio.stop();else trace("NETWORK stop_deferred users_active=1");net::install_connectivity(nullptr);}
bool adhoc_loopback_test(){test_loopback=true;return bool(radio.select(NetworkMode::Adhoc));}
}
using namespace o1602::platform::vita;
extern "C" {
ENetSocket __real_enet_socket_create(ENetSocketType);
void __real_enet_socket_destroy(ENetSocket);
int __real_enet_socket_bind(ENetSocket,const ENetAddress*);
int __real_enet_socket_get_address(ENetSocket,ENetAddress*);
int __real_enet_socket_set_option(ENetSocket,ENetSocketOption,int);
int __real_enet_socket_wait(ENetSocket,enet_uint32*,enet_uint32);
int __real_enet_socket_send(ENetSocket,const ENetAddress*,const ENetBuffer*,size_t);
int __real_enet_socket_receive(ENetSocket,ENetAddress*,ENetBuffer*,size_t);
int __wrap_enet_socket_bind(ENetSocket id,const ENetAddress* a){
    if(!adhoc())return __real_enet_socket_bind(id,a);
    if(!sockets.owns(id)||is_bound(id))return -1;
    ENetAddress at=a?*a:ENetAddress{};
    // ENet clients and the original browser ask for an ephemeral local port.
    // Assign the native virtual-port range explicitly; fixed host ports are NEVER changed.
    if(!at.port){
        for(unsigned p=46061;p<46077;++p){at.port=enet_uint16(p);auto s=address(&at);int rc=sceNetBind(id,reinterpret_cast<SceNetSockaddr*>(&s),sizeof(s));if(rc>=0){mark_bound(id);trace("ADHOC_BIND id=%d virtual=%u ephemeral=1 rc=0",id,p);return 0;}if(*sceNetErrnoLoc()!=SCE_NET_EADDRINUSE){socket_error("Bind ephemeral",rc);return -1;}}
        socket_error("Bind ephemeral exhausted",-1);return -1;
    }
    auto s=address(&at);int rc=sceNetBind(id,reinterpret_cast<SceNetSockaddr*>(&s),sizeof(s));trace("ADHOC_BIND id=%d base=3658 virtual=%u rc=%08x",id,at.port,unsigned(rc));if(rc<0)socket_error("Bind",rc);else mark_bound(id);return rc;
}
ENetSocket __wrap_enet_socket_create(ENetSocketType type){
    if(!adhoc())return __real_enet_socket_create(type);
    if(type!=ENET_SOCKET_TYPE_DATAGRAM||!radio.view.ready)return ENET_SOCKET_NULL;
    const int id=sceNetSocket("Open1602Adhoc",SCE_NET_AF_INET,SCE_NET_SOCK_DGRAM_P2P,0);trace("ADHOC_SOCKET_CREATE id=%d",id);
    if(id<0){socket_error("Create",id);return ENET_SOCKET_NULL;}
    if(!sockets.add(id)){native_close(id);return ENET_SOCKET_NULL;}
    int v=1;int rc=sceNetSetsockopt(id,SCE_NET_SOL_SOCKET,SCE_NET_SO_NBIO,&v,sizeof(v));
    if(rc<0){socket_error("NBIO",rc);sockets.close(id,native_close);return ENET_SOCKET_NULL;}return id;
}
void __wrap_enet_socket_destroy(ENetSocket id){
    if(sockets.owns(id)){sockets.close(id,native_close);trace("ADHOC_SOCKETS_REMAIN count=%u",sockets.count());}
    else if(!adhoc())__real_enet_socket_destroy(id);
    else trace("ADHOC_SOCKET_CLOSE_UNOWNED id=%d",id);
}
int __wrap_enet_socket_set_option(ENetSocket id,ENetSocketOption option,int value){
    if(!adhoc())return __real_enet_socket_set_option(id,option,value);
    if(!sockets.owns(id))return -1;
    int native=0;switch(option){case ENET_SOCKOPT_NONBLOCK:native=SCE_NET_SO_NBIO;break;case ENET_SOCKOPT_BROADCAST:native=SCE_NET_SO_BROADCAST;break;case ENET_SOCKOPT_RCVBUF:native=SCE_NET_SO_RCVBUF;break;case ENET_SOCKOPT_SNDBUF:native=SCE_NET_SO_SNDBUF;break;default:return -1;}
    int rc=sceNetSetsockopt(id,SCE_NET_SOL_SOCKET,native,&value,sizeof(value));if(rc<0)socket_error("Option",rc);return rc;
}
int __wrap_enet_socket_get_address(ENetSocket id,ENetAddress* a){
    if(!adhoc())return __real_enet_socket_get_address(id,a);
    if(!sockets.owns(id))return -1;
    SceNetSockaddrIn s{};unsigned n=sizeof(s);int rc=sceNetGetsockname(id,reinterpret_cast<SceNetSockaddr*>(&s),&n);
    if(rc>=0&&n==sizeof(s)){a->host=s.sin_addr.s_addr;a->port=sceNetNtohs(s.sin_vport);return 0;}socket_error("Getname",rc);return -1;
}
int __wrap_enet_socket_wait(ENetSocket id,enet_uint32* condition,enet_uint32 timeout){
    if(!adhoc())return __real_enet_socket_wait(id,condition,timeout);
    *condition=ENET_SOCKET_WAIT_NONE;if(timeout){trace("ADHOC_BLOCKING_WAIT_REJECTED timeout=%u",timeout);return -1;}return 0;
}
int __wrap_enet_socket_send(ENetSocket id,const ENetAddress* to,const ENetBuffer* buffers,size_t count){
    if(!adhoc())return __real_enet_socket_send(id,to,buffers,count);
    if(!sockets.owns(id)||!to)return -1;
    if(!is_bound(id)){ENetAddress local{};if(__wrap_enet_socket_bind(id,&local)<0)return -1;}
    std::array<unsigned char,ENET_PROTOCOL_MAXIMUM_MTU> packet{};unsigned n=0;
    for(size_t i=0;i<count;++i){if(buffers[i].dataLength>packet.size()-n)return -1;std::memcpy(packet.data()+n,buffers[i].data,buffers[i].dataLength);n+=buffers[i].dataLength;}
    auto s=address(to);int rc=sceNetSendto(id,packet.data(),n,SCE_NET_MSG_DONTWAIT,reinterpret_cast<SceNetSockaddr*>(&s),sizeof(s));
    if(rc>=0){radio.view.sent+=unsigned(rc);return rc;}if(*sceNetErrnoLoc()==SCE_NET_EWOULDBLOCK)return 0;socket_error("Sendto",rc);return -1;
}
int __wrap_enet_socket_receive(ENetSocket id,ENetAddress* from,ENetBuffer* buffers,size_t count){
    if(!adhoc())return __real_enet_socket_receive(id,from,buffers,count);
    if(!sockets.owns(id))return -1;
    std::array<unsigned char,ENET_PROTOCOL_MAXIMUM_MTU+1> packet{};SceNetSockaddrIn s{};unsigned n=sizeof(s);
    int rc=sceNetRecvfrom(id,packet.data(),packet.size(),SCE_NET_MSG_DONTWAIT,reinterpret_cast<SceNetSockaddr*>(&s),&n);
    if(rc<0){if(*sceNetErrnoLoc()==SCE_NET_EWOULDBLOCK)return 0;socket_error("Recvfrom",rc);return -1;}
    size_t cap=0;for(size_t i=0;i<count;++i)cap=std::min(packet.size(),cap+std::min(packet.size(),buffers[i].dataLength));
    if(unsigned(rc)>cap||unsigned(rc)>ENET_PROTOCOL_MAXIMUM_MTU||n!=sizeof(s)||s.sin_family!=SCE_NET_AF_INET)return -2;
    size_t at=0;for(size_t i=0;i<count&&at<unsigned(rc);++i){auto len=std::min<size_t>(buffers[i].dataLength,unsigned(rc)-at);std::memcpy(buffers[i].data,packet.data()+at,len);at+=len;}
    if(from){from->host=s.sin_addr.s_addr;from->port=sceNetNtohs(s.sin_vport);}radio.view.received+=unsigned(rc);return rc;
}
}
