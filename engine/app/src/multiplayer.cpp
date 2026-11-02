// SPDX-License-Identifier: GPL-3.0-or-later
//
// The viewer's side of a multiplayer game (ADR 0011): the session, its menu (multiplayer_menu.hpp),
// the search on the local network, and the games the host makes from its lobby.
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <random>

#include "map_viewer.hpp"

#include "o1602/assets/game_content.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/platform/window.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// How often the menu asks the network for games while it shows the search.
constexpr std::uint64_t kSearchEveryMs = 3000;
/// A game for a computer that joins packs on a worker while the game goes on, this long at most
/// before the host waits for it (SessionConfig::pack_wait_ms, TB-26).
constexpr std::uint32_t kPackWaitMs = 5'000;
/// A game started from a lobby begins for everyone at once: its first frame waits until every
/// player has it, but no longer than this for one whose game makes no progress
/// (SessionConfig::start_wait_ms).
constexpr std::uint32_t kStartWaitMs = 60'000;

/// What this computer remembers for the menu: the last name typed and the last settings a host chose.
void remembered_to_model(MultiplayerModel& model, const AppSettings& settings) {
    model.name = settings.player_name.empty() ? ui::tr("menu.default_name") : settings.player_name;
    model.settings = settings.multiplayer;
}

}  // namespace

std::uint64_t MapViewer::content_fingerprint() const {
    return assets::content_fingerprint(m_game);
}

net::HostSession* MapViewer::host_session() const noexcept {
    return dynamic_cast<net::HostSession*>(m_session.get());
}

net::ClientSession* MapViewer::client_session() const noexcept {
    return dynamic_cast<net::ClientSession*>(m_session.get());
}

void MapViewer::close_lobby() {
    if (m_hud && m_hud->multiplayer_open()) {
        m_hud->close_menu();
        if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
            apply_hud(*a);
        }
    }
}

core::Status MapViewer::start_multiplayer() {
    const ViewerOptions::Multiplayer& mp = *m_options.multiplayer;
    if (mp.role == ViewerOptions::Multiplayer::Role::Host) {
        auto transport = net::enet_listen(mp.port);
        if (!transport) {
            return transport.error();
        }
        if (m_local_player == sim::kNoPlayer) {
            return core::make_error(core::ErrorCode::Unsupported, "the game has no human player to host with");
        }
        // Hosting from the command line without --name: the menu's name, else "Nameless".
        std::string name = mp.name.empty() ? m_app_settings.player_name : mp.name;
        if (name.empty()) {
            name = ui::tr("menu.default_name");
        }
        net::SessionConfig config;
        config.pack_wait_ms = kPackWaitMs;
        config.player = m_app_settings.player_id;
        m_session = std::make_unique<net::HostSession>(std::move(transport).value(), m_state->simulation, m_buildings,
                                                       m_local_player, name, content_fingerprint(), config);
        host_session()->share_packages(shared_packages());
        m_command_sink.set_session(m_session.get());
        m_command_sink.submit(sim::PlayerRename{m_local_player, name});
        std::fprintf(stderr, "multiplayer: hosting on UDP port %u as player %u\n", static_cast<unsigned>(mp.port),
                     static_cast<unsigned>(m_local_player));
        return core::Ok{};
    }
    std::fprintf(stderr, "multiplayer: joining %s:%u\n", mp.address.c_str(), static_cast<unsigned>(mp.port));
    return join_game(mp.address, mp.port, mp.password);
}

sim::Simulation* MapViewer::load_from_host(std::span<const std::uint8_t> save) {
    std::vector<std::string> warnings;
    auto world = sim::read_game(save, m_buildings, &warnings, &m_figures);
    return start_host_game(std::move(world), warnings);
}

net::ClientSession::Finish MapViewer::prepare_host_game(std::vector<std::uint8_t> save) {
    // On a worker thread: reading and unpacking the game, the long part of a large world, while
    // the program goes on and keeps the connection. It only reads the game's tables, which change
    // only when a game starts on the game thread (make_state), not before this one is finished.
    auto warnings = std::make_shared<std::vector<std::string>>();
    auto world =
        std::make_shared<core::Result<sim::World>>(sim::read_game(save, m_buildings, warnings.get(), &m_figures));
    save = {};  // the bytes are no longer needed: freed here, not on the game thread
    // The rest on the game thread, where the game starts.
    return [this, world, warnings] { return start_host_game(std::move(*world), *warnings); };
}

sim::Simulation* MapViewer::start_host_game(core::Result<sim::World> world, const std::vector<std::string>& warnings) {
    if (!world) {
        std::fprintf(stderr, "multiplayer: the host's game cannot be loaded: %s\n", world.error().message.c_str());
        return nullptr;
    }
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    // After the host changed it is the same game: the view stays where it was.
    const render::Camera view = m_camera;
    if (const auto replaced = replace_world(std::move(world).value()); !replaced) {
        std::fprintf(stderr, "multiplayer: %s\n", replaced.error().message.c_str());
        return nullptr;
    }
    if (m_migrating) {
        m_camera = view;
    }
    report_load_problems(warnings.size());
    return &m_state->simulation;
}

