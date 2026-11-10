// SPDX-License-Identifier: GPL-3.0-or-later
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <random>
#include <string>
#include <thread>

#include "o1602/net/enet_transport.hpp"
#include "o1602/net/master_service.hpp"
namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) {
    stopping = 1;
}
std::uint64_t now() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
}  // namespace
int main(int argc, char** argv) {
    using namespace o1602::net;
    std::uint16_t port = master::kPort;
    master::Limits limits;
    std::uint64_t duration = 0;
    std::string bind_address;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key == "--help") {
            std::puts(
                "open1602-master [--bind 0.0.0.0] [--port 16022] [--max-clients 64] [--max-rooms 32] [--max-members "
                "32] [--bytes-per-second 8388608] [--run-ms N]\nUDP lobby and reliable relay. No original game data "
                "required.");
            return 0;
        }
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value: %s\n", key.c_str());
            return 2;
        }
        std::string value = argv[++i];
        if (key == "--bind") {
            bind_address = value;
            continue;
        }
        std::uint64_t n = 0;
        auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
        if (ec != std::errc{} || end != value.data() + value.size() || !n) {
            std::fprintf(stderr, "invalid value\n");
            return 2;
        }
        if (key == "--port" && n <= 65535)
            port = static_cast<std::uint16_t>(n);
        else if (key == "--max-clients" && n <= 256)
            limits.clients = static_cast<std::size_t>(n);
        else if (key == "--max-rooms" && n <= master::kRooms)
            limits.rooms = static_cast<std::size_t>(n);
        else if (key == "--max-members" && n <= 64)
            limits.members = static_cast<std::size_t>(n);
        else if (key == "--bytes-per-second" && n <= (64U << 20))
            limits.bytes_per_second = n;
        else if (key == "--run-ms")
            duration = n;
        else {
            std::fprintf(stderr, "unknown/out-of-range option: %s\n", key.c_str());
            return 2;
        }
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    auto wire = enet_listen(port, limits.clients, master::kWireLimit, 8U << 20, bind_address);
    if (!wire) {
        std::fprintf(stderr, "%s\n", wire.error().message.c_str());
        return 1;
    }
    std::random_device random;
    const auto seed = (std::uint64_t(random()) << 32) | random();
    master::Service service(limits, seed ? seed : 1);
    std::printf("MASTER_READY port=%u wire=%u clients=%u rooms=%u\n", unsigned(port), unsigned(master::kVersion),
                unsigned(limits.clients), unsigned(limits.rooms));
    std::fflush(stdout);
    auto start = now(), last_stats = start;
    std::uint64_t packets = 0;
    const auto deliver = [&](std::vector<master::Delivery> out) {
        for (auto& x : out)
            (*wire)->send(x.peer, x.bytes);
    };
    while (!stopping && (!duration || now() - start < duration)) {
        std::vector<TransportEvent> events;
        (*wire)->poll(events);
        auto time = now();
        for (auto& e : events) {
            if (e.kind == TransportEvent::Kind::Connected) {
                if (!service.connect(e.link, time))
                    (*wire)->disconnect(e.link);
            } else if (e.kind == TransportEvent::Kind::Disconnected)
                deliver(service.disconnect(e.link, time));
            else {
                ++packets;
                service.ping(e.link, (*wire)->round_trip_ms(e.link));
                deliver(service.receive(e.link, e.data, time));
            }
        }
        deliver(service.tick(time));
        for (auto id : service.take_drops()) {
            deliver(service.disconnect(id, time));
            (*wire)->disconnect(id);
        }
        (*wire)->flush();
        if (time - last_stats >= 60000) {
            std::printf("MASTER_STATS clients=%u rooms=%u packets=%llu\n", unsigned(service.clients()),
                        unsigned(service.rooms()), static_cast<unsigned long long>(packets));
            std::fflush(stdout);
            last_stats = time;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("MASTER_STOP clients=%u rooms=%u packets=%llu\n", unsigned(service.clients()),
                unsigned(service.rooms()), static_cast<unsigned long long>(packets));
    return 0;
}
