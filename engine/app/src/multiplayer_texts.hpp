// SPDX-License-Identifier: GPL-3.0-or-later
//
// The texts of the multiplayer menu and messages, which the original's Text.cod does not have: keys
// "net.*" of Open1602's catalogues (data/locale), in the language of the installation
// (ui/localization.hpp). mp_text() gives them in the fonts' Windows-1252.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "o1602/assets/game_content.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/net/session.hpp"

namespace o1602::app {

enum class MpText : std::uint16_t {
    Multiplayer,
    YourName,
    Games,
    ColGame,
    ColHost,
    ColPlayers,
    ColState,
    ColPing,
    StateLobby,
    StateRunning,
    StateRunningJoin,
    StateRunningWatch,
    StateOtherVersion,
    NoGamesFound,
    SearchHint,
    ConnectDirectly,
    HostAddress,
    Password,
    Connect,
    CreateGame,
    HostHint,
    Back,
    Refresh,
    Join,
    CreateTitle,
    NewEndless,
    Scenario,
    Savegame,
    Level,
    WorldNumber,
    Roll,
    Players,
    PlayerCount,
    EndlessHint,
    ScenarioHint,
    SavegameHint,
    NoSavegames,
    Session,
    GameTitle,
    PasswordOptional,
    ShowOnLan,
    AllowJoinRunning,
    AllowSpectators,
    ComputerTakesOver,
    HostControlsSpeed,
    HostOnly,
    Port,
    OpenLobby,
    DefaultTitle,
    EndlessGame,
    LevelN,
    Watchers,
    Nobody,
    SeatOpen,
    SeatComputer,
    SeatBlocked,
    Ready,
    Choosing,
    Free,
    Take,
    Kick,
    You,
    Host,
    Chat,
    ChatHint,
    Leave,
    Start,
    StartHint,
    WaitHostStart,
    Watch,
    Yes,
    No,
    Connecting,
    PlayerJoined,
    PlayerLeft,
    ComputerTookOver,
    Lagging,
    Desync,
    Waiting,
    HostPort,
    CannotHost,
    CannotConnect,
    StartFailed,
    HostLeftSaved,
    HostMoving,
    HostMoved,
    HostMovedYou,
    ChooseBanner,
    Dropped,
    Reconnecting,
    SpeedRefused,
    AllowSpectatorChat,
    ChatAll,
    ChatClose,
    ChatToLabel,
    ChatToYou,
    ChatToYouAnd,
    ChatTo,
    ChatHintAll,
    ChatHintPrivate,
    ChatReadOnly,
    ChatMissed,
    PackageKept,
    GameWaiting,
    GameReceiving,
    GameBytes,
    GameLoading,
    Starting,
    StartingHint,
    StartingCount,
    LoadWaiting,
    LoadPercent,
    LoadLoading,
    Spectator,
    StartNow,
};

/// The catalogue key of a text ("net.player_joined").
[[nodiscard]] std::string_view mp_key(MpText id) noexcept;
[[nodiscard]] std::string mp_text(MpText id);
/// The text for the log: its key and `value` (UTF-8), English whatever the game's language (LC-24).
[[nodiscard]] std::string mp_log(MpText id, std::string_view value = {});
/// The text with its "{1}" replaced by `value` (a name, a number).
[[nodiscard]] std::string mp_text(MpText id, std::string_view value);
[[nodiscard]] std::string mp_text(MpText id, std::string_view first, std::string_view second);
/// Why a session ended or a computer was refused, for the player.
[[nodiscard]] std::string mp_text(net::EndReason reason);
/// `packages` (RefuseReason::Packages): the packages of the game this computer lacks.
[[nodiscard]] std::string mp_text(net::RefuseReason reason, std::span<const assets::ContentPackage> packages = {});
/// "2.4 MB", "640 KB": the size of packages a host would send.
[[nodiscard]] std::string mp_size(std::uint64_t bytes);
/// The question whether to download `packages` (`bytes` together) from the host.
[[nodiscard]] std::string mp_package_offer(std::span<const assets::ContentPackage> packages, std::uint64_t bytes);

}  // namespace o1602::app
