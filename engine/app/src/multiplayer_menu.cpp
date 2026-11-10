// SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_menu.hpp"

#include <algorithm>
#include <charconv>
#include <random>

#include "o1602/ui/list_scroll.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

// The search
constexpr Id kName = 1;
constexpr Id kAddress = 2;
constexpr Id kJoinPassword = 3;
constexpr Id kConnect = 4;
constexpr Id kCreate = 5;
constexpr Id kBack = 6;
constexpr Id kRefresh = 7;
constexpr Id kJoin = 8;
constexpr Id kBrowseUp = 9;
constexpr Id kBrowseDown = 10;
constexpr Id kBrowseRow = 100;  ///< + row
constexpr std::size_t kBrowseRows = 14;
// Creating
constexpr Id kKindEndless = 20;
constexpr Id kKindScenario = 21;
constexpr Id kKindSavegame = 22;
constexpr Id kLevel = 30;  ///< + level
constexpr Id kSeed = 40;
constexpr Id kRoll = 41;
constexpr Id kTitle = 42;
constexpr Id kPassword = 43;
constexpr Id kLan = 44;
constexpr Id kJoinRunning = 45;
constexpr Id kSpectators = 46;
constexpr Id kDedicated = 47;
constexpr Id kPort = 48;
constexpr Id kOpenLobby = 49;
constexpr Id kCreateBack = 50;
constexpr Id kListUp = 51;
constexpr Id kListDown = 52;
constexpr Id kFewerPlayers = 53;
constexpr Id kMorePlayers = 54;
constexpr Id kTakeOver = 55;
constexpr Id kHostPauses = 56;
constexpr Id kSpectatorChat = 57;
constexpr Id kListRow = 200;  ///< + row
constexpr std::size_t kListRows = 11;
// The lobby
constexpr Id kWatcherKick = 700;  ///< + row of the spectators the host can send away
constexpr std::size_t kWatcherRows = 5;
constexpr Id kBannerChoice = 900;  ///< + button of the colours on offer
constexpr std::size_t kMaxBannerChoices = 100;
/// Every slot a lobby can have (LC-1): each range below leaves room for all of them.
constexpr std::size_t kMaxSeats = net::kMaxLobbySeats;
constexpr Id kSeatRange = 300;
static_assert(kMaxSeats <= static_cast<std::size_t>(kSeatRange));
static_assert(kBannerChoice + static_cast<Id>(kMaxBannerChoices) <= 1000);
constexpr Id kSeatKind = 1000;                      ///< + slot
constexpr Id kSeatTake = kSeatKind + kSeatRange;    ///< + slot
constexpr Id kSeatKick = kSeatTake + kSeatRange;    ///< + slot
constexpr Id kSeatBanner = kSeatKick + kSeatRange;  ///< + slot: choose its banner
constexpr std::size_t kBannerColumns = 6;
constexpr Id kChat = 60;
constexpr Id kLeave = 61;
constexpr Id kReady = 62;
constexpr Id kStart = 63;
constexpr Id kWatch = 64;
constexpr std::size_t kChatLines = 12;
constexpr Rect kChatBox{48, 0, 604, 28};  ///< a row of the lobby's chat (x and width)
// Connecting
constexpr Id kCancel = 70;
// The game's settings, on the create screen and in the lobby
constexpr Id kSettings = 80;
constexpr Id kPublic = 86;
constexpr Id kNetwork = 81, kInternet = 82, kAdhoc = 83, kRadioHost = 84, kRadioSearch = 85;

constexpr std::size_t kMaxAddress = 64;
constexpr std::size_t kMaxNumber = 10;

// The frame of every screen, its heading and the buttons at the bottom.
constexpr Rect kScreen{16, 16, 992, 736};
constexpr Rect kHeading{40, 28, 560, 40};
constexpr std::int32_t kBottomY = 690;
constexpr std::int32_t kRowH = 32;

std::uint32_t random_seed() {
    std::random_device device;
    return device() % 100000;
}

std::optional<std::uint32_t> number(std::string_view text) {
    std::uint32_t v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    return ec == std::errc() && end == text.data() + text.size() && !text.empty() ? std::optional(v) : std::nullopt;
}

// The lobby's setup: a first line with what is played, then the settings as settings.cfg has them.
constexpr std::string_view kSetupScopeKey = "game = ";
constexpr std::array<std::string_view, 4> kSetupScopes = {"endless", "multiplayer", "scenario", "savegame"};

/// Open1602's defaults, the original's rules, or something else: in the words of the start dialog.
std::string rules_summary(sim::Rules rules) {
    rules.victory = {};  // the game's goal, not its rules: the start dialog shows it on a page of its own
    if (rules == sim::Rules::open1602()) {
        return ui::tr("setup.defaults");
    }
    if (rules == sim::Rules::original()) {
        return ui::tr("setup.original");
    }
    return ui::tr("setup.custom");
}

}  // namespace

SetupScope HostSetup::scope() const noexcept {
    switch (kind) {
        case Kind::Scenario:
            return SetupScope::Scenario;
        case Kind::Savegame:
            return SetupScope::Savegame;
        case Kind::Endless:
            break;
    }
    return SetupScope::MultiplayerEndless;
}

EndlessSetup HostSetup::game_settings() const {
    EndlessSetup s = multiplayer_setup(settings, players);
    if (!victory_offered(scope())) {
        s.rules.victory = {};
    }
    return s;
}

std::vector<std::uint8_t> lobby_setup(const EndlessSetup& settings, SetupScope scope) {
    std::string text(kSetupScopeKey);
    text += kSetupScopes[static_cast<std::size_t>(scope)];
    text += '\n';
    text += write_setup(settings, "");
    return {text.begin(), text.end()};
}

std::optional<std::pair<EndlessSetup, SetupScope>> read_lobby_setup(std::span<const std::uint8_t> bytes) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!text.starts_with(kSetupScopeKey)) {
        return std::nullopt;
    }
    const std::size_t end = text.find('\n');
    const std::string_view name = text.substr(
        kSetupScopeKey.size(), end == std::string_view::npos ? std::string_view::npos : end - kSetupScopeKey.size());
    const auto known = std::find(kSetupScopes.begin(), kSetupScopes.end(), name);
    if (known == kSetupScopes.end()) {
        return std::nullopt;
    }
    const auto scope = static_cast<SetupScope>(known - kSetupScopes.begin());
    EndlessSetup settings = EndlessSetup::defaults(scope);
    if (end != std::string_view::npos) {
        read_setup(settings, text.substr(end + 1), "");
    }
    return std::pair{settings, scope};
}

bool may_block_seats(const net::Lobby& lobby) {
    if (lobby.started) {
        return false;
    }
    const auto settings = read_lobby_setup(lobby.setup);
    return settings && settings->second == SetupScope::MultiplayerEndless;
}

std::vector<net::LobbySeat> endless_seats(std::size_t players) {
    players = std::clamp<std::size_t>(players, HostSetup::kMinPlayers, sim::kMaxPlayers);
    std::vector<net::LobbySeat> seats(std::max<std::size_t>(sim::player_slot(players - 1) + 1U, sim::kOriginalPlayers));
    for (std::size_t s = 0; s < seats.size(); ++s) {
        // Slot and banner go together, the factions' too (sim::kFirstExtraBanner).
        seats[s].banner = static_cast<sim::BannerId>(s);
        if (s >= sim::kOriginalPlayers && s < sim::kFirstExtraPlayer) {
            seats[s].kind = net::LobbySeat::Kind::Reserved;
        }
    }
    return seats;
}

std::vector<sim::BannerId> player_banners() {
    std::vector<sim::BannerId> banners;
    for (std::size_t n = 0; n < sim::kMaxPlayers; ++n) {
        banners.push_back(static_cast<sim::BannerId>(
            n < sim::kOriginalPlayers ? n : sim::kFirstExtraBanner + (n - sim::kOriginalPlayers)));
    }
    return banners;
}

