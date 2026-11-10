// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/loopback.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "o1602/sim/random.hpp"

namespace o1602::net {

namespace {

constexpr LinkId kHostLink = 0;

}  // namespace

struct LoopbackNetwork::Impl {
    struct Pending {
        std::uint64_t due = 0;
        TransportEvent event;
    };
    /// One computer's connection to the host; computer i (from 1) is link i on the host.
    struct Link {
        bool to_host_open = true;    ///< the host still hears this computer
        bool to_client_open = true;  ///< the computer still hears the host
        bool cut = false;
        std::uint64_t last_to_host = 0;  ///< messages of a link keep their order
        std::uint64_t last_to_client = 0;
        std::vector<Pending> client_inbox;
    };

    std::uint32_t latency = 0;
    std::uint32_t jitter = 0;
    sim::Random random;
    std::uint64_t now = 0;
    bool host_open = false;
    std::vector<Pending> host_inbox;
    std::vector<Link> links{1};  ///< index 0 unused: link ids start at 1
    std::uint64_t messages = 0;
    std::uint64_t bytes = 0;

    std::uint64_t due(std::uint64_t& last) {
        const std::uint64_t delay = latency + (jitter > 0 ? random.below(jitter + 1) : 0);
        last = std::max(last, now + delay);
        return last;
    }

    void to_host(LinkId link, TransportEvent::Kind kind, std::vector<std::uint8_t> data = {}) {
        Link& l = links[link];
        host_inbox.push_back({due(l.last_to_host), {kind, link, std::move(data)}});
    }

    void to_client(LinkId link, TransportEvent::Kind kind, std::vector<std::uint8_t> data = {}) {
        Link& l = links[link];
        l.client_inbox.push_back({due(l.last_to_client), {kind, kHostLink, std::move(data)}});
    }

    static void take_due(std::vector<Pending>& inbox, std::uint64_t now, std::vector<TransportEvent>& out) {
        // Stable: the order of each link stays as it was sent.
        auto split =
            std::stable_partition(inbox.begin(), inbox.end(), [now](const Pending& p) { return p.due <= now; });
        for (auto it = inbox.begin(); it != split; ++it) {
            out.push_back(std::move(it->event));
        }
        inbox.erase(inbox.begin(), split);
    }
};

namespace {

class HostEnd final : public Transport {
public:
    explicit HostEnd(std::shared_ptr<LoopbackNetwork::Impl> impl) : m_impl(std::move(impl)) {
        m_impl->host_open = true;
    }
    ~HostEnd() override {
        m_impl->host_open = false;
        for (LinkId id = 1; id < m_impl->links.size(); ++id) {
            disconnect(id);
        }
    }
    HostEnd(const HostEnd&) = delete;
    HostEnd& operator=(const HostEnd&) = delete;

    void poll(std::vector<TransportEvent>& events) override {
        const std::size_t first = events.size();
        LoopbackNetwork::Impl::take_due(m_impl->host_inbox, m_impl->now, events);
        // What a link sent after the host closed it is not heard.
        events.erase(std::remove_if(events.begin() + static_cast<std::ptrdiff_t>(first), events.end(),
                                    [this](const TransportEvent& e) {
                                        const auto& l = m_impl->links[e.link];
                                        return !l.to_host_open &&
                                               !(e.kind == TransportEvent::Kind::Disconnected && l.cut);
                                    }),
                     events.end());
    }
    void send(LinkId link, std::span<const std::uint8_t> message) override {
        if (link == 0 || link >= m_impl->links.size() || !m_impl->links[link].to_host_open ||
            !m_impl->links[link].to_client_open) {
            return;
        }
        ++m_impl->messages;
        m_impl->bytes += message.size();
        m_impl->to_client(link, TransportEvent::Kind::Received, {message.begin(), message.end()});
    }
    void flush() override {}
    void disconnect(LinkId link) override {
        if (link == 0 || link >= m_impl->links.size() || !m_impl->links[link].to_host_open) {
            return;
        }
        auto& l = m_impl->links[link];
        l.to_host_open = false;
        if (l.to_client_open) {
            m_impl->to_client(link, TransportEvent::Kind::Disconnected);
        }
    }
    [[nodiscard]] std::uint32_t round_trip_ms(LinkId /*link*/) const override { return 2 * m_impl->latency; }
    [[nodiscard]] std::string address(LinkId link) const override { return "loopback:" + std::to_string(link); }

private:
    std::shared_ptr<LoopbackNetwork::Impl> m_impl;
};

class ClientEnd final : public Transport {
public:
    ClientEnd(std::shared_ptr<LoopbackNetwork::Impl> impl, LinkId id) : m_impl(std::move(impl)), m_id(id) {
        if (m_impl->host_open) {
            m_impl->to_host(m_id, TransportEvent::Kind::Connected);
            // The computer hears of the connection once the host's answer is back.
            auto& l = m_impl->links[m_id];
            l.last_to_client = std::max(l.last_to_client, m_impl->now + m_impl->latency);
            m_impl->to_client(m_id, TransportEvent::Kind::Connected);
        } else {
            m_impl->to_client(m_id, TransportEvent::Kind::Disconnected);
            m_impl->links[m_id].to_host_open = false;
        }
    }
    ~ClientEnd() override { disconnect(kHostLink); }
    ClientEnd(const ClientEnd&) = delete;
    ClientEnd& operator=(const ClientEnd&) = delete;