std::string MapViewer::seat_names(const std::vector<sim::PlayerId>& seats) const {
    std::string names;
    for (const sim::PlayerId s : seats) {
        std::string name;
        if (m_session && s < m_session->lobby().seats.size()) {
            name = m_session->lobby().seats[s].name;
        }
        if (const sim::Player* p = m_state ? m_state->world.player(s) : nullptr; name.empty() && p != nullptr) {
            name = p->name;
        }
        names += (names.empty() ? "" : ", ") + (name.empty() ? std::to_string(s) : name);
    }
    return names;
}

void MapViewer::chat_line(const net::SessionEvent& e) {
    ChatLine line{e.name, e.text, e.seat, platform::ticks_ms(), false, {}};
    const sim::PlayerId me = m_session ? m_session->local_seat() : sim::kNoPlayer;
    if (!e.everyone) {
        line.whisper = true;
        if (e.seat == me) {
            line.to = mp_text(MpText::ChatTo, seat_names(e.to));
        } else {
            std::vector<sim::PlayerId> others = e.to;
            std::erase(others, me);
            line.to = others.empty() ? mp_text(MpText::ChatToYou) : mp_text(MpText::ChatToYouAnd, seat_names(others));
        }
    }
    add_chat_line(m_hud ? &m_hud->multiplayer_model().chat : nullptr, std::move(line));
    if (!e.missed.empty()) {
        session_line(MpText::ChatMissed, seat_names(e.missed));
    }
}

void MapViewer::update_chat() {
    if (!m_hud) {
        return;
    }
    ChatModel model;
    if (m_session && m_session->lobby().started && (host_session() != nullptr || m_host_game_loaded)) {
        const net::Lobby& lobby = m_session->lobby();
        const sim::PlayerId me = m_session->local_seat();
        model.available = true;
        model.may_write = me != sim::kNoPlayer || lobby.spectators_chat;
        for (std::size_t s = 0; s < lobby.seats.size() && me != sim::kNoPlayer; ++s) {
            const net::LobbySeat& seat = lobby.seats[s];
            if (seat.kind == net::LobbySeat::Kind::Human && s != me) {
                model.partners.push_back(ChatPartner{static_cast<sim::PlayerId>(s), seat.name, seat.banner});
            }
        }
    }
    m_hud->set_chat(std::move(model));
    for (ChatSay& say : m_hud->take_chat()) {
        if (m_session) {
            m_session->say(std::move(say.text), std::move(say.to));
        }
    }
}

void MapViewer::session_line(std::string text, std::string_view log, sim::PlayerId seat) {
    if (!log.empty()) {
        std::fprintf(stderr, "multiplayer: %.*s\n", static_cast<int>(log.size()), log.data());
    }
    add_chat_line(m_hud ? &m_hud->multiplayer_model().chat : nullptr,
                  ChatLine{{}, std::move(text), seat, platform::ticks_ms(), false, {}}, false);
}

void MapViewer::session_line(MpText id, std::string_view value, sim::PlayerId seat) {
    session_line(value.empty() ? mp_text(id) : mp_text(id, value), mp_log(id, value), seat);
}

void MapViewer::announce_take_overs() {
    // The host hands a person's slot to the computer some time after the person left (ADR 0011):
    // the same frame on every computer, so each says so when it sees the slot change hands.
    const auto& players = m_state->world.players();
    if (m_session_roles.size() == players.size()) {
        for (std::size_t i = 0; i < players.size(); ++i) {
            if (m_session_roles[i] == sim::PlayerRole::Human && players[i].role == sim::PlayerRole::Computer) {
                session_line(MpText::ComputerTookOver, players[i].name, static_cast<sim::PlayerId>(i));
            }
        }
    }
    m_session_roles.resize(players.size());
    for (std::size_t i = 0; i < players.size(); ++i) {
        m_session_roles[i] = players[i].role;
    }
}

