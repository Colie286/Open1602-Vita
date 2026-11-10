// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/master_client.hpp"

#include <algorithm>
#include <atomic>
#include <thread>
#include <enet/enet.h>
#include "enet_library.hpp"
#include <charconv>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
namespace o1602::net {
namespace {
std::uint64_t now() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
template <class T>
bool number(std::string_view s, T& n) {
    auto [p, e] = std::from_chars(s.data(), s.data() + s.size(), n);
    return !s.empty() && e == std::errc{} && p == s.data() + s.size() && n != 0;
}
}  // namespace
std::string relay_address(std::uint64_t room, std::uint32_t member) {
    return "relay-" + std::to_string(room) + "-" + std::to_string(member);
}
std::optional<std::pair<std::uint64_t, std::uint32_t>> parse_relay_address(std::string_view s) {
    if (!s.starts_with("relay-"))
        return {};
    s.remove_prefix(6);
    auto at = s.find('-');
    if (at == s.npos)
        return {};
    std::uint64_t r = 0;
    std::uint32_t p = 0;
    if (!number(s.substr(0, at), r) || !number(s.substr(at + 1), p))
        return {};
    return std::pair{r, p};
}
std::optional<std::pair<std::string, std::uint16_t>> parse_master_endpoint(std::string_view s) {
    if (s.empty() || s.size() > 255)
        return {};
    auto at = s.find(':');
    auto host = s.substr(0, at);
    std::uint16_t port = master::kPort;
    std::string normalized(host);
    const bool numeric=std::all_of(host.begin(),host.end(),[](char c){return (c>='0'&&c<='9')||c=='.';});
    if(numeric){
        unsigned first=0;
        for(unsigned i=0;i<4;++i){
            const auto dot=host.find('.');const auto part=host.substr(0,dot);unsigned value=0;
            const auto [end,ec]=std::from_chars(part.data(),part.data()+part.size(),value);
            if(part.empty()||ec!=std::errc{}||end!=part.data()+part.size()||value>255||
               (part.size()>1&&part.front()=='0'))return {};
            if(i==0)first=value;
            if(i==3){if(dot!=host.npos)return {};}else{if(dot==host.npos)return {};host.remove_prefix(dot+1);}
        }
        if(first==0||first>=224)return {};
    }else{
        if(host.empty()||host.size()>253)return {};
        if(host.back()=='.')host.remove_suffix(1);
        while(!host.empty()){
            const auto dot=host.find('.');const auto label=host.substr(0,dot);
            if(label.empty()||label.size()>63||label.front()=='-'||label.back()=='-')return {};
            for(char c:label)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return {};
            if(dot==host.npos)break;
            host.remove_prefix(dot+1);if(host.empty())return {};
        }
        for(char& c:normalized)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    }
    if (at != s.npos && !number(s.substr(at + 1), port))
        return {};
    return std::pair{normalized, port};
}
namespace {
struct MasterLookup {std::atomic<bool> done{false};std::string ipv4;};
std::atomic<unsigned> active_master_lookups{0};
}
struct MasterClient::Impl {
    std::shared_ptr<MasterLookup> lookup;
    std::uint16_t port=master::kPort;
    std::unique_ptr<Transport> wire;
    bool ready = false, failed = false, listen = false, pending = false, opening = false, action_sent = false,
         info_dirty = false;
    std::string error;
    std::uint64_t started = now(), action_at = 0, last_list = 0, last_action = 0, room = 0, want_room = 0,
                  generation = 0;
    std::uint32_t request = 0, self = 0, owner = 0, target = 0, successor = 0;
    master::Kind action = master::Kind::Hello;
    std::vector<std::uint8_t> info;
    std::set<std::uint32_t> links;
    std::map<std::uint32_t, std::uint32_t> ping;
    std::vector<TransportEvent> events;
    std::size_t queued = 0;
    std::vector<FoundGame> games;
    std::map<std::uint32_t, std::deque<std::vector<std::uint8_t>>> outgoing;
    std::map<std::uint32_t, std::size_t> credits, peer_queued;
    std::map<std::uint32_t, std::uint64_t> closing;
    std::size_t outgoing_bytes = 0;
    std::uint32_t last_peer = 0;
    std::uint64_t tokens = master::kCreditWindow, last_budget = now();
    void clear_traffic() {
        outgoing.clear();
        closing.clear();
        credits.clear();
        peer_queued.clear();
        outgoing_bytes = 0;
        last_peer = 0;
    }
    void discard_peer(std::uint32_t id) {
        if (auto it = peer_queued.find(id); it != peer_queued.end()) {
            outgoing_bytes -= it->second;
            peer_queued.erase(it);
        }
        outgoing.erase(id);
        closing.erase(id);
        credits.erase(id);
        ping.erase(id);
    }
    void close_peer(std::uint32_t id, bool report) {
        if (links.erase(id)) {
            control(master::Kind::Close, id);
            discard_peer(id);
            if (report)
                events.push_back({TransportEvent::Kind::Disconnected, listen ? id : 0, {}});
        }
    }
    void enqueue(std::uint32_t id, std::span<const std::uint8_t> data) {
        // Original Session::send_bytes may enqueue an entire 32 MiB snapshot in one call.
        // Keep that bounded source queue apart from ENet's small in-flight queue.
        if (data.size() > kMaxPacketBytes || outgoing_bytes + data.size() > (72U << 20) ||
            peer_queued[id] + data.size() > (36U << 20)) {
            close_peer(id, true);
            return;
        }
        outgoing[id].emplace_back(data.begin(), data.end());
        outgoing_bytes += data.size();
        peer_queued[id] += data.size();
    }
    void drain(std::uint64_t time) {
        tokens = std::min<std::uint64_t>(
            master::kCreditWindow, tokens + std::min<std::uint64_t>(time - last_budget, 1000) * (4U << 20) / 1000);
        last_budget = time;
        auto held = wire->queued_bytes(0);
        std::size_t attempts = 0;
        while (!outgoing.empty() && attempts < outgoing.size()) {
            auto it = outgoing.upper_bound(last_peer);
            if (it == outgoing.end())
                it = outgoing.begin();
            last_peer = it->first;
            auto& queue = it->second;
            if (queue.empty()) {
                ++attempts;
                continue;
            }
            const auto size = queue.front().size();
            const auto cost = size + 29;
            if (size > credits[last_peer] || cost > tokens || held + cost > (2U << 20)) {
                ++attempts;
                continue;
            }
            master::Message x;
            x.kind = master::Kind::Data;
            x.peer = last_peer;
            x.data = std::move(queue.front());
            queue.pop_front();
            credits[last_peer] -= size;
            peer_queued[last_peer] -= size;
            outgoing_bytes -= size;
            tokens -= cost;
            held += cost;
            send(std::move(x));
            attempts = 0;
        }
        std::vector<std::uint32_t> finished;
        for (const auto& [id, deadline] : closing)
            if (outgoing[id].empty() || time >= deadline)
                finished.push_back(id);
        for (auto id : finished)
            close_peer(id, false);
    }