std::string banner_name(std::string_view id) {
    const std::size_t colon = id.find(':');
    const std::string_view colour = colon == std::string_view::npos ? id : id.substr(colon + 1);
    const std::string key = "net.colour." + std::string(colour);
    const std::string& text = ui::tr(key);
    return text != key ? text : std::string(colour);
}

std::optional<std::pair<std::string, std::uint16_t>> parse_address(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    std::uint16_t port = net::kDefaultPort;
    if (const std::size_t colon = text.rfind(':'); colon != std::string_view::npos) {
        const auto p = number(text.substr(colon + 1));
        if (!p || *p == 0 || *p > 65535) {
            return std::nullopt;
        }
        port = static_cast<std::uint16_t>(*p);
        text = text.substr(0, colon);
    }
    if (text.empty() || text.find_first_of(" :") != std::string_view::npos) {
        return std::nullopt;
    }
    return std::pair{std::string(text), port};
}

MultiplayerMenu::MultiplayerMenu(ui::FormStyle style) : m_form(style), m_settings(style) {
    m_setup.seed = random_seed();
    m_seed_text = std::to_string(m_setup.seed);
    m_port_text = std::to_string(m_setup.port);
}

void MultiplayerMenu::open(std::string notice) {
    m_screen = Screen::Browse;
    m_notice = std::move(notice);
    m_form.set_focus(std::nullopt);
    m_name_known = false;  // the name from the settings again
    m_ready = false;
    m_password_address.reset();
    m_join_password.clear();
}

void MultiplayerMenu::show_connecting(std::string address) {
    m_screen = Screen::Connecting;
    m_connecting = std::move(address);
    m_form.set_focus(std::nullopt);
}

void MultiplayerMenu::show_lobby() {
    m_screen = Screen::Lobby;
    m_ready = false;
    m_banner_seat.reset();
    m_chat.clear();
    m_form.set_focus(m_explicit_text_input ? std::nullopt : std::optional<Id>(kChat));
}

void MultiplayerMenu::show_network(){if(m_screen!=Screen::Network)m_network_from=m_screen;m_screen=Screen::Network;m_form.set_focus(std::nullopt);m_selected.reset();}

std::vector<ui::DrawItem> MultiplayerMenu::draw(const MultiplayerModel& model,
                                                const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    if (m_screen == Screen::Settings) {
        return m_settings.draw(fonts);
    }
    m_chat_font = m_form.font(fonts);  // the chat is wrapped with it
    layout(model);
    return m_form.draw(fonts);
}

bool MultiplayerMenu::wheel(const MultiplayerModel& model, std::int32_t notches) {
    switch (m_screen) {
        case Screen::Browse:
            m_browse_first = ui::wheel_scroll(m_browse_first, browse_rows(model).size(), kBrowseRows, notches);
            return true;
        case Screen::Create:
            if (m_setup.kind == HostSetup::Kind::Endless) {
                return false;
            }
            m_list_first = ui::wheel_scroll(
                m_list_first,
                m_setup.kind == HostSetup::Kind::Scenario ? model.scenarios.size() : model.savegames.size(), kListRows,
                notches);
            return true;
        case Screen::Lobby: {
            // The chat, back to older rows and forward again (not while the colours are offered).
            if (m_banner_seat || m_chat_rows == 0) {
                return true;
            }
            const std::size_t count = chat_rows(model).size();
            const std::size_t last = count > m_chat_rows ? count - m_chat_rows : 0;
            const std::size_t first = last - std::min(m_chat_back, last);
            m_chat_back = last - ui::wheel_scroll(first, count, m_chat_rows, notches);
            return true;
        }
        case Screen::Connecting:
        case Screen::Settings:
        case Screen::Network:
            break;
    }
    return false;
}

std::vector<MultiplayerEvent> MultiplayerMenu::mouse(const MultiplayerModel& model, std::int32_t x, std::int32_t y,
                                                     bool down, ui::MouseButton button) {
    std::vector<MultiplayerEvent> out;
    if (m_screen == Screen::Settings) {
        const GameSettingsMenu::Result result = m_settings.mouse(x, y, down, button);
        if (result == GameSettingsMenu::Result::Start) {
            if (m_settings_from == Screen::Create) {
                // More than four players always get a world of their own; with fewer again, the
                // choice the host made stays.
                const bool own_world = m_setup.settings.own_world;
                m_setup.settings = m_settings.setup();
                if (m_setup.players > sim::kOriginalPlayers) {
                    m_setup.settings.own_world = own_world;
                }
            } else {
                MultiplayerEvent e;
                e.kind = MultiplayerEvent::Kind::Settings;
                e.setup.settings = m_settings.setup();
                out.push_back(std::move(e));
            }
        }
        if (result != GameSettingsMenu::Result::None) {
            close_settings();
        }
        return out;
    }
    if (button != ui::MouseButton::Left) {
        return out;
    }
    layout(model);
    const auto focus = m_form.focus();
    if (const auto id = m_form.mouse(x, y, down)) {
        click(model, *id, out);
    } else if (m_form.released()) {
        // A click beside the fields ends typing (the chat line stays open). The mouse only moving
        // away keeps it: the keys go to the field clicked until Enter, Escape or another click.
        if (const auto under = m_form.hovered(); !under || field_text(*under) == nullptr) {
            if (m_screen != Screen::Lobby) {
                m_form.set_focus(std::nullopt);
            }
        }
    }
    if (focus == kName && m_form.focus() != kName) {
        // Left the name field with the mouse: the name counts, as with Tab or Escape.
        MultiplayerEvent e;
        e.kind = MultiplayerEvent::Kind::Name;
        e.text = m_name;
        out.insert(out.begin(), std::move(e));
    }
    return out;
}

void MultiplayerMenu::layout(const MultiplayerModel& model) {
    if (!m_name_known) {
        m_name = model.name;
        m_name_known = true;
    }
    if (!m_settings_known) {
        m_setup.settings = model.settings;
        m_settings_known = true;
    }
    m_form.clear();
    m_form.panel(kScreen);
    switch (m_screen) {
        case Screen::Network: {
            const auto& n=model.network;
            m_form.label(kHeading,ui::tr("vita.net.title"),Align::Left,Tone::Heading);
            m_form.button(kInternet,Rect{64,100,420,52},ui::tr("vita.net.wlan"),!n.pending&&!model.session);
            m_form.button(kAdhoc,Rect{516,100,444,52},ui::tr("vita.net.adhoc"),!n.pending&&!model.session);
            m_form.paragraph(Rect{64,178,896,72},ui::tr(n.status_key),Tone::Heading);
            m_form.label(Rect{64,250,896,32},ui::tr("vita.net.own",{n.own_ip}));
            m_form.label(Rect{64,286,896,32},ui::tr("vita.net.peers",{std::to_string(n.candidates.size())}));
            if(model.session)m_form.label(Rect{64,322,896,32},ui::tr("vita.net.session",{ui::tr(model.hosting?"vita.net.role_host":"vita.net.role_guest"),ui::tr(model.session->ended()?"vita.net.ended":model.session->lobby().started?"vita.net.playing":"vita.net.lobby")}));
            m_form.label(Rect{64,358,896,32},ui::tr("vita.net.resources",{std::to_string(n.sockets),std::to_string(n.sent),std::to_string(n.received)}));
            m_form.paragraph(Rect{64,400,896,96},ui::tr("vita.net.help"),Tone::Dim);
            if(!n.error.empty())m_form.label(Rect{64,510,896,40},n.error,Align::Left,Tone::Warning);
            const bool ready=n.mode==net::NetworkMode::Adhoc&&n.ready&&!n.pending&&!model.session;
            m_form.button(kRadioHost,Rect{64,570,420,52},ui::tr("vita.net.host"),ready);
            m_form.button(kRadioSearch,Rect{516,570,444,52},ui::tr("vita.net.search"),ready);
            m_form.button(kBack,Rect{40,kBottomY,240,44},model.session?tr(MpText::Back):ui::tr("vita.net.leave"));
            break;
        }
        case Screen::Browse:
            layout_browse(model);
            break;
        case Screen::Create:
            layout_create(model);
            break;
        case Screen::Lobby:
            layout_lobby(model);
            break;
        case Screen::Connecting:
            layout_connecting();
            break;
        case Screen::Settings:
            break;  // the start dialog's own form
    }
}