void MapViewer::apply_session_events() {
    const auto who = [](const net::SessionEvent& e) { return e.name.empty() ? std::to_string(e.seat) : e.name; };
    std::optional<std::string> ended;
    bool host_left = false;
    struct PackageOffer {
        std::vector<assets::ContentPackage> packages;
        std::uint64_t bytes = 0;
    };
    std::optional<PackageOffer> offer;
    for (const net::SessionEvent& e : m_session->take_events()) {
        switch (e.kind) {
            case net::SessionEvent::Kind::Welcomed:
                std::fprintf(stderr, "multiplayer: in as %s\n",
                             e.seat == sim::kNoPlayer ? "spectator" : ("player " + std::to_string(e.seat)).c_str());
                if (const net::ClientSession* client = client_session();
                    client != nullptr && e.seat != sim::kNoPlayer) {
                    m_rejoin = Rejoin{m_host_address, m_host_port, e.seat, client->token()};
                }
                if (m_hud && m_hud->multiplayer_open() &&
                    m_hud->multiplayer()->screen() == MultiplayerMenu::Screen::Connecting) {
                    m_hud->multiplayer()->show_lobby();
                }
                break;
            case net::SessionEvent::Kind::Loaded:
                m_host_game_loaded = true;
                if (m_migrating) {
                    m_migrating = false;
                    session_line(MpText::HostMoved, m_successor_name);
                    m_events.notify(mp_text(MpText::HostMoved, m_successor_name), {}, platform::ticks_ms());
                }
                std::fprintf(stderr, "multiplayer: game from the host loaded at frame %llu\n",
                             static_cast<unsigned long long>(e.frame));
                if (!m_session->starting()) {
                    close_lobby();  // else the lobby shows who is still loading until all begin (Started)
                }
                break;
            case net::SessionEvent::Kind::Started:
                std::fprintf(stderr, "multiplayer: everyone has the game, it begins\n");
                if (m_host_game_loaded || host_session() != nullptr) {
                    close_lobby();
                }
                break;
            case net::SessionEvent::Kind::Joined:
                session_line(MpText::PlayerJoined, who(e), e.seat);
                break;
            case net::SessionEvent::Kind::Left:
                session_line(MpText::PlayerLeft, who(e), e.seat);
                break;
            case net::SessionEvent::Kind::Lagging:
                session_line(MpText::Lagging, who(e), e.seat);
                break;
            case net::SessionEvent::Kind::Desync:
                std::fprintf(stderr, "multiplayer: player %u diverged after frame %llu\n",
                             static_cast<unsigned>(e.seat), static_cast<unsigned long long>(e.frame));
                session_line(MpText::Desync, who(e), e.seat);
                break;
            case net::SessionEvent::Kind::Ended:
                std::fprintf(stderr, "multiplayer: %s\n",
                             e.reason == net::EndReason::Refused ? net::describe(e.refused) : net::describe(e.reason));
                ended = e.reason == net::EndReason::Refused ? mp_text(e.refused, e.packages) : mp_text(e.reason);
                host_left = e.reason == net::EndReason::HostLeft;
                if (e.reason == net::EndReason::Refused && e.refused == net::RefuseReason::Packages &&
                    e.share_bytes != 0 && !m_fetching_packages) {
                    offer = PackageOffer{e.packages, e.share_bytes};
                }
                break;
            case net::SessionEvent::Kind::LobbyChanged:
                break;  // the menu shows the lobby as it is
            case net::SessionEvent::Kind::Chat:
                chat_line(e);
                break;
            case net::SessionEvent::Kind::Dropped:
                session_line(MpText::Dropped, std::to_string(e.count), e.seat);
                break;
            case net::SessionEvent::Kind::Reconnecting:
                session_line(MpText::Reconnecting);
                break;
            case net::SessionEvent::Kind::SpeedRefused:
                session_line(MpText::SpeedRefused);
                break;
            case net::SessionEvent::Kind::PackageKept:
                session_line(MpText::PackageKept, core::utf8_to_cp1252(assets::package_label(e.packages.at(0))));
                break;
        }
    }
    if (ended && offer && m_hud && m_hud->multiplayer_open()) {
        // The host would send the packages it refused this computer for: the person decides.
        offer_packages(offer->packages, offer->bytes, *ended);
        return;
    }
    // A session that ends before its game ran: back to the search with the reason. One that ends
    // in the game keeps the game standing as it is (the title says so).
    // When the host leaves the running game, it moves on to the successor; following one that
    // did not take this computer in, the game is saved as it stood.
    if (ended && m_hud && m_hud->multiplayer_open()) {
        leave_session(*ended);
    } else if (ended && m_migrating) {
        m_migrating = false;
        save_after_host_left();
    } else if (host_left && m_host_game_loaded && !migrate()) {
        save_after_host_left();
    }
}

bool MapViewer::migrate() {
    net::ClientSession* client = client_session();
    if (client == nullptr || !client->can_migrate()) {
        return false;
    }
    std::string successor;  // the value of the text, if it has one
    MpText text = MpText::HostMovedYou;
    if (client->hosts_next()) {
        auto transport = net::enet_listen(m_host_port);
        if (!transport) {
            std::fprintf(stderr, "multiplayer: cannot host the game on: %s\n", transport.error().message.c_str());
            return false;
        }
        std::fprintf(stderr, "multiplayer: the host left; hosting the game on UDP port %u\n",
                     static_cast<unsigned>(m_host_port));
        m_session = std::make_unique<net::HostSession>(std::move(transport).value(), *client, m_buildings);
        host_session()->share_packages(shared_packages());
    } else {
        // A successor the host saw on its own computer is on the computer this one joined.
        std::string address = client->successor_address();
        if (address.starts_with("127.")) {
            address = m_host_address;
        }
        m_host_address = address;
        const std::uint16_t port = m_host_port;
        std::fprintf(stderr, "multiplayer: the host left; following %s:%u\n", address.c_str(),
                     static_cast<unsigned>(port));
        m_successor_name = client->successor_name();
        m_session = std::make_unique<net::ClientSession>(*client, [address, port]() -> std::unique_ptr<net::Transport> {
            auto transport = net::enet_connect(address, port);
            return transport ? std::move(transport).value() : nullptr;
        });
        m_migrating = true;
        text = MpText::HostMoving;
        successor = m_successor_name;
    }
    m_command_sink.set_session(m_session.get());
    session_line(text, successor);
    m_events.notify(successor.empty() ? mp_text(text) : mp_text(text, successor), {}, platform::ticks_ms());
    return true;
}