    void poll(std::vector<TransportEvent>& events) override {
        auto& l = m_impl->links[m_id];
        const std::size_t first = events.size();
        LoopbackNetwork::Impl::take_due(l.client_inbox, m_impl->now, events);
        if (!l.to_client_open && !l.cut) {
            events.resize(first);
        }
    }
    void send(LinkId /*link*/, std::span<const std::uint8_t> message) override {
        auto& l = m_impl->links[m_id];
        if (!l.to_host_open || !l.to_client_open) {
            return;
        }
        ++m_impl->messages;
        m_impl->bytes += message.size();
        m_impl->to_host(m_id, TransportEvent::Kind::Received, {message.begin(), message.end()});
    }
    void flush() override {}
    void disconnect(LinkId /*link*/) override {
        auto& l = m_impl->links[m_id];
        if (!l.to_client_open) {
            return;
        }
        l.to_client_open = false;
        if (l.to_host_open) {
            m_impl->to_host(m_id, TransportEvent::Kind::Disconnected);
        }
    }
    [[nodiscard]] std::uint32_t round_trip_ms(LinkId /*link*/) const override { return 2 * m_impl->latency; }
    [[nodiscard]] LinkId id() const noexcept { return m_id; }

private:
    std::shared_ptr<LoopbackNetwork::Impl> m_impl;
    LinkId m_id;
};

}  // namespace

LoopbackNetwork::LoopbackNetwork(std::uint32_t latency_ms, std::uint32_t jitter_ms, std::uint64_t seed)
    : m_impl(std::make_shared<Impl>()) {
    m_impl->latency = latency_ms;
    m_impl->jitter = jitter_ms;
    m_impl->random = sim::Random(seed, 0x4C4F4F50);  // "LOOP"
}

LoopbackNetwork::~LoopbackNetwork() = default;

std::unique_ptr<Transport> LoopbackNetwork::listen() {
    return std::make_unique<HostEnd>(m_impl);
}

std::unique_ptr<Transport> LoopbackNetwork::connect() {
    m_impl->links.emplace_back();
    return std::make_unique<ClientEnd>(m_impl, static_cast<LinkId>(m_impl->links.size() - 1));
}

void LoopbackNetwork::advance(std::uint32_t ms) {
    m_impl->now += ms;
}

void LoopbackNetwork::cut(const Transport& computer) {
    const auto* end = dynamic_cast<const ClientEnd*>(&computer);
    if (end == nullptr) {
        return;
    }
    auto& l = m_impl->links[end->id()];
    if (l.cut || (!l.to_host_open && !l.to_client_open)) {
        return;
    }
    l.cut = true;
    // Everything on the way is lost; both ends hear of it at once.
    std::erase_if(m_impl->host_inbox, [&](const Impl::Pending& p) { return p.event.link == end->id(); });
    l.client_inbox.clear();
    const bool host_hears = l.to_host_open && m_impl->host_open;
    const bool client_hears = l.to_client_open;
    l.to_host_open = false;
    l.to_client_open = false;
    if (host_hears) {
        m_impl->host_inbox.push_back({m_impl->now, {TransportEvent::Kind::Disconnected, end->id(), {}}});
    }
    if (client_hears) {
        l.client_inbox.push_back({m_impl->now, {TransportEvent::Kind::Disconnected, kHostLink, {}}});
    }
}

void LoopbackNetwork::set_latency(std::uint32_t latency_ms, std::uint32_t jitter_ms) {
    m_impl->latency = latency_ms;
    m_impl->jitter = jitter_ms;
}

std::uint64_t LoopbackNetwork::messages_sent() const noexcept {
    return m_impl->messages;
}
std::uint64_t LoopbackNetwork::bytes_sent() const noexcept {
    return m_impl->bytes;
}

}  // namespace o1602::net