void MultiplayerMenu::open_settings(const EndlessSetup& setup, SetupScope scope, GameSettingsMenu::Mode mode) {
    m_settings.open(setup, scope, mode);
    m_settings_from = m_screen;
    m_screen = Screen::Settings;
    m_form.set_focus(std::nullopt);  // the keys do not go to a field behind the dialog
}

void MultiplayerMenu::settings_button(std::int32_t y, const EndlessSetup& setup, bool can_change) {
    m_form.label(Rect{684, y, 292, 30}, ui::tr("setup.summary", {rules_summary(setup.rules)}), Align::Left, Tone::Dim);
    m_form.button(kSettings, Rect{688, y + 34, 284, 38}, ui::tr(can_change ? "setup.change" : "setup.view"));
}

const std::vector<MultiplayerMenu::ChatRow>& MultiplayerMenu::chat_rows(const MultiplayerModel& model) {
    // Lines without a serial cannot be told apart: wrapped anew each time.
    const auto made_of = model.chat.empty()
                             ? decltype(m_chat_wrapped_for){0, 0, 0, m_chat_font}
                             : decltype(m_chat_wrapped_for){model.chat.front().serial, model.chat.back().serial,
                                                            model.chat.size(), m_chat_font};
    if (made_of == m_chat_wrapped_for && std::get<1>(made_of) != 0) {
        return m_chat_wrapped;
    }
    m_chat_wrapped_for = made_of;
    m_chat_wrapped.clear();
    // As wide as a label in the chat's box has room for, so that no row is cut.
    const double width = kChatBox.w - 2.0 * ui::kTextPadding;
    for (std::size_t i = 0; i < model.chat.size(); ++i) {
        const ChatLine& line = model.chat[i];
        bool first = true;
        for (std::string& text : ui::wrap_lines(m_chat_font, chat_prefix(line) + line.text, width)) {
            m_chat_wrapped.push_back(ChatRow{i, line.serial, std::move(text), std::exchange(first, false)});
        }
    }
    return m_chat_wrapped;
}

std::string chat_prefix(const ChatLine& line) {
    if (line.name.empty()) {
        return {};
    }
    return line.whisper ? line.name + " (" + line.to + "): " : line.name + ": ";
}

// --- Finding a game ---

std::vector<MultiplayerMenu::BrowseRow> MultiplayerMenu::browse_rows(const MultiplayerModel& model) const {
    std::vector<BrowseRow> rows;
    for (const net::FoundGame& g : model.found) {
        rows.push_back(BrowseRow{g.address + ":" + std::to_string(g.info.port), &g});
    }
    return rows;
}

std::string MultiplayerMenu::state_text(const net::GameInfo& info) const {
    if (info.protocol != net::kProtocolVersion) {
        return tr(MpText::StateOtherVersion);
    }
    if (!info.started) {
        return tr(MpText::StateLobby);
    }
    if (info.join_running && info.players < info.slots) {
        return tr(MpText::StateRunningJoin);
    }
    return tr(info.spectators ? MpText::StateRunningWatch : MpText::StateRunning);
}

void MultiplayerMenu::layout_browse(const MultiplayerModel& model) {
    ui::Form& f = m_form;
    f.label(kHeading, tr(MpText::Multiplayer), Align::Left, Tone::Heading);
    f.label(Rect{560, 32, 170, 32}, tr(MpText::YourName), Align::Right);
    f.field(kName, Rect{740, 32, 244, 32}, m_name);

    // The games that answered the search (hosts joined before are asked too, but only listed when
    // they answer).
    f.panel(Rect{40, 84, 644, 586});
    const std::array<std::pair<std::int32_t, std::int32_t>, 5> cols = {
        {{48, 210}, {258, 140}, {398, 80}, {478, 130}, {608, 70}}};
    const std::array<MpText, 5> heads = {MpText::ColGame, MpText::ColHost, MpText::ColPlayers, MpText::ColState,
                                         MpText::ColPing};
    for (std::size_t c = 0; c < cols.size(); ++c) {
        f.label(Rect{cols[c].first, 92, cols[c].second, 28}, tr(heads[c]), Align::Left, Tone::Heading);
    }
    const std::vector<BrowseRow> rows = browse_rows(model);
    m_browse_first = std::min(m_browse_first, rows.size() > kBrowseRows ? rows.size() - kBrowseRows : 0);
    if (rows.empty()) {
        f.label(Rect{48, 140, 620, 30}, tr(MpText::NoGamesFound));
    }
    for (std::size_t r = 0; r < kBrowseRows && m_browse_first + r < rows.size(); ++r) {
        const BrowseRow& row = rows[m_browse_first + r];
        const std::int32_t y = 124 + static_cast<std::int32_t>(r) * kRowH;
        f.row(kBrowseRow + static_cast<Id>(r), Rect{44, y, 636, kRowH}, m_selected == row.address);
        const net::GameInfo& info = row.game->info;
        f.label(Rect{cols[0].first, y, cols[0].second, kRowH},
                (row.game->address.starts_with("relay-") ? ui::tr("master.row_prefix") + " " : std::string{}) +
                    (info.password ? "* " + info.title : info.title));
        f.label(Rect{cols[1].first, y, cols[1].second, kRowH}, info.host);
        f.label(Rect{cols[2].first, y, cols[2].second, kRowH},
                std::to_string(info.players) + " / " + std::to_string(info.slots));
        f.label(Rect{cols[3].first, y, cols[3].second, kRowH}, state_text(info), Align::Left,
                info.protocol == net::kProtocolVersion ? Tone::Normal : Tone::Warning);
        f.label(Rect{cols[4].first, y, cols[4].second, kRowH}, std::to_string(row.game->ping_ms) + " ms");
    }
    if (m_browse_first > 0) {
        f.button(kBrowseUp, Rect{604, 588, 36, 30}, "-");
    }
    if (m_browse_first + kBrowseRows < rows.size()) {
        f.button(kBrowseDown, Rect{644, 588, 36, 30}, "+");
    }
    f.label(Rect{48, 626, 620, 30}, m_notice.empty() ? tr(MpText::SearchHint) : m_notice, Align::Left,
            m_notice.empty() ? Tone::Dim : Tone::Warning);

    // Joining by address, creating.
    f.panel(Rect{700, 84, 284, 236});
    f.label(Rect{708, 92, 268, 30}, tr(MpText::ConnectDirectly), Align::Left, Tone::Heading);
    f.label(Rect{708, 124, 268, 24}, tr(MpText::HostAddress), Align::Left, Tone::Dim);
    f.field(kAddress, Rect{712, 150, 260, 32}, m_address);
    f.label(Rect{708, 186, 268, 24}, tr(MpText::Password), Align::Left, Tone::Dim);
    f.field(kJoinPassword, Rect{712, 212, 260, 32}, m_join_password, true);
    f.button(kConnect, Rect{712, 262, 260, 40}, tr(MpText::Connect), parse_address(m_address).has_value());
    f.button(kCreate, Rect{700, 340, 284, 56}, tr(MpText::CreateGame), true, true);
    f.paragraph(Rect{700, 410, 284, 120}, tr(MpText::HostHint, std::to_string(net::kDefaultPort)), Tone::Dim);
    if (!model.master_status.empty() && model.network.mode != net::NetworkMode::Adhoc)
        f.paragraph(Rect{700, 594, 284, 70}, model.master_status, Tone::Dim, 22);
    if(model.network.supported){
        f.button(kNetwork,Rect{700,548,284,44},ui::tr("vita.net.title"));
        if(model.network.mode==net::NetworkMode::Adhoc){
            f.label(Rect{700,598,284,28},ui::tr("vita.net.adhoc"),Align::Left,Tone::Heading);
            f.label(Rect{700,628,284,28},model.network.own_ip,Align::Left,Tone::Dim);
        }
    }

    f.button(kBack, Rect{40, kBottomY, 180, 44}, tr(MpText::Back));
    f.button(kRefresh, Rect{596, kBottomY, 180, 44}, tr(MpText::Refresh));
    f.button(kJoin, Rect{804, kBottomY, 180, 44}, tr(MpText::Join), m_selected.has_value());
}