void MapViewer::save_after_host_left() {
    const std::filesystem::path path = save_folder() / ("hostleft" + std::string(sim::kSaveExtension));
    // The game goes on at this computer alone: it loads with this player's seat (the only person
    // left) and the computer plays the seats of the others. The running game stays as it is.
    std::vector<sim::Player*> others;
    for (std::size_t i = 0; i < m_state->world.players().size() && i < sim::kNoPlayer; ++i) {
        const auto id = static_cast<sim::PlayerId>(i);
        sim::Player* p = m_state->world.player(id);
        if (p != nullptr && p->role == sim::PlayerRole::Human && id != m_local_player) {
            p->role = sim::PlayerRole::Computer;
            others.push_back(p);
        }
    }
    const auto status = save_game(path);
    for (sim::Player* p : others) {
        p->role = sim::PlayerRole::Human;
    }
    const std::string file = utf8_to_cp1252(core::path_to_utf8(path.filename()));
    // error_text() logs what went wrong in English.
    const std::string text = status ? mp_text(MpText::HostLeftSaved, file)
                                    : ui::tr("notice.save_failed", {ui::error_text(status.error(), "save")});
    session_line(text, status ? mp_log(MpText::HostLeftSaved, file) : std::string("notice.save_failed"));
    m_events.notify(text, {}, platform::ticks_ms());
}

// --- The menu ---

void MapViewer::open_multiplayer_menu() {
    if (!m_hud || m_hud->multiplayer() == nullptr) {
        return;
    }
    MultiplayerModel& model = m_hud->multiplayer_model();
    if (auto texts = assets::TextTable::load(m_game)) {
        model.level_names = texts->section("ENDLESSKIND");
        std::array<std::string, 4> headings;
        for (std::size_t g = 0; g < headings.size(); ++g) {
            const auto& lines = texts->section("GAME");
            headings[g] = 85 + g < lines.size() ? lines[85 + g] : std::string{};
        }
        if (const auto folder = m_game.resolve("Szenes")) {
            model.scenarios = scenario_list(*folder, headings, &m_game);
        }
    }
    // Before the menu first draws and takes them: the click that opened it ends the frame's menu
    // work, so apply_multiplayer_menu() would only fill them after that first drawing, and the
    // name would be empty after each start.
    remembered_to_model(model, m_app_settings);
    m_hud->open_multiplayer();
    m_lan_search_ms = 0;  // search at once
}

void MapViewer::apply_multiplayer_menu() {
    MultiplayerMenu* menu = m_hud->multiplayer();
    if (menu == nullptr) {
        return;
    }
    const std::uint64_t now = platform::ticks_ms();
    const auto delta = static_cast<std::uint32_t>(std::min<std::uint64_t>(now - m_lan_update_ms, 1000));
    m_lan_update_ms = now;

    // The search runs while the search screen shows.
    if (menu->screen() == MultiplayerMenu::Screen::Browse) {
        if (!m_lan_browser) {
            if (auto browser = net::LanBrowser::open()) {
                m_lan_browser = std::move(browser).value();
            } else {
                std::fprintf(stderr, "multiplayer: no search on the local network: %s\n",
                             browser.error().message.c_str());
                m_lan_search_ms = now;  // try again with the next search
            }
        }
        if (m_lan_browser) {
            if (m_lan_search_ms == 0 || now - m_lan_search_ms >= kSearchEveryMs) {
                m_lan_browser->search(m_app_settings.remembered_hosts);
                m_lan_search_ms = now;
            }
            m_lan_browser->update(delta);
        }
    } else {
        m_lan_browser.reset();
        m_lan_search_ms = 0;
    }

    MultiplayerModel& model = m_hud->multiplayer_model();
    remembered_to_model(model, m_app_settings);
    model.found = m_lan_browser ? m_lan_browser->games() : std::vector<net::FoundGame>{};
    model.savegames.clear();
    for (std::size_t slot = 0; slot < m_slots.size(); ++slot) {
        if (!m_slots.names[slot].empty() && std::filesystem::exists(slot_file(save_folder(), slot))) {
            model.savegames.emplace_back(slot, m_slots.names[slot]);
        }
    }
    const auto session_to_model = [&] {
        model.session = m_session.get();
        const net::HostSession* host = host_session();
        model.hosting = host != nullptr;
        model.everyone_ready = host != nullptr && host->everyone_ready();
        model.port = m_hosted ? m_hosted->port : net::kDefaultPort;
        join_to_model();
    };
    session_to_model();
    for (const MultiplayerEvent& e : m_hud->take_multiplayer_events()) {
        handle_multiplayer(e);
        if (!m_hud->multiplayer_open()) {
            break;  // the rest belongs to the menu that is gone
        }
    }
    session_to_model();  // the events may have begun or ended a session
}

void MapViewer::join_to_model() {
    if (!m_hud) {
        return;
    }
    MultiplayerModel& model = m_hud->multiplayer_model();
    const net::ClientSession* client = client_session();
    model.join = client != nullptr ? client->join_stage() : net::ClientSession::JoinStage::None;
    model.join_received = client != nullptr ? client->received_bytes() : 0;
    model.join_total = client != nullptr ? client->total_bytes() : 0;
}

