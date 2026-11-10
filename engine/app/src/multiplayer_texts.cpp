// SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_texts.hpp"

#include <algorithm>
#include <array>
#include <string>

#include "o1602/core/text.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

struct Entry {
    MpText id;
    const char* key;
};

// In the order of MpText.
constexpr std::array kTexts = {
    Entry{MpText::Multiplayer, "net.multiplayer"},
    Entry{MpText::YourName, "net.your_name"},
    Entry{MpText::Games, "net.games"},
    Entry{MpText::ColGame, "net.col_game"},
    Entry{MpText::ColHost, "net.col_host"},
    Entry{MpText::ColPlayers, "net.col_players"},
    Entry{MpText::ColState, "net.col_state"},
    Entry{MpText::ColPing, "net.col_ping"},
    Entry{MpText::StateLobby, "net.state_lobby"},
    Entry{MpText::StateRunning, "net.state_running"},
    Entry{MpText::StateRunningJoin, "net.state_running_join"},
    Entry{MpText::StateRunningWatch, "net.state_running_watch"},
    Entry{MpText::StateOtherVersion, "net.state_other_version"},
    Entry{MpText::NoGamesFound, "net.no_games_found"},
    Entry{MpText::SearchHint, "net.search_hint"},
    Entry{MpText::ConnectDirectly, "net.connect_directly"},
    Entry{MpText::HostAddress, "net.host_address"},
    Entry{MpText::Password, "net.password"},
    Entry{MpText::Connect, "net.connect"},
    Entry{MpText::CreateGame, "net.create_game"},
    Entry{MpText::HostHint, "net.host_hint"},
    Entry{MpText::Back, "net.back"},
    Entry{MpText::Refresh, "net.refresh"},
    Entry{MpText::Join, "net.join"},
    Entry{MpText::CreateTitle, "net.create_title"},
    Entry{MpText::NewEndless, "net.new_endless"},
    Entry{MpText::Scenario, "net.scenario"},
    Entry{MpText::Savegame, "net.savegame"},
    Entry{MpText::Level, "net.level"},
    Entry{MpText::WorldNumber, "net.world_number"},
    Entry{MpText::Roll, "net.roll"},
    Entry{MpText::Players, "net.players"},
    Entry{MpText::PlayerCount, "net.player_count"},
    Entry{MpText::EndlessHint, "net.endless_hint"},
    Entry{MpText::ScenarioHint, "net.scenario_hint"},
    Entry{MpText::SavegameHint, "net.savegame_hint"},
    Entry{MpText::NoSavegames, "net.no_savegames"},
    Entry{MpText::Session, "net.session"},
    Entry{MpText::GameTitle, "net.game_title"},
    Entry{MpText::PasswordOptional, "net.password_optional"},
    Entry{MpText::ShowOnLan, "net.show_on_lan"},
    Entry{MpText::AllowJoinRunning, "net.allow_join_running"},
    Entry{MpText::AllowSpectators, "net.allow_spectators"},
    Entry{MpText::ComputerTakesOver, "net.computer_takes_over"},
    Entry{MpText::HostPausesOnly, "net.host_pauses_only"},
    Entry{MpText::HostOnly, "net.host_only"},
    Entry{MpText::Port, "net.port"},
    Entry{MpText::OpenLobby, "net.open_lobby"},
    Entry{MpText::DefaultTitle, "net.default_title"},
    Entry{MpText::EndlessGame, "net.endless_game"},
    Entry{MpText::LevelN, "net.level_n"},
    Entry{MpText::Watchers, "net.watchers"},
    Entry{MpText::Nobody, "net.nobody"},
    Entry{MpText::SeatOpen, "net.seat_open"},
    Entry{MpText::SeatComputer, "net.seat_computer"},
    Entry{MpText::SeatBlocked, "net.seat_blocked"},
    Entry{MpText::Ready, "net.ready"},
    Entry{MpText::Choosing, "net.choosing"},
    Entry{MpText::Free, "net.free"},
    Entry{MpText::Take, "net.take"},
    Entry{MpText::Kick, "net.kick"},
    Entry{MpText::You, "net.you"},
    Entry{MpText::Host, "net.host"},
    Entry{MpText::Chat, "net.chat"},
    Entry{MpText::ChatHint, "net.chat_hint"},
    Entry{MpText::Leave, "net.leave"},
    Entry{MpText::Start, "net.start"},
    Entry{MpText::StartHint, "net.start_hint"},
    Entry{MpText::WaitHostStart, "net.wait_host_start"},
    Entry{MpText::Watch, "net.watch"},
    Entry{MpText::Yes, "net.yes"},
    Entry{MpText::No, "net.no"},
    Entry{MpText::Connecting, "net.connecting"},
    Entry{MpText::PlayerJoined, "net.player_joined"},
    Entry{MpText::PlayerLeft, "net.player_left"},
    Entry{MpText::ComputerTookOver, "net.computer_took_over"},
    Entry{MpText::Lagging, "net.lagging"},
    Entry{MpText::Desync, "net.desync"},
    Entry{MpText::Waiting, "net.waiting"},
    Entry{MpText::HostPort, "net.host_port"},
    Entry{MpText::CannotHost, "net.cannot_host"},
    Entry{MpText::CannotConnect, "net.cannot_connect"},
    Entry{MpText::StartFailed, "net.start_failed"},
    Entry{MpText::HostLeftSaved, "net.host_left_saved"},
    Entry{MpText::HostMoving, "net.host_moving"},
    Entry{MpText::HostMoved, "net.host_moved"},
    Entry{MpText::HostMovedYou, "net.host_moved_you"},
    Entry{MpText::ChooseBanner, "net.choose_banner"},
    Entry{MpText::Dropped, "net.dropped"},
    Entry{MpText::Reconnecting, "net.reconnecting"},
    Entry{MpText::PauseRefused, "net.pause_refused"},
    Entry{MpText::AllowSpectatorChat, "net.allow_spectator_chat"},
    Entry{MpText::ChatAll, "net.chat_all"},
    Entry{MpText::ChatClose, "net.chat_close"},
    Entry{MpText::ChatToLabel, "net.chat_to_label"},
    Entry{MpText::ChatToYou, "net.chat_to_you"},
    Entry{MpText::ChatToYouAnd, "net.chat_to_you_and"},
    Entry{MpText::ChatTo, "net.chat_to"},
    Entry{MpText::ChatHintAll, "net.chat_hint_all"},
    Entry{MpText::ChatHintPrivate, "net.chat_hint_private"},
    Entry{MpText::ChatReadOnly, "net.chat_read_only"},
    Entry{MpText::ChatMissed, "net.chat_missed"},
    Entry{MpText::PackageKept, "net.package_kept"},
    Entry{MpText::GameWaiting, "net.game_waiting"},
    Entry{MpText::GameReceiving, "net.game_receiving"},
    Entry{MpText::GameBytes, "net.game_bytes"},
    Entry{MpText::GameLoading, "net.game_loading"},
};