// --- Creating a game ---

void MultiplayerMenu::layout_create(const MultiplayerModel& model) {
    ui::Form& f = m_form;
    f.label(kHeading, tr(MpText::CreateTitle), Align::Left, Tone::Heading);
    using Kind = HostSetup::Kind;
    f.button(kKindEndless, Rect{40, 84, 200, 40}, tr(MpText::NewEndless), true, m_setup.kind == Kind::Endless);
    f.button(kKindScenario, Rect{244, 84, 180, 40}, tr(MpText::Scenario), true, m_setup.kind == Kind::Scenario);
    f.button(kKindSavegame, Rect{428, 84, 180, 40}, tr(MpText::Savegame), true, m_setup.kind == Kind::Savegame);
    f.panel(Rect{40, 130, 600, 540});
    switch (m_setup.kind) {
        case Kind::Endless: {
            f.label(Rect{52, 142, 300, 30}, tr(MpText::Level), Align::Left, Tone::Heading);
            for (std::size_t i = 0; i < sim::NewGameOptions::kDifficulties; ++i) {
                std::string name = i < model.level_names.size() && !model.level_names[i].empty()
                                       ? model.level_names[i]
                                       : tr(MpText::LevelN, std::to_string(i + 1));
                const auto x = static_cast<std::int32_t>(56 + (i % 2) * 288);
                const auto y = static_cast<std::int32_t>(180 + (i / 2) * 50);
                f.button(kLevel + static_cast<Id>(i), Rect{x, y, 280, 42}, std::move(name), true, m_setup.level == i);
            }
            f.label(Rect{52, 300, 300, 30}, tr(MpText::WorldNumber), Align::Left, Tone::Heading);
            f.field(kSeed, Rect{56, 336, 200, 36}, m_seed_text);
            f.button(kRoll, Rect{264, 336, 140, 36}, tr(MpText::Roll));
            f.label(Rect{52, 392, 300, 30}, tr(MpText::Players), Align::Left, Tone::Heading);
            f.button(kFewerPlayers, Rect{56, 428, 44, 36}, "-", m_setup.players > HostSetup::kMinPlayers);
            f.label(Rect{100, 428, 80, 36}, std::to_string(m_setup.players), Align::Centre);
            f.button(kMorePlayers, Rect{180, 428, 44, 36}, "+", m_setup.players < sim::kMaxPlayers);
            f.paragraph(Rect{52, 484, 576, 80}, tr(MpText::EndlessHint), Tone::Dim);
            break;
        }
        case Kind::Scenario:
        case Kind::Savegame: {
            const bool scenarios = m_setup.kind == Kind::Scenario;
            const std::size_t count = scenarios ? model.scenarios.size() : model.savegames.size();
            m_list_first = std::min(m_list_first, count > kListRows ? count - kListRows : 0);
            if (count == 0 && !scenarios) {
                f.label(Rect{52, 142, 560, 30}, tr(MpText::NoSavegames));
            }
            for (std::size_t r = 0; r < kListRows && m_list_first + r < count; ++r) {
                const std::size_t i = m_list_first + r;
                const std::int32_t y = 140 + static_cast<std::int32_t>(r) * kRowH;
                if (scenarios) {
                    const MenuScenario& s = model.scenarios[i];
                    if (s.separator) {
                        f.label(Rect{52, y, 560, kRowH}, s.name, Align::Left, Tone::Heading);
                        continue;
                    }
                    f.row(kListRow + static_cast<Id>(r), Rect{44, y, 592, kRowH}, m_chosen == i);
                    f.label(Rect{52, y, 576, kRowH}, s.name);
                } else {
                    f.row(kListRow + static_cast<Id>(r), Rect{44, y, 592, kRowH}, m_chosen == model.savegames[i].first);
                    f.label(Rect{52, y, 576, kRowH}, model.savegames[i].second);
                }
            }
            if (m_list_first > 0) {
                f.button(kListUp, Rect{556, 498, 36, 30}, "-");
            }
            if (m_list_first + kListRows < count) {
                f.button(kListDown, Rect{596, 498, 36, 30}, "+");
            }
            f.paragraph(Rect{52, 540, 576, 120}, tr(scenarios ? MpText::ScenarioHint : MpText::SavegameHint),
                        Tone::Dim);
            break;
        }
    }

    // The session.
    f.panel(Rect{656, 84, 328, 586});
    f.label(Rect{668, 92, 300, 30}, tr(MpText::Session), Align::Left, Tone::Heading);
    f.label(Rect{664, 126, 312, 24}, tr(MpText::GameTitle), Align::Left, Tone::Dim);
    f.field(kTitle, Rect{668, 152, 304, 34}, m_setup.title);
    f.label(Rect{664, 194, 312, 24}, tr(MpText::PasswordOptional), Align::Left, Tone::Dim);
    f.field(kPassword, Rect{668, 220, 304, 34}, m_setup.password, true);
    f.check(kLan, Rect{668, 270, 304, 30}, tr(MpText::ShowOnLan), m_setup.lan && !m_setup.public_room,
            !m_setup.public_room);
    f.check(kJoinRunning, Rect{668, 302, 304, 30}, tr(MpText::AllowJoinRunning), m_setup.join_running);
    f.check(kSpectators, Rect{668, 334, 304, 30}, tr(MpText::AllowSpectators), m_setup.spectators);
    f.check(kSpectatorChat, Rect{668, 366, 304, 30}, tr(MpText::AllowSpectatorChat), m_setup.spectators_chat,
            m_setup.spectators);
    f.check(kTakeOver, Rect{668, 398, 304, 30}, tr(MpText::ComputerTakesOver), m_setup.computer_takes_over);
    f.check(kDedicated, Rect{668, 430, 304, 30}, tr(MpText::HostOnly), m_setup.dedicated);
    f.check(kHostPauses, Rect{668, 462, 304, 30}, tr(MpText::HostPausesOnly), m_setup.host_pauses_only);
    if (m_setup.kind != HostSetup::Kind::Savegame) {
        // A savegame keeps the settings it was started with; the lobby shows them.
        settings_button(502, m_setup.game_settings(), true);
    }
    const bool can_public = model.master_configured && model.master_ready &&
                            model.network.mode != net::NetworkMode::Adhoc;
    if (!can_public)
        m_setup.public_room = false;
    f.check(kPublic, Rect{668, 574, 304, 30}, ui::tr("master.public"), m_setup.public_room, can_public);
    if (!m_setup.public_room) {
        f.label(Rect{664, 620, 150, 34}, tr(MpText::Port), Align::Left, Tone::Dim);
        f.field(kPort, Rect{820, 620, 152, 34}, m_port_text);
    }

    const bool chosen = m_setup.kind == HostSetup::Kind::Endless || m_chosen.has_value();
    const auto port = number(m_port_text);
    const bool valid = chosen && port && *port > 0 && *port <= 65535 &&
                       (m_setup.kind != HostSetup::Kind::Endless || number(m_seed_text).has_value());
    f.button(kCreateBack, Rect{40, kBottomY, 180, 44}, tr(MpText::Back));
    f.button(kOpenLobby, Rect{764, kBottomY, 220, 44}, tr(MpText::OpenLobby),
             valid && (!m_setup.public_room || model.master_ready), true);
}

// --- The lobby ---