void MapViewer::handle_multiplayer(const MultiplayerEvent& e) {
    MultiplayerMenu* menu = m_hud->multiplayer();
    const auto keep_name = [&] {
        const std::string& name = menu->name();
        if (!name.empty() && name != m_app_settings.player_name) {
            m_app_settings.player_name = name;
            save_settings();
        }
    };
    net::HostSession* host = host_session();
    net::ClientSession* client = client_session();
    switch (e.kind) {
        case MultiplayerEvent::Kind::Close:
            m_lan_browser.reset();
            m_hud->close_multiplayer();
            break;
        case MultiplayerEvent::Kind::Search:
            m_lan_search_ms = 0;
            break;
        case MultiplayerEvent::Kind::Name:
            keep_name();
            break;
        case MultiplayerEvent::Kind::Join:
            keep_name();
            if (auto status = join_game(e.address, e.port, e.password); !status) {
                menu->open(mp_text(MpText::CannotConnect, status.error().message));
            }
            break;
        case MultiplayerEvent::Kind::Host:
            keep_name();
            if (e.setup.kind != HostSetup::Kind::Savegame && !(e.setup.settings == m_app_settings.multiplayer)) {
                m_app_settings.multiplayer = e.setup.settings;  // offered first next time
                save_settings();
            }
            if (auto status = host_lobby(e.setup); !status) {
                leave_session(mp_text(MpText::CannotHost, status.error().message));
            }
            break;
        case MultiplayerEvent::Kind::Settings:
            if (host != nullptr && !host->started() && m_hosted && m_hosted->kind != HostSetup::Kind::Savegame) {
                m_hosted->settings = e.setup.settings;
                const net::Lobby& lobby = host->lobby();
                host->set_lobby_info(lobby.title, lobby.game,
                                     lobby_setup(m_hosted->game_settings(), m_hosted->scope(), m_hosted->level));
                if (!(e.setup.settings == m_app_settings.multiplayer)) {
                    m_app_settings.multiplayer = e.setup.settings;
                    save_settings();
                }
            }
            break;
        case MultiplayerEvent::Kind::Leave:
            leave_session();
            break;
        case MultiplayerEvent::Kind::Start:
            if (auto status = start_hosted_game(); !status) {
                session_line(MpText::StartFailed, status.error().message);
            }
            break;
        case MultiplayerEvent::Kind::StartNow:
            if (host != nullptr) {
                host->start_now();
            }
            break;
        case MultiplayerEvent::Kind::TakeSeat:
            if (host != nullptr) {
                (void)host->take_seat(e.seat);
            } else if (client != nullptr) {
                client->take_seat(e.seat);
            }
            break;
        case MultiplayerEvent::Kind::SetSeat:
            if (host != nullptr && (e.seat_kind != net::LobbySeat::Kind::Closed || may_block_seats(host->lobby()))) {
                host->set_seat(e.seat, e.seat_kind);
            }
            break;
        case MultiplayerEvent::Kind::Kick:
            if (host != nullptr) {
                host->kick(e.seat);
            }
            break;
        case MultiplayerEvent::Kind::KickMember:
            if (host != nullptr) {
                host->kick_member(e.member, e.seat, e.text);
            }
            break;
        case MultiplayerEvent::Kind::SetBanner:
            if (host != nullptr) {
                (void)host->set_banner(e.seat, e.banner);
            } else if (client != nullptr) {
                client->set_banner(e.banner);
            }
            break;
        case MultiplayerEvent::Kind::Ready:
            if (client != nullptr) {
                client->set_ready(e.ready);
            }
            break;
        case MultiplayerEvent::Kind::Say:
            if (m_session) {
                m_session->say(e.text);
            }
            break;
    }
}

void MapViewer::update_lan_beacon() {
    if (m_lan_beacon && m_hosted && m_session) {
        m_lan_beacon->set_info(net::game_info(m_session->lobby(), m_session->roster(), m_hosted->port));
        m_lan_beacon->update();
    }
}

void MapViewer::end_session() {
    m_session.reset();
    m_host_game_loaded = false;
    m_migrating = false;
    m_command_sink.set_session(nullptr);
    m_lan_beacon.reset();
    m_hosted.reset();
    m_hosted_world.reset();
    m_lan_search_ms = 0;
    if (m_hud) {
        m_hud->multiplayer_model().session = nullptr;
        m_hud->multiplayer_model().hosting = false;
        m_hud->multiplayer_model().chat.clear();
    }
}

void MapViewer::leave_session(std::string notice) {
    end_session();
    if (m_hud) {
        m_hud->open_multiplayer(std::move(notice));
    }
}

// --- Hosting ---