constexpr bool in_order() {
    for (std::size_t i = 0; i < kTexts.size(); ++i) {
        if (static_cast<std::size_t>(kTexts[i].id) != i) {
            return false;
        }
    }
    return true;
}
static_assert(in_order(), "kTexts must list every MpText in order");
static_assert(kTexts.size() == static_cast<std::size_t>(MpText::GameLoading) + 1, "a text is missing");

/// "lighthouse 1.2, weather" in the fonts' Windows-1252.
std::string package_names(std::span<const assets::ContentPackage> packages) {
    std::string names;
    for (const assets::ContentPackage& p : packages) {
        names += (names.empty() ? "" : ", ") + core::utf8_to_cp1252(assets::package_label(p));
    }
    return names;
}

}  // namespace

std::string_view mp_key(MpText id) noexcept {
    return kTexts[static_cast<std::size_t>(id)].key;
}

std::string mp_text(MpText id) {
    return ui::tr(mp_key(id));
}

std::string mp_log(MpText id, std::string_view value) {
    std::string out(mp_key(id));
    if (!value.empty()) {
        out += " " + core::cp1252_to_utf8(value);
    }
    return out;
}

std::string mp_text(MpText id, std::string_view value) {
    return ui::tr(mp_key(id), {value});
}

std::string mp_text(MpText id, std::string_view first, std::string_view second) {
    return ui::tr(mp_key(id), {first, second});
}

std::string mp_text(net::EndReason reason) {
    switch (reason) {
        case net::EndReason::None:
            return {};
        case net::EndReason::ConnectFailed:
            return ui::tr("net.end.connect_failed");
        case net::EndReason::HostLeft:
            return ui::tr("net.end.host_left");
        case net::EndReason::Refused:
            return ui::tr("net.end.refused");
        case net::EndReason::ProtocolError:
            return ui::tr("net.end.protocol_error");
        case net::EndReason::LoadFailed:
            return ui::tr("net.end.load_failed");
        case net::EndReason::Desync:
            return ui::tr("net.end.desync");
    }
    return {};
}

std::string mp_text(net::RefuseReason reason, std::span<const assets::ContentPackage> packages) {
    switch (reason) {
        case net::RefuseReason::Version:
            return ui::tr("net.refused.version");
        case net::RefuseReason::Content:
            return ui::tr("net.refused.content");
        case net::RefuseReason::Full:
            return ui::tr("net.refused.full");
        case net::RefuseReason::Closed:
            return ui::tr("net.refused.closed");
        case net::RefuseReason::Desync:
            return ui::tr("net.refused.desync");
        case net::RefuseReason::Invalid:
            return ui::tr("net.refused.invalid");
        case net::RefuseReason::Password:
            return ui::tr("net.refused.password");
        case net::RefuseReason::Kicked:
            return ui::tr("net.refused.kicked");
        case net::RefuseReason::Taken:
            return ui::tr("net.refused.taken");
        case net::RefuseReason::Packages:
            return ui::tr("net.refused.packages", {package_names(packages)});
    }
    return {};
}

std::string mp_size(std::uint64_t bytes) {
    constexpr std::uint64_t kKb = 1024;
    constexpr std::uint64_t kMb = kKb * kKb;
    if (bytes < kMb) {
        return ui::tr("net.size_kb", {std::to_string(std::max<std::uint64_t>((bytes + kKb - 1) / kKb, 1))});
    }
    const std::uint64_t tenths = (bytes * 10 + kMb / 2) / kMb;
    return ui::tr("net.size_mb", {std::to_string(tenths / 10), std::to_string(tenths % 10)});
}

std::string mp_package_offer(std::span<const assets::ContentPackage> packages, std::uint64_t bytes) {
    return ui::tr("net.fetch_question", {package_names(packages), mp_size(bytes)});
}

}  // namespace o1602::app