void MultiplayerMenu::layout_lobby(const MultiplayerModel& model) {
    ui::Form& f = m_form;
    const net::Session* session = model.session;
    if (session == nullptr) {
        return;
    }
    const net::Lobby& lobby = session->lobby();
    const sim::PlayerId me = session->local_seat();
    f.label(kHeading, lobby.title, Align::Left, Tone::Heading);
    f.label(Rect{560, 32, 424, 32},
            model.public_session ? ui::tr("master.public")
                                 : (model.hosting ? tr(MpText::HostPort, std::to_string(model.port)) : lobby.game),
            Align::Right, Tone::Dim);

    // The slots of the players (the factions' are left out), closer together when there are many;
    // the chat below gets what they leave.
    std::vector<std::size_t> shown;
    for (std::size_t s = 0; s < lobby.seats.size() && s < kMaxSeats; ++s) {
        if (lobby.seats[s].kind != net::LobbySeat::Kind::Reserved) {
            shown.push_back(s);
        }
    }
    const std::int32_t row_h = shown.size() <= 8 ? 36 : 30;
    const auto rows_end = static_cast<std::int32_t>(96 + shown.size() * static_cast<std::size_t>(row_h));
    const std::int32_t chat_top = rows_end + 50;
    f.panel(Rect{40, 84, 620, chat_top - 96});
    for (std::size_t r = 0; r < shown.size(); ++r) {
        const std::size_t s = shown[r];
        const net::LobbySeat& seat = lobby.seats[s];
        const auto y = static_cast<std::int32_t>(96 + r * static_cast<std::size_t>(row_h));
        const std::int32_t h = row_h - 2;
        const Id k = static_cast<Id>(s);
        if (may_choose_banner(model, s)) {
            // A click on the colour offers the others.
            f.button(kSeatBanner + k, Rect{48, y, 30, h}, {}, true, m_banner_seat == s);
        }
        f.swatch(Rect{52, y + 4, 22, h - 8}, banner_colour(seat.banner));
        std::string who;
        std::string state;
        Tone tone = Tone::Normal;
        switch (seat.kind) {
            case net::LobbySeat::Kind::Human: {
                who = seat.name;
                const bool host = std::any_of(session->roster().begin(), session->roster().end(),
                                              [&](const net::Member& m) { return m.host && m.seat == s; });
                if (s == me) {
                    who += " " + tr(MpText::You);
                } else if (host) {
                    who += " " + tr(MpText::Host);
                }
                state = seat.ready ? tr(MpText::Ready) : tr(MpText::Choosing);
                tone = seat.ready ? Tone::Normal : Tone::Dim;
                break;
            }
            case net::LobbySeat::Kind::Open:
                who = tr(MpText::SeatOpen);
                state = tr(MpText::Free);
                tone = Tone::Dim;
                break;
            case net::LobbySeat::Kind::Computer:
                who = tr(MpText::SeatComputer);
                state = tr(MpText::Ready);
                break;
            case net::LobbySeat::Kind::Closed:
            case net::LobbySeat::Kind::Reserved:
                who = tr(MpText::SeatBlocked);
                tone = Tone::Dim;
                break;
        }
        const bool human = seat.kind == net::LobbySeat::Kind::Human;
        if (model.hosting && !human) {
            // The host sets who plays a slot by clicking through the kinds.
            f.button(kSeatKind + k, Rect{84, y, 250, h}, who);
        } else {
            f.label(Rect{80, y, 260, h}, who, Align::Left, human ? Tone::Normal : Tone::Dim);
        }
        f.label(Rect{344, y, 150, h}, state, Align::Left, tone);
        if (model.hosting && human && s != me) {
            f.button(kSeatKick + k, Rect{500, y, 150, h}, tr(MpText::Kick));
        } else if (seat.kind == net::LobbySeat::Kind::Open && s != me && !lobby.started) {
            f.button(kSeatTake + k, Rect{500, y, 150, h}, tr(MpText::Take));
        }
    }
    std::string watchers;
    for (const net::Member& m : session->roster()) {
        if (m.seat == sim::kNoPlayer) {
            watchers += (watchers.empty() ? "" : ", ") + m.name;
        }
    }
    f.label(Rect{48, rows_end + 4, 600, 30}, tr(MpText::Watchers, watchers.empty() ? tr(MpText::Nobody) : watchers),
            Align::Left, Tone::Dim);

    // The chat, or the colours to choose from.
    f.panel(Rect{40, chat_top, 620, 670 - chat_top});
    if (m_banner_seat && !may_choose_banner(model, *m_banner_seat)) {
        m_banner_seat.reset();  // the slot changed hands, or the game started
    }
    const auto chat_lines = static_cast<std::size_t>(std::max(0, (622 - 8 - (chat_top + 8)) / 28));
    m_chat_rows = m_banner_seat ? 0 : std::min(kChatLines, chat_lines);
    // Scrolled back (mouse wheel), the view holds still while new lines come in.
    const std::vector<ChatRow>& wrapped = chat_rows(model);
    const std::uint64_t newest = model.chat.empty() ? 0 : model.chat.back().serial;
    if (newest < m_chat_newest) {
        m_chat_back = 0;  // the chat was cleared
    } else if (m_chat_back > 0) {
        for (auto r = wrapped.rbegin(); r != wrapped.rend() && r->serial > m_chat_newest; ++r) {
            ++m_chat_back;  // a row of a new line
        }
    }
    m_chat_newest = newest;
    const std::size_t visible = std::min(wrapped.size(), m_chat_rows);
    m_chat_back = std::min(m_chat_back, wrapped.size() - visible);
    if (m_banner_seat) {
        layout_banner_choice(model, chat_top + 8);
    }
    for (std::size_t i = 0; i < visible; ++i) {
        const ChatRow& row = wrapped[wrapped.size() - m_chat_back - visible + i];
        const ChatLine& line = model.chat[row.line];
        const std::int32_t y = chat_top + 8 + static_cast<std::int32_t>(i) * 28;
        const Rect box{kChatBox.x, y, kChatBox.w, 28};
        const Tone tone = line.whisper ? Tone::Heading : Tone::Normal;
        if (line.name.empty()) {
            f.label(box, row.text, Align::Left, Tone::Dim);
        } else if (row.first && line.seat < lobby.seats.size() && row.text.starts_with(line.name)) {
            // A player's name in the colour they chose; a spectator's in the line's.
            f.speech(box, line.name, banner_text_colour(lobby.seats[line.seat].banner),
                     row.text.substr(line.name.size()), tone);
        } else {
            f.label(box, row.text, Align::Left, tone);
        }
    }
    f.field(kChat, Rect{52, 622, 596, 36}, m_chat);
    if (m_chat.empty() && m_form.focus() != kChat) {
        f.label(Rect{52, 622, 596, 36}, tr(MpText::ChatHint), Align::Left, Tone::Dim);
    }

    // What is played, and the hint.
    f.panel(Rect{676, 84, 308, 586});
    f.paragraph(Rect{684, 96, 292, 70}, lobby.game, Tone::Heading, 26);
    const auto setting = [&](std::int32_t y, MpText name, bool on) {
        f.entry(Rect{684, y, 292, 30}, tr(name), tr(on ? MpText::Yes : MpText::No), Tone::Dim);
    };
    setting(176, MpText::Password, lobby.password);
    setting(208, MpText::AllowJoinRunning, lobby.join_running);
    setting(240, MpText::AllowSpectators, lobby.spectators);
    setting(272, MpText::AllowSpectatorChat, lobby.spectators && lobby.spectators_chat);
    setting(304, MpText::ComputerTakesOver, lobby.computer_takes_over);
    setting(336, MpText::HostPausesOnly, lobby.host_pauses_only);
    // The host's settings: the host changes them until the game starts, the others look at them.
    std::int32_t y_free = 386;
    if (const auto settings = read_lobby_setup(lobby.setup)) {
        settings_button(372, settings->first,
                        model.hosting && !lobby.started && settings->second != SetupScope::Savegame);
        y_free = 452;
    }
    if (lobby.spectators && me != sim::kNoPlayer) {
        f.button(kWatch, Rect{688, y_free, 284, 38}, tr(MpText::Watch));
    }
    if (model.hosting) {
        // The host can send spectators away too (they hold no slot to kick).
        m_watchers.clear();
        const auto& roster = session->roster();
        // As many rows as fit above the hint.
        const auto rows = std::min(kWatcherRows, static_cast<std::size_t>((560 - y_free - 46) / 40));
        for (std::size_t i = 1; i < roster.size() && m_watchers.size() < rows; ++i) {
            if (roster[i].seat == sim::kNoPlayer) {
                const std::int32_t y = y_free + 46 + static_cast<std::int32_t>(m_watchers.size()) * 40;
                f.label(Rect{688, y, 130, 38}, roster[i].name, Align::Left, Tone::Dim);
                f.button(kWatcherKick + static_cast<Id>(m_watchers.size()), Rect{822, y, 150, 38}, tr(MpText::Kick));
                m_watchers.push_back({i, roster[i].name});
            }
        }
    }
    if (model.join != net::ClientSession::JoinStage::None) {
        layout_join_progress(model, Rect{684, 560, 292, 100});
    } else {
        f.paragraph(Rect{684, 560, 292, 100}, tr(model.hosting ? MpText::StartHint : MpText::WaitHostStart), Tone::Dim);
    }

    f.button(kLeave, Rect{40, kBottomY, 180, 44}, tr(MpText::Leave));
    if(model.network.supported)f.button(kNetwork,Rect{240,kBottomY,280,44},ui::tr("vita.net.info"));
    if (!model.hosting && me != sim::kNoPlayer) {
        f.button(kReady, Rect{560, kBottomY, 200, 44}, tr(MpText::Ready), true, m_ready);
    }
    if (model.hosting) {
        f.button(kStart, Rect{764, kBottomY, 220, 44}, tr(MpText::Start), model.everyone_ready, true);
    }
}