core::Status MapViewer::host_lobby(const HostSetup& setup) {
    if (m_session && m_session->ended()) {
        end_session();  // one that ended in its game stood only to show the game
    }
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "a multiplayer game runs already");
    }
    net::Lobby lobby;
    lobby.title = setup.title;
    lobby.game = setup.file_name;
    std::optional<sim::World> world;
    if (setup.kind == HostSetup::Kind::Endless) {
        // A slot per player, the world is made at the start.
        lobby.seats = endless_seats(setup.players);
        lobby.banners = player_banners();
    } else {
        const std::filesystem::path path =
            setup.kind == HostSetup::Kind::Scenario ? setup.file : slot_file(save_folder(), setup.slot);
        auto loaded = read_world(path);
        if (!loaded) {
            return loaded.error();
        }
        // The slots of the humans and computers of the game; human ones are open for whoever comes.
        const std::vector<sim::Player>& players = loaded->players();
        std::size_t count = 0;
        const auto playable = [&](std::size_t s) {
            const sim::Player& p = players[s];
            return p.status != sim::PlayerStatus::Absent &&
                   (p.role == sim::PlayerRole::Human || p.role == sim::PlayerRole::Computer);
        };
        for (std::size_t s = 0; s < players.size() && s < net::kMaxLobbySeats; ++s) {
            count = playable(s) ? s + 1 : count;
        }
        for (std::size_t s = 0; s < count; ++s) {
            net::LobbySeat seat;
            // A computer player that a person played before (who left, or a game played on alone)
            // is open for that person again.
            const bool computer = players[s].role == sim::PlayerRole::Computer && players[s].owner == 0;
            seat.kind = !playable(s) ? net::LobbySeat::Kind::Reserved
                        : computer   ? net::LobbySeat::Kind::Computer
                                     : net::LobbySeat::Kind::Open;
            seat.banner = players[s].banner;
            seat.owner = players[s].owner;
            lobby.seats.push_back(std::move(seat));
        }
        if (lobby.seats.empty()) {
            return core::make_error(core::ErrorCode::Unsupported, "the game has no player slots");
        }
        // A scenario's players may change colours before it begins; a savegame's keep theirs.
        if (setup.kind == HostSetup::Kind::Scenario) {
            lobby.banners = player_banners();
        }
        world = std::move(loaded).value();
    }
    // The settings for everyone to see; a savegame keeps those it was started with.
    if (setup.kind == HostSetup::Kind::Savegame) {
        EndlessSetup saved;
        saved.rules = world->saved_rules().value_or(m_options.rules);
        lobby.setup = lobby_setup(saved, SetupScope::Savegame);
    } else {
        lobby.setup = lobby_setup(setup.game_settings(), setup.scope(), setup.level);
    }
    // The host plays the first open slot (the first slot if all are computers), unless it only hosts.
    // A savegame's host plays the slot it played before, if the game knows it.
    sim::PlayerId seat = sim::kNoPlayer;
    if (!setup.dedicated) {
        const std::uint64_t own = net::player_verifier(m_app_settings.player_id);
        auto open = std::find_if(lobby.seats.begin(), lobby.seats.end(), [own](const net::LobbySeat& s) {
            return s.kind == net::LobbySeat::Kind::Open && own != 0 && s.owner == own;
        });
        if (open == lobby.seats.end()) {
            open = std::find_if(lobby.seats.begin(), lobby.seats.end(), [](const net::LobbySeat& s) {
                return s.kind == net::LobbySeat::Kind::Open && s.owner == 0;
            });
        }
        if (open == lobby.seats.end()) {
            open = std::find_if(lobby.seats.begin(), lobby.seats.end(),
                                [](const net::LobbySeat& s) { return s.kind == net::LobbySeat::Kind::Open; });
        }
        seat = static_cast<sim::PlayerId>(open != lobby.seats.end() ? open - lobby.seats.begin() : 0);
    }
    net::SessionConfig config;
    config.pack_wait_ms = kPackWaitMs;
    config.start_wait_ms = kStartWaitMs;
    config.password = setup.password;
    config.player = m_app_settings.player_id;
    config.join_running = setup.join_running;
    config.spectators = setup.spectators;
    config.spectators_chat = setup.spectators_chat;
    config.computer_takes_over = setup.computer_takes_over;
    config.host_controls_speed = setup.host_controls_speed;
    auto transport = net::enet_listen(setup.port);
    if (!transport) {
        return transport.error();
    }
    const std::string name = m_hud ? m_hud->multiplayer()->name() : m_app_settings.player_name;
    m_session = std::make_unique<net::HostSession>(std::move(transport).value(), std::move(lobby), seat, name,
                                                   content_fingerprint(), config);
    // A computer needs the packages of the game to join: a savegame's own, else those installed here.
    host_session()->set_packages(world && world->packages() ? *world->packages() : assets::content_of(m_packages));
    host_session()->share_packages(shared_packages());
    m_command_sink.set_session(m_session.get());
    m_hosted = setup;
    m_hosted_world = std::move(world);
    if (setup.lan) {
        if (auto beacon = net::LanBeacon::open()) {
            m_lan_beacon = std::move(beacon).value();
        } else {
            // Another host on this computer has the port; the game can still be joined by address.
            std::fprintf(stderr, "multiplayer: not shown on the local network: %s\n", beacon.error().message.c_str());
        }
    }
    std::fprintf(stderr, "multiplayer: lobby on UDP port %u\n", static_cast<unsigned>(setup.port));
    if (m_hud) {
        m_hud->multiplayer_model().chat.clear();
        m_hud->multiplayer()->show_lobby();
    }
    return core::Ok{};
}