    void send(master::Message x) {
        if (wire) {
            auto b = master::encode(x);
            if (!b.empty())
                wire->send(0, b);
        }
    }
    void control(master::Kind kind, std::uint32_t peer = 0, std::uint32_t amount = 0) {
        master::Message x;
        x.kind = kind;
        x.peer = peer;
        x.room = room;
        x.successor = amount;
        send(std::move(x));
    }
    void broken(std::string why) {
        if (failed)
            return;
        failed = true;
        ready = false;
        error = std::move(why);
        std::fprintf(stderr, "MASTER_FAILED reason=%s\n", error.c_str());
        for (auto id : links)
            events.push_back({TransportEvent::Kind::Disconnected, listen ? id : 0, {}});
        if (!listen && links.empty() && generation)
            events.push_back({TransportEvent::Kind::Disconnected, 0, {}});
        links.clear();
        clear_traffic();
        ping.clear();
        games.clear();
        wire.reset();
    }
    void new_view(bool host, std::uint32_t to) {
        for (auto id : links)
            control(master::Kind::Close, id);
        links.clear();
        clear_traffic();
        ping.clear();
        events.clear();
        queued = 0;
        ++generation;
        listen = host;
        target = to;
        opening = false;
        action_sent = false;
        last_action = 0;
        ++request;
        if (!request)
            ++request;
    }
};
class MasterClient::Relay final : public Transport {
    std::shared_ptr<MasterClient> m_client;
    std::uint64_t m_generation;

public:
    explicit Relay(std::shared_ptr<MasterClient> c) : m_client(std::move(c)), m_generation(m_client->m->generation) {}
    ~Relay() override {
        auto& s = *m_client->m;
        if (m_generation == s.generation) {
            for (auto id : s.links)
                s.control(master::Kind::Close, id);
            s.links.clear();
            s.clear_traffic();
            s.events.clear();
            s.queued = 0;
            if (s.wire)
                s.wire->flush();
        }
    }
    void poll(std::vector<TransportEvent>& out) override {
        m_client->update();
        auto& s = *m_client->m;
        if (m_generation != s.generation)
            return;
        for (auto& e : s.events) {
            if (e.kind == TransportEvent::Kind::Received && !e.data.empty()) {
                const auto peer = s.listen ? e.link : s.target;
                if (s.links.contains(peer))
                    s.control(master::Kind::Credit, peer, static_cast<std::uint32_t>(e.data.size()));
            }
            out.push_back(std::move(e));
        }
        if (s.wire)
            s.wire->flush();
        s.events.clear();
        s.queued = 0;
    }
    void send(LinkId link, std::span<const std::uint8_t> data) override {
        auto& s = *m_client->m;
        const auto id = s.listen ? link : s.target;
        if (m_generation != s.generation || !s.links.contains(id) || s.closing.contains(id))
            return;
        s.enqueue(id, data);
    }
    void flush() override {
        m_client->update();
        if (m_client->m->wire)
            m_client->m->wire->flush();
    }
    void disconnect(LinkId link) override {
        auto& s = *m_client->m;
        auto id = s.listen ? link : s.target;
        if (m_generation == s.generation && s.links.contains(id))
            s.closing[id] = now() + 2000;
    }
    std::uint32_t round_trip_ms(LinkId link) const override {
        const auto& s = *m_client->m;
        auto id = s.listen ? link : s.target;
        auto it = s.ping.find(id);
        return s.wire ? s.wire->round_trip_ms(0) + (it == s.ping.end() ? 0 : it->second) : 0;
    }
    std::string address(LinkId link) const override {
        auto& s = *m_client->m;
        return relay_address(s.room, s.listen ? link : s.target);
    }
};
MasterClient::MasterClient(std::unique_ptr<Transport> t) : m(std::make_unique<Impl>()) {
    m->wire = std::move(t);
}
MasterClient::~MasterClient() {
    leave();
    if (m->wire)
        m->wire->flush();
}
core::Result<std::shared_ptr<MasterClient>> MasterClient::open(const std::string& endpoint) {
    const auto e = parse_master_endpoint(endpoint);
    if (!e)
        return core::make_error(core::ErrorCode::InvalidFormat, "invalid master_server address (hostname-or-IPv4[:port])");
    // Do not call inet_pton before EnetLibrary::acquire: Vita libc can initialise
    // its own network service implicitly, conflicting with the owned SDK pool.
    const bool numeric=std::all_of(e->first.begin(),e->first.end(),[](char c){return (c>='0'&&c<='9')||c=='.';});
    if(numeric){
        auto wire=enet_connect(e->first,e->second,master::kWireLimit,4U<<20);
        if(!wire)return wire.error();
        return std::shared_ptr<MasterClient>(new MasterClient(std::move(*wire)));
    }
    // Only DNS runs beside the UI. The transport is created/polled on its owner thread.
    // Keep ENet/platform services alive even if the menu discards this lookup.
    auto client=std::shared_ptr<MasterClient>(new MasterClient(nullptr));
    auto job=std::make_shared<MasterLookup>();client->m->lookup=job;client->m->port=e->second;
    if(active_master_lookups.fetch_add(1)>=4){--active_master_lookups;return core::make_error(core::ErrorCode::PlatformError,"master resolver busy");}
    if(!detail::EnetLibrary::acquire()){--active_master_lookups;return core::make_error(core::ErrorCode::PlatformError,"ENet could not be initialised");}
    try{
        std::thread([job,name=e->first]{
            ENetAddress found{};char ip[64]{};
            try{if(enet_address_set_host(&found,name.c_str())==0&&enet_address_get_host_ip(&found,ip,sizeof(ip))==0)job->ipv4=ip;}catch(...){}
            detail::EnetLibrary::release();--active_master_lookups;
            job->done.store(true,std::memory_order_release);
        }).detach();
    }catch(...){detail::EnetLibrary::release();--active_master_lookups;return core::make_error(core::ErrorCode::PlatformError,"cannot start master resolver");}
    return client;
}
bool MasterClient::ready() const {
    return m->ready && !m->failed;
}
bool MasterClient::failed() const {
    return m->failed;
}
bool MasterClient::hosting() const {
    return ready() && m->room && m->listen && !m->pending;
}
const std::string& MasterClient::error() const {
    return m->error;
}
const std::vector<FoundGame>& MasterClient::games() const {
    return m->games;
}
std::uint64_t MasterClient::room() const {
    return m->room;
}
std::uint32_t MasterClient::member() const {
    return m->self;
}
void MasterClient::refresh() {
    m->last_list = 0;
}
void MasterClient::leave() {
    if (m->room || m->pending)
        m->control(master::Kind::Leave);
    m->room = 0;
    m->pending = false;
    m->opening = false;
    m->action_sent = false;
    m->links.clear();
    m->clear_traffic();
    m->ping.clear();
    m->events.clear();
    m->queued = 0;
    ++m->generation;
}
void MasterClient::publish(const GameInfo& info, std::string_view successor) {
    auto bytes = encode_answer(0, info);
    if (bytes.size() > master::kMetadata)
        return;
    auto next = parse_relay_address(successor);
    auto id = next && next->first == m->room ? next->second : 0;
    if (bytes == m->info && id == m->successor)
        return;
    m->info = std::move(bytes);
    m->successor = id;
    m->info_dirty = true;
}
core::Result<std::unique_ptr<Transport>> MasterClient::listen() {
    if (m->failed)
        return core::make_error(core::ErrorCode::PlatformError, m->error);
    m->new_view(true, 0);
    m->action = m->room ? master::Kind::Claim : master::Kind::Create;
    m->pending = true;
    m->action_at = now();
    return std::unique_ptr<Transport>(std::make_unique<Relay>(shared_from_this()));
}
core::Result<std::unique_ptr<Transport>> MasterClient::take_over(std::string_view address) {
    const auto expected = parse_relay_address(address);
    if (!expected || !ready() || m->room != expected->first || m->self != expected->second)
        return core::make_error(core::ErrorCode::PlatformError,
                                "public successor no longer belongs to the original room");
    return listen();  // Nonzero original room: Claim only; the server checks the grant.
}
core::Result<std::unique_ptr<Transport>> MasterClient::connect(const std::string& address) {
    auto to = parse_relay_address(address);
    if (!to)
        return core::make_error(core::ErrorCode::InvalidFormat, "invalid relay room");
    if (m->failed)
        return core::make_error(core::ErrorCode::PlatformError, m->error);
    if (m->room && m->room != to->first)
        leave();
    m->new_view(false, to->second);
    m->want_room = to->first;
    m->action = master::Kind::Join;
    m->pending = m->room == 0;
    m->action_at = now();
    return std::unique_ptr<Transport>(std::make_unique<Relay>(shared_from_this()));
}
void MasterClient::update() {
    if(m->failed)return;
    if(m->lookup){
        if(now()-m->started>8000){m->broken("master.timeout");return;}
        if(!m->lookup->done.load(std::memory_order_acquire))return;
        const auto ip=m->lookup->ipv4;m->lookup.reset();
        if(ip.empty()){m->broken("master.unavailable");return;}
        auto wire=enet_connect(ip,m->port,master::kWireLimit,4U<<20);
        if(!wire){m->broken("master.unavailable");return;}
        m->wire=std::move(*wire);
    }
    if(!m->wire)return;
    std::vector<TransportEvent> in;
    m->wire->poll(in);
    for (auto& e : in) {
        if (e.kind == TransportEvent::Kind::Disconnected) {
            m->broken("master.disconnected");
            break;
        }
        if (e.kind == TransportEvent::Kind::Connected) {
            m->control(master::Kind::Hello);
            continue;
        }
        auto x = master::decode(e.data);
        if (!x) {
            m->broken("master.invalid_response");
            break;
        }
        switch (x->kind) {
            case master::Kind::Welcome:
                if (!x->peer) {
                    m->broken("master.invalid_response");
                    break;
                }
                m->self = x->peer;
                m->ready = true;
                std::fprintf(stderr, "MASTER_CONNECTED member=%u\n", unsigned(m->self));
                break;
            case master::Kind::Entered:
                if (!m->pending || x->successor != m->request ||
                    (m->action != master::Kind::Join && m->action != master::Kind::Create &&
                     m->action != master::Kind::Claim))
                    break;
                if (!x->room || !x->peer || (m->action == master::Kind::Join && x->room != m->want_room) ||
                    (m->action == master::Kind::Claim && x->room != m->room) || (m->listen && x->peer != m->self)) {
                    m->broken("master.invalid_response");
                    break;
                }
                m->room = x->room;
                m->owner = x->peer;
                m->pending = false;
                m->opening = false;
                m->info_dirty = true;
                std::fprintf(stderr, "MASTER_ROOM room=%llu member=%u host=%u hosting=%d\n",
                             static_cast<unsigned long long>(m->room), unsigned(m->self), unsigned(m->owner),
                             m->listen);
                break;
            case master::Kind::Rooms:
                m->games.clear();
                for (auto& room : x->rooms)
                    if (auto info = decode_answer(room.info))
                        m->games.push_back(
                            {relay_address(room.id, room.host), info->second, m->wire->round_trip_ms(0), 0});
                break;
            case master::Kind::Up:
                if (!m->room || x->room != m->room)
                    break;
                if (!m->generation || (!m->listen && x->peer != m->target)) {
                    m->control(master::Kind::Close, x->peer);
                    break;
                }
                if (m->links.insert(x->peer).second) {
                    m->credits[x->peer] = master::kCreditWindow;
                    m->events.push_back({TransportEvent::Kind::Connected, m->listen ? x->peer : 0, {}});
                }
                m->ping[x->peer] = x->successor;
                break;
            case master::Kind::Down:
                if (!m->room || x->room != m->room)
                    break;
                m->discard_peer(x->peer);
                if (m->links.erase(x->peer) || (!m->listen && m->opening && x->peer == m->target))
                    m->events.push_back({TransportEvent::Kind::Disconnected, m->listen ? x->peer : 0, {}});
                break;
            case master::Kind::Forward:
                if (!m->room || x->room != m->room)
                    break;
                if (!m->links.contains(x->peer))
                    break;
                if (m->queued + x->data.size() > 4U * kMaxPacketBytes) {
                    m->broken("master.queue_full");
                    break;
                }
                m->queued += x->data.size();
                m->ping[x->peer] = x->successor;
                m->events.push_back({TransportEvent::Kind::Received, m->listen ? x->peer : 0, std::move(x->data)});
                break;
            case master::Kind::Credit:
                if (x->room != m->room || !m->links.contains(x->peer))
                    break;
                if (!x->successor || x->successor > master::kCreditWindow) {
                    m->broken("master.invalid_response");
                    break;
                }
                m->credits[x->peer] = std::min<std::size_t>(master::kCreditWindow, m->credits[x->peer] + x->successor);
                break;
            case master::Kind::Error:
                if (x->successor && (!m->pending || x->successor != m->request))
                    break;
                m->broken(x->peer == static_cast<unsigned>(master::Error::Full)       ? "master.full"
                          : x->peer == static_cast<unsigned>(master::Error::NotFound) ? "master.room_missing"
                                                                                      : "master.refused");
                break;
            default:
                m->broken("master.invalid_response");
                break;
        }
        if (m->failed)
            break;
    }
    const auto time = now();
    if (!m->failed && ((!m->ready && time - m->started > 8000) || (m->pending && time - m->action_at > 8000))) {
        m->broken("master.timeout");
        return;
    }
    if (!m->ready || m->failed)
        return;
    if (m->pending && (!m->action_sent || (m->action == master::Kind::Claim && time - m->last_action >= 500))) {
        master::Message x;
        x.kind = m->action;
        x.room = m->want_room;
        x.successor = m->request;
        if (x.kind == master::Kind::Create) {
            if (m->info.empty())
                return;
            x.data = m->info;
        }
        m->send(std::move(x));
        m->action_sent = true;
        m->last_action = time;
    }
    if (m->room && m->listen && !m->pending && m->info_dirty && !m->info.empty()) {
        master::Message x;
        x.kind = master::Kind::Publish;
        x.successor = m->successor;
        x.data = m->info;
        m->send(std::move(x));
        m->info_dirty = false;
    }
    if (m->room && !m->listen && !m->opening) {
        m->control(master::Kind::Open, m->target);
        m->opening = true;
    }
    if (time - m->last_list >= 3000) {
        m->control(master::Kind::List);
        m->last_list = time;
    }
    m->drain(time);
    m->wire->flush();
}
}  // namespace o1602::net