bool MultiplayerMenu::may_choose_banner(const MultiplayerModel& model, std::size_t seat) {
    const net::Session* session = model.session;
    if (session == nullptr) {
        return false;
    }
    const net::Lobby& lobby = session->lobby();
    if (lobby.started || lobby.banners.empty() || seat >= lobby.seats.size()) {
        return false;
    }
    const net::LobbySeat::Kind kind = lobby.seats[seat].kind;
    if (seat == session->local_seat()) {
        return true;
    }
    return model.hosting && kind != net::LobbySeat::Kind::Human && kind != net::LobbySeat::Kind::Reserved;
}

std::string MultiplayerMenu::banner_label(sim::BannerId banner) const {
    return banner < m_banners.size() ? banner_name(m_banners[banner].id) : std::to_string(banner);
}

void MultiplayerMenu::layout_banner_choice(const MultiplayerModel& model, std::int32_t top) {
    ui::Form& f = m_form;
    const net::Lobby& lobby = model.session->lobby();
    const std::size_t seat = *m_banner_seat;
    const net::LobbySeat& mine = lobby.seats[seat];
    f.label(Rect{48, top, 604, 28}, tr(MpText::ChooseBanner), Align::Left, Tone::Heading);
    // As many rows as there is room above the chat line, at most 36 high.
    const std::int32_t grid_top = top + 32;
    const std::int32_t room = 616 - grid_top;
    const std::size_t wanted_rows =
        (std::min(lobby.banners.size(), kMaxBannerChoices) + kBannerColumns - 1) / kBannerColumns;
    const std::size_t rows = std::min<std::size_t>(wanted_rows, static_cast<std::size_t>(std::max(0, room / 26)));
    const std::int32_t cell_h = rows == 0 ? 0 : std::min<std::int32_t>(36, room / static_cast<std::int32_t>(rows));
    const std::int32_t cell_w = 600 / static_cast<std::int32_t>(kBannerColumns);
    m_banner_choices.clear();
    for (std::size_t i = 0; i < lobby.banners.size() && i < rows * kBannerColumns; ++i) {
        const sim::BannerId banner = lobby.banners[i];
        // Another person's banner is theirs; a computer's or a free slot's is swapped.
        const bool other_person = std::any_of(lobby.seats.begin(), lobby.seats.end(), [&](const net::LobbySeat& s) {
            return &s != &mine && s.kind == net::LobbySeat::Kind::Human && s.banner == banner;
        });
        const auto x = static_cast<std::int32_t>(50 + (i % kBannerColumns) * static_cast<std::size_t>(cell_w));
        const auto y = static_cast<std::int32_t>(grid_top + static_cast<std::int32_t>(i / kBannerColumns) * cell_h);
        const Rect cell{x, y, cell_w - 4, cell_h - 2};
        f.button(kBannerChoice + static_cast<Id>(i), cell, {}, !other_person, banner == mine.banner);
        f.swatch(Rect{x + 5, y + 6, 16, cell_h - 14}, banner_colour(banner));
        f.label(Rect{x + 24, y, cell_w - 30, cell_h - 2}, banner_label(banner), Align::Left,
                other_person ? Tone::Dim : Tone::Normal);
        m_banner_choices.push_back(banner);
    }
}

void MultiplayerMenu::layout_join_progress(const MultiplayerModel& model, Rect box) {
    // What happens now, the bar, and below it the megabytes while the game comes in.
    using Stage = net::ClientSession::JoinStage;
    ui::Form& f = m_form;
    const MpText what = model.join == Stage::Waiting     ? MpText::GameWaiting
                        : model.join == Stage::Receiving ? MpText::GameReceiving
                                                         : MpText::GameLoading;
    f.paragraph(Rect{box.x, box.y, box.w, 48}, tr(what), Tone::Heading);
    double done = 0.0;
    if (model.join == Stage::Loading) {
        done = 1.0;
    } else if (model.join == Stage::Receiving && model.join_total > 0) {
        done = static_cast<double>(model.join_received) / static_cast<double>(model.join_total);
    }
    f.progress(Rect{box.x + 8, box.y + 52, box.w - 16, 20}, done);
    if (model.join == Stage::Receiving && model.join_total > 0) {
        constexpr std::uint64_t kMiB = std::uint64_t{1} << 20;
        const std::uint64_t received = model.join_received / kMiB;
        const std::uint64_t total = (model.join_total + kMiB - 1) / kMiB;
        f.label(Rect{box.x, box.y + 74, box.w, 26},
                mp_text(MpText::GameBytes, std::to_string(received), std::to_string(total)), Align::Centre, Tone::Dim);
    }
}

void MultiplayerMenu::layout_connecting() {
    m_form.label(kHeading, tr(MpText::Multiplayer), Align::Left, Tone::Heading);
    m_form.label(Rect{40, 340, 944, 40}, tr(MpText::Connecting, m_connecting), Align::Centre);
    m_form.button(kCancel, Rect{40, kBottomY, 180, 44}, tr(MpText::Back));
}

// --- Clicks and keys ---

std::optional<MultiplayerEvent> MultiplayerMenu::join_event(const std::string& address) const {
    const auto parsed = parse_address(address);
    if (!parsed) {
        return std::nullopt;
    }
    MultiplayerEvent e;
    e.kind = MultiplayerEvent::Kind::Join;
    e.address = parsed->first;
    e.port = parsed->second;
    e.password = m_join_password;
    return e;
}