core::Status MapViewer::start_hosted_game() {
    net::HostSession* host = host_session();
    if (host == nullptr || host->started() || !m_hosted) {
        return core::make_error(core::ErrorCode::Unsupported, "no lobby to start");
    }
    const HostSetup& setup = *m_hosted;
    const net::Lobby lobby = host->lobby();
    const sim::PlayerId own = host->local_seat();
    std::optional<sim::World> world;
    // The settings the host chose (ADR 0008); they reach the others with the game.
    const EndlessSetup settings = setup.game_settings();
    const sim::Rules* rules = setup.kind == HostSetup::Kind::Savegame ? nullptr : &settings.rules;
    if (setup.kind == HostSetup::Kind::Endless) {
        auto texts = assets::TextTable::load(m_game);
        if (!texts) {
            return texts.error();
        }
        // The world of the number the host chose: the same number, the same islands.
        sim::NewGameOptions o;
        o.difficulty = std::min<std::uint8_t>(setup.level, sim::NewGameOptions::kDifficulties - 1);
        o.layout = sim::layout_for_seed(setup.seed);
        o.colour = own < sim::kOriginalPlayers ? own : 0;
        o.name = own < lobby.seats.size() ? lobby.seats[own].name : std::string{};
        o.world = m_options.world;
        o.world.seed = setup.seed;
        o.world.guarantee_resources = settings.rules.guarantee_resources;
        o.multiplayer = true;  // the .szm level: every player starts with the small trading cog
        if (settings.own_world) {
            // A world of its own: chosen, or more players than the original's maps have starts
            // for (multiplayer_setup); by default as much sea and land per player as the
            // original's 500 x 350 for four, all ships at one place as the original.
            o.custom = settings.world_settings();
        }
        std::random_device device;
        const std::time_t now = std::time(nullptr);
        if (const std::tm* t = std::localtime(&now)) {
            o.game_id = sim::new_game_id(static_cast<std::uint32_t>(t->tm_year + 1900),
                                         static_cast<std::uint32_t>(t->tm_mon + 1),
                                         static_cast<std::uint32_t>(t->tm_mday), static_cast<std::uint32_t>(t->tm_hour),
                                         static_cast<std::uint32_t>(t->tm_min), device());
        }
        std::vector<std::string> warnings;
        auto made = sim::start_new_game(m_game, m_buildings, *texts, o, &warnings);
        if (!made) {
            return made.error();
        }
        give_mod_content(*made);
        for (const auto& w : warnings) {
            std::fprintf(stderr, "warning: %s\n", w.c_str());
        }
        world = std::move(made).value();
        name_more_players(*world);
    } else if (m_hosted_world) {
        world = std::move(*m_hosted_world);
        m_hosted_world.reset();
    } else {
        return core::make_error(core::ErrorCode::NotFound, "the game of the lobby is gone");
    }
    // The slots as the lobby has them. Nobody plays an open slot yet: the computer plays it, and
    // when people may come into the running game, the host hands it to whoever comes.
    std::vector<std::string> colour_names;  // PLAYCOL: the computers of an endless game are named by colour
    if (auto texts = assets::TextTable::load(m_game); texts && setup.kind == HostSetup::Kind::Endless) {
        colour_names = texts->section("PLAYCOL");
    }
    for (std::size_t s = 0; s < lobby.seats.size(); ++s) {
        sim::Player* p = world->player(static_cast<sim::PlayerId>(s));
        if (p == nullptr) {
            continue;
        }
        // The factions' slots stay as the world has them, whatever the lobby says: a free trader
        // or pirates made a computer player would be out of the game at the first check.
        if (p->role != sim::PlayerRole::Human && p->role != sim::PlayerRole::Computer) {
            continue;
        }
        const net::LobbySeat& seat = lobby.seats[s];
        if (!lobby.banners.empty()) {
            p->banner = seat.banner;  // the colour chosen in the lobby (all different, net::HostSession)
        }
        net::LobbySeat::Kind kind = seat.kind;
        if (kind == net::LobbySeat::Kind::Reserved) {
            kind = net::LobbySeat::Kind::Closed;  // one beyond the number of players
        }
        switch (kind) {
            case net::LobbySeat::Kind::Human:
                p->role = sim::PlayerRole::Human;
                p->name = seat.name;
                break;
            case net::LobbySeat::Kind::Open:
            case net::LobbySeat::Kind::Computer:
                p->role = sim::PlayerRole::Computer;
                if (setup.kind == HostSetup::Kind::Endless) {
                    p->name = colour_player_name(p->banner, colour_names);
                }
                break;
            case net::LobbySeat::Kind::Closed: {
                // Nobody plays it: no ship of it sails (a slot with no city and no ship is absent).
                p->status = sim::PlayerStatus::Absent;
                std::erase_if(world->ships(), [s](const sim::Ship& ship) { return ship.owner == s; });
                break;
            }
            case net::LobbySeat::Kind::Reserved:
                break;  // made closed above
        }
    }
    if (auto replaced = replace_world(std::move(*world), {}, rules); !replaced) {
        return replaced;
    }
    host->start(m_state->simulation, m_buildings);
    if (own != sim::kNoPlayer && own < lobby.seats.size()) {
        m_command_sink.submit(sim::PlayerRename{own, lobby.seats[own].name});
    }
    std::fprintf(stderr, "multiplayer: the game starts\n");
    if (!host->starting()) {
        close_lobby();  // else once every player has the game (Started)
    }
    return core::Ok{};
}