void MultiplayerMenu::click(const MultiplayerModel& model, Id id, std::vector<MultiplayerEvent>& out) {
    if (field_text(id) != nullptr) {
        if (id == kAddress && m_password_address) {
            m_password_address.reset();
            m_join_password.clear();
        }
        m_form.set_focus(id);
        return;
    }
    m_form.set_focus(m_screen == Screen::Lobby && !m_explicit_text_input ? std::optional<Id>(kChat) : std::nullopt);
    const auto event = [&](MultiplayerEvent::Kind kind) {
        MultiplayerEvent e;
        e.kind = kind;
        out.push_back(std::move(e));
        return &out.back();
    };
    switch (m_screen) {
        case Screen::Network:
            if(id==kBack&&model.session){m_screen=m_network_from;break;}
            if(id==kInternet||id==kBack)event(MultiplayerEvent::Kind::Internet);
            else if(id==kAdhoc)event(MultiplayerEvent::Kind::Adhoc);
            else if (id == kRadioSearch) {
                m_screen = Screen::Browse;
                event(MultiplayerEvent::Kind::Search);
            } else if (id == kRadioHost) {
                m_screen = Screen::Create;
                m_setup.public_room = false;
                m_setup.lan = true;
                if (m_setup.title.empty())
                    m_setup.title = tr(MpText::DefaultTitle, m_name);
            }
            break;
        case Screen::Browse: {
            const std::vector<BrowseRow> rows = browse_rows(model);
            if (id == kNetwork) {
                show_network();
            } else if (id == kBack) {
                event(MultiplayerEvent::Kind::Close);
            } else if (id == kRefresh) {
                event(MultiplayerEvent::Kind::Search);
            } else if (id == kConnect) {
                if (auto e = join_event(m_address)) {
                    out.push_back(std::move(*e));
                }
            } else if (id == kJoin && m_selected) {
                if (auto e = join_event(*m_selected)) {
                    out.push_back(std::move(*e));
                }
            } else if (id == kCreate) {
                m_screen = Screen::Create;
                if (model.network.mode == net::NetworkMode::Adhoc) {
                    m_setup.public_room = false;
                }
                if (m_setup.title.empty() || m_setup.title == m_auto_title) {
                    // The suggestion follows the name until the person types a title of their own.
                    m_auto_title = tr(MpText::DefaultTitle, m_name);
                    m_setup.title = m_auto_title;
                }
            } else if (id == kBrowseUp) {
                m_browse_first -= std::min(m_browse_first, kBrowseRows);
            } else if (id == kBrowseDown) {
                m_browse_first += kBrowseRows;
            } else if (id >= kBrowseRow && id < kBrowseRow + static_cast<Id>(kBrowseRows)) {
                const std::size_t i = m_browse_first + static_cast<std::size_t>(id - kBrowseRow);
                if (i < rows.size()) {
                    const auto& row = rows[i];
                    const bool same = m_selected == row.address;
                    if (!same)
                        m_join_password.clear();
                    m_password_address.reset();
                    if (row.game && row.game->info.password) {
                        // Selecting a protected room deliberately opens the Vita keyboard.
                        // Keep its target separate from the optional Direct-IP field.
                        m_password_address = row.address;
                        m_form.set_focus(kJoinPassword);
                        m_form.select_all();
                    } else if (same) {
                        if (auto e = join_event(row.address)) {  // a second click joins
                            out.push_back(std::move(*e));
                        }
                    }
                    m_selected = row.address;
                }
            }
            break;
        }
        case Screen::Create: {
            using Kind = HostSetup::Kind;
            if (id == kCreateBack) {
                m_screen = Screen::Browse;
            } else if (id == kKindEndless || id == kKindScenario || id == kKindSavegame) {
                const Kind kind = id == kKindEndless    ? Kind::Endless
                                  : id == kKindScenario ? Kind::Scenario
                                                        : Kind::Savegame;
                if (kind != m_setup.kind) {
                    m_setup.kind = kind;
                    m_chosen.reset();
                    m_list_first = 0;
                }
            } else if (id >= kLevel && id < kLevel + static_cast<Id>(sim::NewGameOptions::kDifficulties)) {
                m_setup.level = static_cast<std::uint8_t>(id - kLevel);
            } else if (id == kFewerPlayers && m_setup.players > HostSetup::kMinPlayers) {
                --m_setup.players;
            } else if (id == kMorePlayers && m_setup.players < sim::kMaxPlayers) {
                ++m_setup.players;
            } else if (id == kRoll) {
                m_setup.seed = random_seed();
                m_seed_text = std::to_string(m_setup.seed);
            } else if (id == kPublic) {
                m_setup.public_room = !m_setup.public_room;
            } else if (id == kLan) {
                m_setup.lan = !m_setup.lan;
            } else if (id == kJoinRunning) {
                m_setup.join_running = !m_setup.join_running;
            } else if (id == kSpectators) {
                m_setup.spectators = !m_setup.spectators;
            } else if (id == kSpectatorChat) {
                m_setup.spectators_chat = !m_setup.spectators_chat;
            } else if (id == kTakeOver) {
                m_setup.computer_takes_over = !m_setup.computer_takes_over;
            } else if (id == kHostPauses) {
                m_setup.host_pauses_only = !m_setup.host_pauses_only;
            } else if (id == kDedicated) {
                m_setup.dedicated = !m_setup.dedicated;
            } else if (id == kSettings && m_setup.kind != Kind::Savegame) {
                open_settings(m_setup.game_settings(), m_setup.scope(), GameSettingsMenu::Mode::Apply);
            } else if (id == kListUp) {
                m_list_first -= std::min(m_list_first, kListRows);
            } else if (id == kListDown) {
                m_list_first += kListRows;
            } else if (id >= kListRow && id < kListRow + static_cast<Id>(kListRows)) {
                const std::size_t i = m_list_first + static_cast<std::size_t>(id - kListRow);
                if (m_setup.kind == Kind::Scenario && i < model.scenarios.size() && !model.scenarios[i].separator) {
                    m_chosen = i;
                } else if (m_setup.kind == Kind::Savegame && i < model.savegames.size()) {
                    m_chosen = model.savegames[i].first;
                }
            } else if (id == kOpenLobby) {
                HostSetup setup = m_setup;
                setup.seed = number(m_seed_text).value_or(m_setup.seed);
                setup.port = static_cast<std::uint16_t>(number(m_port_text).value_or(net::kDefaultPort));
                if (setup.title.empty()) {
                    setup.title = tr(MpText::DefaultTitle, m_name);
                }
                if (m_setup.kind == Kind::Scenario && m_chosen && *m_chosen < model.scenarios.size()) {
                    const MenuScenario& s = model.scenarios[*m_chosen];
                    setup.file = s.files.empty() ? std::filesystem::path{} : s.files.front().second;
                    setup.file_name = s.name;
                } else if (m_setup.kind == Kind::Savegame && m_chosen) {
                    setup.slot = *m_chosen;
                    for (const auto& [slot, name] : model.savegames) {
                        if (slot == *m_chosen) {
                            setup.file_name = name;
                        }
                    }
                } else if (m_setup.kind == Kind::Endless) {
                    std::string level = setup.level < model.level_names.size()
                                            ? model.level_names[setup.level]
                                            : tr(MpText::LevelN, std::to_string(setup.level + 1));
                    setup.file_name = tr(MpText::EndlessGame) + ", " + level + ", " +
                                      tr(MpText::PlayerCount, std::to_string(setup.players)) + ", " +
                                      tr(MpText::WorldNumber) + " " + std::to_string(setup.seed);
                }
                event(MultiplayerEvent::Kind::Host)->setup = std::move(setup);
            }
            break;
        }
        case Screen::Lobby: {
            if(id==kNetwork){show_network();break;}
            const net::Session* session = model.session;
            if (id == kLeave) {
                event(MultiplayerEvent::Kind::Leave);
            } else if (id == kReady) {
                m_ready = !m_ready;
                event(MultiplayerEvent::Kind::Ready)->ready = m_ready;
            } else if (id == kStart) {
                event(MultiplayerEvent::Kind::Start);
            } else if (id == kSettings && session != nullptr) {
                if (const auto settings = read_lobby_setup(session->lobby().setup)) {
                    const bool change = model.hosting && !session->lobby().started;
                    open_settings(settings->first, settings->second,
                                  change ? GameSettingsMenu::Mode::Apply : GameSettingsMenu::Mode::View);
                }
            } else if (id == kWatch) {
                event(MultiplayerEvent::Kind::TakeSeat)->seat = sim::kNoPlayer;
                m_ready = false;
            } else if (id >= kSeatTake && id < kSeatTake + static_cast<Id>(kMaxSeats)) {
                event(MultiplayerEvent::Kind::TakeSeat)->seat = static_cast<sim::PlayerId>(id - kSeatTake);
                m_ready = false;
            } else if (id >= kWatcherKick && id < kWatcherKick + static_cast<Id>(m_watchers.size())) {
                // The entry as shown: the host kicks it only if the roster still has it there (MS-18).
                const auto& [member, name] = m_watchers[static_cast<std::size_t>(id - kWatcherKick)];
                MultiplayerEvent* e = event(MultiplayerEvent::Kind::KickMember);
                e->member = member;
                e->seat = sim::kNoPlayer;
                e->text = name;
            } else if (id >= kSeatBanner && id < kSeatBanner + static_cast<Id>(kMaxSeats)) {
                const auto s = static_cast<std::size_t>(id - kSeatBanner);
                m_banner_seat = m_banner_seat == s ? std::nullopt : std::optional(s);
            } else if (id >= kBannerChoice && id < kBannerChoice + static_cast<Id>(m_banner_choices.size()) &&
                       m_banner_seat) {
                MultiplayerEvent* e = event(MultiplayerEvent::Kind::SetBanner);
                e->seat = static_cast<sim::PlayerId>(*m_banner_seat);
                e->banner = m_banner_choices[static_cast<std::size_t>(id - kBannerChoice)];
                m_banner_seat.reset();
            } else if (id >= kSeatKick && id < kSeatKick + static_cast<Id>(kMaxSeats)) {
                event(MultiplayerEvent::Kind::Kick)->seat = static_cast<sim::PlayerId>(id - kSeatKick);
            } else if (id >= kSeatKind && id < kSeatKind + static_cast<Id>(kMaxSeats) && session != nullptr) {
                const auto s = static_cast<std::size_t>(id - kSeatKind);
                if (s < session->lobby().seats.size()) {
                    using SeatKind = net::LobbySeat::Kind;
                    const SeatKind now = session->lobby().seats[s].kind;
                    MultiplayerEvent* e = event(MultiplayerEvent::Kind::SetSeat);
                    e->seat = static_cast<sim::PlayerId>(s);
                    if (may_block_seats(session->lobby())) {
                        // The computer plays an open slot anyway: the other choice is nobody, and
                        // the world and what scales with it stay those of all the slots.
                        e->seat_kind = now == SeatKind::Open ? SeatKind::Closed : SeatKind::Open;
                    } else {
                        // A scenario's or savegame's players are in its world: a person's or the
                        // computer's, never nobody's.
                        e->seat_kind = now == SeatKind::Open ? SeatKind::Computer : SeatKind::Open;
                    }
                }
            }
            break;
        }
        case Screen::Connecting:
            if (id == kCancel) {
                event(MultiplayerEvent::Kind::Leave);
            }
            break;
        case Screen::Settings:
            break;  // the start dialog answers in mouse()
    }
}