// --- Joining ---

core::Status MapViewer::join_game(const std::string& address, std::uint16_t port, const std::string& password,
                                  bool fetch_packages) {
    if (m_session && m_session->ended()) {
        end_session();  // one that ended in its game stood only to show the game
    }
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "a multiplayer game runs already");
    }
    auto transport = net::enet_connect(address, port);
    if (!transport) {
        return transport.error();
    }
    m_host_address = address;
    m_host_port = port;
    // The host joined by address comes first in the search next time.
    const std::string remembered = port == net::kDefaultPort ? address : address + ":" + std::to_string(port);
    std::vector<std::string>& hosts = m_app_settings.remembered_hosts;
    std::erase(hosts, remembered);
    hosts.insert(hosts.begin(), remembered);
    hosts.resize(std::min(hosts.size(), AppSettings::kMaxHosts));
    save_settings();

    net::Hello hello;
    hello.content = content_fingerprint();
    hello.packages = assets::content_of(known_packages());  // the host asks for those of its game
    hello.fetch_packages = fetch_packages;
    m_fetching_packages = fetch_packages;
    m_join_password = password;
    // The name typed in the menu; joining from the command line, the one given there.
    std::string name = m_hud && m_hud->multiplayer() != nullptr ? m_hud->multiplayer()->name() : std::string{};
    if (name.empty()) {
        name = m_options.multiplayer ? m_options.multiplayer->name : m_app_settings.player_name;
    }
    hello.name = name.empty() ? ui::tr("menu.default_name") : name;
    hello.password = password;
    hello.player = m_app_settings.player_id;  // the host knows the slot this person played before
    if (m_rejoin && m_rejoin->address == address && m_rejoin->port == port) {
        // Back to the host this computer played at: its own slot, not the next free one (MS-4).
        hello.seat = m_rejoin->seat;
        hello.token = m_rejoin->token;
    }
    m_local_player = sim::kNoPlayer;  // until the host sends the game
    net::SessionConfig config;
    config.pack_wait_ms = kPackWaitMs;  // for when this computer hosts next
    config.player = m_app_settings.player_id;
    auto client = std::make_unique<net::ClientSession>(
        std::move(transport).value(), hello,
        [this](std::span<const std::uint8_t> save) { return load_from_host(save); }, config);
    client->load_in_background([this](std::vector<std::uint8_t> save) { return prepare_host_game(std::move(save)); });
    // A broken connection: back to this host first; only if it cannot be reached, on to the
    // successor (MS-2).
    client->reconnect_with([address, port]() -> std::unique_ptr<net::Transport> {
        auto again = net::enet_connect(address, port);
        return again ? std::move(again).value() : nullptr;
    });
    // Packages the host sends, which the person agreed to: into the cache, before the game that uses
    // them is loaded.
    client->keep_packages_with(
        [this](const assets::ContentPackage& package, std::span<const assets::PackageFile> files) {
            auto kept = assets::store_package(data_folder() / "mods-cache", package, files);
            if (!kept) {
                std::fprintf(stderr, "mods: %s\n", kept.error().message.c_str());
                return false;
            }
            std::fprintf(stderr, "mods: received %s\n", assets::package_label(package).c_str());
            std::erase_if(m_cached_packages, [&](const assets::ModPackage& p) { return p.content() == package; });
            m_cached_packages.push_back(std::move(kept).value());
            return true;
        });
    m_session = std::move(client);
    m_command_sink.set_session(m_session.get());
    if (m_hud && m_hud->multiplayer() != nullptr) {
        m_hud->multiplayer_model().chat.clear();
        m_hud->multiplayer()->show_connecting(address + ":" + std::to_string(port));
    }
    return core::Ok{};
}

// --- Packages a host sends ---

void MapViewer::offer_packages(std::span<const assets::ContentPackage> packages, std::uint64_t bytes,
                               std::string refusal) {
    end_session();
    m_fetch_offer = FetchOffer{m_host_address, m_host_port, m_join_password, std::move(refusal)};
    m_hud->ask(mp_package_offer(packages, bytes));
}

void MapViewer::answer_package_offer(bool yes) {
    const FetchOffer offer = std::move(*m_fetch_offer);
    m_fetch_offer.reset();
    if (!yes) {
        leave_session(offer.refusal);
    } else if (auto status = join_game(offer.address, offer.port, offer.password, true); !status) {
        leave_session(mp_text(MpText::CannotConnect, status.error().message));
    }
}

std::vector<net::SharedPackage> MapViewer::shared_packages() const {
    std::vector<net::SharedPackage> out;
    for (const assets::ModPackage& p : known_packages()) {
        out.push_back(net::SharedPackage{p.content(), p.folder, assets::package_size(p.folder)});
    }
    return out;
}

}  // namespace o1602::app