std::optional<MultiplayerEvent> MultiplayerMenu::back() {
    m_form.set_focus(std::nullopt);
    MultiplayerEvent e;
    switch (m_screen) {
        case Screen::Network:
            if(m_network_from==Screen::Lobby){m_screen=Screen::Lobby;return std::nullopt;}
            e.kind=MultiplayerEvent::Kind::Internet;
            return e;
        case Screen::Browse:
            e.kind = MultiplayerEvent::Kind::Close;
            return e;
        case Screen::Create:
            m_screen = Screen::Browse;
            return std::nullopt;
        case Screen::Lobby:
            if (m_banner_seat) {
                m_banner_seat.reset();  // the colours close first
                return std::nullopt;
            }
            e.kind = MultiplayerEvent::Kind::Leave;
            return e;
        case Screen::Connecting:
            e.kind = MultiplayerEvent::Kind::Leave;
            return e;
        case Screen::Settings:
            if (!m_settings.back()) {  // from the victory conditions to the settings first
                close_settings();
            }
            return std::nullopt;
    }
    return std::nullopt;
}

void MultiplayerMenu::close_settings() {
    m_screen = m_settings_from;
    if (m_screen == Screen::Lobby) {
        m_form.set_focus(m_explicit_text_input ? std::nullopt : std::optional<Id>(kChat));
    }
}

std::string* MultiplayerMenu::field_text(Id id) {
    switch (id) {
        case kName:
            return m_screen == Screen::Browse ? &m_name : nullptr;
        case kAddress:
            return m_screen == Screen::Browse ? &m_address : nullptr;
        case kJoinPassword:
            return m_screen == Screen::Browse ? &m_join_password : nullptr;
        case kSeed:
            return m_screen == Screen::Create ? &m_seed_text : nullptr;
        case kTitle:
            return m_screen == Screen::Create ? &m_setup.title : nullptr;
        case kPassword:
            return m_screen == Screen::Create ? &m_setup.password : nullptr;
        case kPort:
            return m_screen == Screen::Create ? &m_port_text : nullptr;
        case kChat:
            return m_screen == Screen::Lobby ? &m_chat : nullptr;
        default:
            return nullptr;
    }
}

std::size_t MultiplayerMenu::field_limit(Id id) const {
    switch (id) {
        case kName:
            return net::kMaxNameLength;
        case kAddress:
            return kMaxAddress;
        case kJoinPassword:
        case kPassword:
            return net::kMaxPasswordLength;
        case kTitle:
            return net::kMaxTitleLength;
        case kSeed:
        case kPort:
            return kMaxNumber;
        case kChat:
            return net::kMaxChatLength;
        default:
            return 0;
    }
}

void MultiplayerMenu::type(std::string_view text) {
    const auto focus = m_form.focus();
    std::string* s = focus ? field_text(*focus) : nullptr;
    if (s == nullptr) {
        return;
    }
    const bool digits = *focus == kSeed || *focus == kPort;
    if (!text.empty() && m_form.take_all_selected()) {
        s->clear();  // typed over the whole text (Ctrl+A)
    }
    for (const char c : text) {
        const bool ok = digits ? (c >= '0' && c <= '9') : static_cast<unsigned char>(c) >= 0x20;
        if (ok && s->size() < field_limit(*focus)) {
            s->push_back(c);
        }
    }
}

std::vector<MultiplayerEvent> MultiplayerMenu::key(const MultiplayerModel& model, int key) {
    std::vector<MultiplayerEvent> out;
    const auto focus = m_form.focus();
    std::string* s = focus ? field_text(*focus) : nullptr;
    if (s == nullptr) {
        m_form.set_focus(std::nullopt);
        return out;
    }
    if (key == 1) {
        m_form.select_all();  // Ctrl+A
        return out;
    }
    const bool all = m_form.take_all_selected();
    switch (key) {
        case 8:
            if (all) {
                s->clear();
            } else if (!s->empty()) {
                s->pop_back();
            }
            break;
        case 9: {
            // The next field of the screen.
            constexpr std::array<Id, 8> kOrder = {kName,     kAddress, kJoinPassword, kTitle,
                                                  kPassword, kSeed,    kPort,         kChat};
            const auto it = std::find(kOrder.begin(), kOrder.end(), *focus);
            for (std::size_t n = 1; n <= kOrder.size(); ++n) {
                const Id next = kOrder[(static_cast<std::size_t>(it - kOrder.begin()) + n) % kOrder.size()];
                if (field_text(next) != nullptr) {
                    m_form.set_focus(next);
                    break;
                }
            }
            break;
        }
        case 13:
            if (*focus == kChat) {
                if (!m_chat.empty()) {
                    MultiplayerEvent e;
                    e.kind = MultiplayerEvent::Kind::Say;
                    e.text = std::exchange(m_chat, {});
                    out.push_back(std::move(e));
                }
                if (m_explicit_text_input) { m_form.set_focus(std::nullopt); }
                break;
            }
            if (*focus == kName) {
                MultiplayerEvent e;
                e.kind = MultiplayerEvent::Kind::Name;
                e.text = m_name;
                out.push_back(std::move(e));
            } else if (*focus == kAddress || *focus == kJoinPassword) {
                const auto& address = *focus == kJoinPassword && m_password_address ? *m_password_address : m_address;
                if (auto e = join_event(address)) {
                    out.push_back(std::move(*e));
                }
            }
            m_form.set_focus(std::nullopt);
            break;
        case 27:
            if (*focus == kJoinPassword && m_password_address) {
                m_join_password.clear();
                m_password_address.reset();
            }
            if (m_explicit_text_input && *focus == kChat) { m_chat.clear(); }
            m_form.set_focus(std::nullopt);
            break;
        default:
            break;
    }
    if (focus == kName && key != 13 && m_form.focus() != kName) {
        // Left the name field another way: the name counts all the same.
        MultiplayerEvent e;
        e.kind = MultiplayerEvent::Kind::Name;
        e.text = m_name;
        out.push_back(std::move(e));
    }
    (void)model;
    return out;
}

}  // namespace o1602::app
