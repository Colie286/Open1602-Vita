// SPDX-License-Identifier: GPL-3.0-or-later
//
// The multiplayer menu (ADR 0011): Open1602's own, the original's network screens are not used.
// Three screens over the whole window: finding a game (the local network searched by itself,
// remembered hosts, an address typed in), creating one (an endless game, a scenario or a
// savegame, and the session's settings) and the lobby (slots, ready, chat, start). The menu shows
// what the viewer puts into the MultiplayerModel and answers with MultiplayerEvents; the session
// and the games are the viewer's.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "app_settings.hpp"
#include "endless_setup.hpp"
#include "game_settings_menu.hpp"
#include "main_menu.hpp"
#include "multiplayer_texts.hpp"

#include "o1602/net/lan.hpp"
#include "o1602/net/session.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/ui/form.hpp"

namespace o1602::app {

/// The game a host sets up.
struct HostSetup {
    enum class Kind : std::uint8_t { Endless, Scenario, Savegame };
    Kind kind = Kind::Endless;
    std::uint8_t level = 1;  ///< endless game: the difficulty, 0..3 ([ENDLESSKIND])
    std::uint32_t seed = 0;  ///< endless game: the world's number
    /// Endless game: players with a start (kMinPlayers..sim::kMaxPlayers); the computer plays the
    /// slots no person takes. Four play on the original's maps; more get a world of their own,
    /// sized for them.
    std::uint8_t players = 4;
    static constexpr std::uint8_t kMinPlayers = 4;
    std::filesystem::path file;  ///< scenario
    std::size_t slot = 0;        ///< savegame: its slot
    std::string file_name;       ///< its name for the lobby (Windows-1252)
    std::string title;           ///< the game's name
    std::string password;
    bool lan = true;  ///< answer searches on the local network
    bool join_running = true;
    bool spectators = true;
    bool spectators_chat = true;       ///< spectators may write in the chat (they always read it)
    bool computer_takes_over = true;   ///< the computer plays on for a person who left
    bool host_controls_speed = false;  ///< only the host pauses, resumes and sets the speed (MS-20)
    bool dedicated = false;            ///< the host plays no slot
    std::uint16_t port = net::kDefaultPort;
    /// The game's settings (ADR 0008): rules, and for an endless game its world; a scenario uses
    /// the rules only, a savegame keeps its own.
    EndlessSetup settings = EndlessSetup::defaults(SetupScope::MultiplayerEndless);

    /// What the settings are for.
    [[nodiscard]] SetupScope scope() const noexcept;
    /// The settings an endless game really gets, with the players of its lobby.
    /// Victory conditions only for an endless game.
    [[nodiscard]] EndlessSetup game_settings() const;
};

/// The host's settings as the lobby passes them on (net::Lobby::setup, which the session does not
/// read): what is played and its settings, as "key = value" text.
/// A new endless game's also its world number (`settings.world_seed`) and `level`.
[[nodiscard]] std::vector<std::uint8_t> lobby_setup(const EndlessSetup& settings, SetupScope scope,
                                                    std::uint8_t level = 1);
/// The settings of a lobby's setup; nothing if it has none or not in this form.
[[nodiscard]] std::optional<std::pair<EndlessSetup, SetupScope>> read_lobby_setup(std::span<const std::uint8_t> bytes);
/// The level of a new endless game in a lobby's setup (1 if it names none).
[[nodiscard]] std::uint8_t read_lobby_level(std::span<const std::uint8_t> bytes);
/// Whether the host may block a slot of `lobby` (nobody plays it, the world is still made for all
/// the slots): only in a new endless game before it starts. A scenario's or savegame's players
/// are in its world already.
[[nodiscard]] bool may_block_seats(const net::Lobby& lobby);

/// A line of the chat, or a notice of the session (someone came or went).
struct ChatLine {
    std::string name;  ///< empty for a notice
    std::string text;
    sim::PlayerId seat = sim::kNoPlayer;
    std::uint64_t time_ms = 0;  ///< when it came (real time)
    /// Said privately (net::Chat::everyone off); `to` names to whom ("an Euch", "an Anna, Bernd").
    bool whisper = false;
    std::string to;
    /// Counts up with each line added (add_chat_line), so a chat scrolled back holds still while
    /// new lines come in; 0 when not counted.
    std::uint64_t serial = 0;
};

/// What goes before a line's text: "Anna: ", "Anna (an Euch): "; nothing for a notice.
[[nodiscard]] std::string chat_prefix(const ChatLine& line);

/// What the menu shows; the viewer keeps it up to date.
struct MultiplayerModel {
    std::string name;                                            ///< the player's name
    std::vector<net::FoundGame> found;                           ///< games that answered the search
    std::vector<std::string> level_names;                        ///< [ENDLESSKIND]
    std::vector<MenuScenario> scenarios;                         ///< the scenario list of the main menu
    std::vector<std::pair<std::size_t, std::string>> savegames;  ///< slot and name
    EndlessSetup settings = EndlessSetup::defaults(SetupScope::MultiplayerEndless);  ///< the last a host chose
    const net::Session* session = nullptr;  ///< the session, once hosting or joining
    bool hosting = false;
    bool everyone_ready = false;             ///< (host) every other player is ready
    std::uint16_t port = net::kDefaultPort;  ///< (host) the game's port
    std::deque<ChatLine> chat;               ///< newest last
    /// (joining) How far the game from the host has come, and the bytes of it so far and in all.
    net::ClientSession::JoinStage join = net::ClientSession::JoinStage::None;
    std::uint64_t join_received = 0;
    std::uint64_t join_total = 0;
};

struct MultiplayerEvent {
    enum class Kind : std::uint8_t {
        Close,       ///< back to the main menu
        Search,      ///< look for games again
        Join,        ///< connect to `address`:`port` with `password`
        Host,        ///< open a lobby for `setup`
        Leave,       ///< out of the session (back to the search)
        Start,       ///< (host) start the game
        TakeSeat,    ///< take slot `seat` (kNoPlayer: watch)
        SetSeat,     ///< (host) slot `seat` becomes `seat_kind`
        Kick,        ///< (host) send the player of `seat` away
        KickMember,  ///< (host) send the spectator of roster entry `member` (`seat`, `text` as shown) away
        Ready,       ///< ready (`ready`) or not
        Say,         ///< chat `text`
        Name,        ///< the player's name changed to `text`
        SetBanner,   ///< slot `seat` gets `banner` (the own slot; the host also the computer's and open ones)
        Settings,    ///< (host, in the lobby) the game's settings are now `setup.settings`
        StartNow,    ///< (host) stop waiting for the players still loading the game at the start
    };
    Kind kind = Kind::Close;
    std::string address{};
    std::uint16_t port = net::kDefaultPort;
    std::string password{};
    HostSetup setup{};
    sim::PlayerId seat = sim::kNoPlayer;
    std::size_t member = 0;
    net::LobbySeat::Kind seat_kind = net::LobbySeat::Kind::Open;
    bool ready = false;
    std::string text{};
    sim::BannerId banner = 0;
};

/// A banner as the lobby shows it: its colour in the palette and its id (render::Banner::id), whose
/// name after the colon names its text ("anno1602:red": "net.colour.red").
struct MenuBanner {
    std::int32_t colour = 0;  ///< a palette colour or exactly the banner's (ui::rgb_ink)
    std::string id;
    /// The player's name in the chat: the banner's colour, lightened where it would be hard to
    /// read on the menus' dark ground (a dark blue): a palette colour or a colour of its own
    /// (ui::rgb_ink).
    std::int32_t text = 0;
};

/// The lobby slots of an endless game for `players` (clamped to HostSetup::kMinPlayers..
/// sim::kMaxPlayers): the four players' slots, then those of players five on behind the free
/// trader, the pirates and the natives (sim::player_slot). The factions' slots are reserved, so
/// no person and no computer player ever takes them.
/// Each slot has the banner of its number, as a new world gives its players (sim::add_players).
[[nodiscard]] std::vector<net::LobbySeat> endless_seats(std::size_t players);

/// The banners players choose from in a lobby (net::Lobby::banners): the original's four, then the
/// further colours (render::BannerTable::add_more_players), one for each of sim::kMaxPlayers.
[[nodiscard]] std::vector<sim::BannerId> player_banners();

/// The name of a banner in the game's language ("Red", "Rot"): the text of its colour, else the id
/// after the colon (a mod's banner without a text).
[[nodiscard]] std::string banner_name(std::string_view id);

/// "name", "name:port", "1.2.3.4:port": the host and its port (kDefaultPort if none is given);
/// nothing for an empty or malformed address.
[[nodiscard]] std::optional<std::pair<std::string, std::uint16_t>> parse_address(std::string_view text);

class MultiplayerMenu {
public:
    enum class Screen : std::uint8_t { Browse, Create, Lobby, Connecting, Settings };

    explicit MultiplayerMenu(ui::FormStyle style);

    [[nodiscard]] Screen screen() const noexcept { return m_screen; }
    /// The search screen; `notice` in red below the list (why a session ended).
    void open(std::string notice = {});
    void show_connecting(std::string address);
    void show_lobby();
    void set_scale(double scale) {
        m_scale = scale > 0.0 ? scale : 1.0;
        m_form.set_scale(scale);
        m_settings.set_scale(scale);
    }
    /// What makes the preview of the islands of a new endless game, and its colours
    /// (GameSettingsMenu).
    void set_preview_maker(GameSettingsMenu::PreviewMaker maker) { m_settings.set_preview_maker(std::move(maker)); }
    void set_preview_colours(const GameSettingsMenu::PreviewColours& colours) {
        m_settings.set_preview_colours(colours);
    }
    /// The start dialog over the screens (for the tests: its preview).
    [[nodiscard]] GameSettingsMenu& settings_dialog() noexcept { return m_settings; }
    /// The banners by id (render::BannerTable): a slot shows the colour of its banner
    /// (net::LobbySeat::banner); `other` for a banner beyond them.
    void set_banners(std::vector<MenuBanner> banners, std::uint8_t other) {
        m_banners = std::move(banners);
        m_other_banner = other;
    }
    /// The slot whose banner is being chosen (the colours shown in place of the chat).
    [[nodiscard]] std::optional<std::size_t> choosing_banner() const noexcept { return m_banner_seat; }

    /// Everything to draw for the model, relative to the 1024 x 768 design's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const MultiplayerModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The right button only reaches the start dialog's settings.
    std::vector<MultiplayerEvent> mouse(const MultiplayerModel& model, std::int32_t x, std::int32_t y, bool down,
                                        ui::MouseButton button = ui::MouseButton::Left);
    /// The mouse wheel (`notches` up positive): the games found, the scenarios or savegames of a
    /// game being created, or the lobby's chat, a row per notch. False on a screen without a list.
    bool wheel(const MultiplayerModel& model, std::int32_t notches);
    /// Escape: one screen back. Close on the search screen, Leave in a session; nothing when the
    /// menu went back by itself (from creating a game to the search).
    std::optional<MultiplayerEvent> back();
    /// The name in the name field (typed, or the model's).
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

    [[nodiscard]] bool typing() const noexcept { return m_form.focus().has_value(); }
    void type(std::string_view text);
    /// Ctrl+A 1 (selects the field's whole text), Backspace 8, Tab 9 (next field), Enter 13 (sends
    /// the chat, connects), Escape 27.
    std::vector<MultiplayerEvent> key(const MultiplayerModel& model, int key);

private:
    [[nodiscard]] static std::string tr(MpText id) { return mp_text(id); }
    [[nodiscard]] static std::string tr(MpText id, std::string_view value) { return mp_text(id, value); }
    void layout(const MultiplayerModel& model);
    void layout_browse(const MultiplayerModel& model);
    void layout_create(const MultiplayerModel& model);
    void layout_lobby(const MultiplayerModel& model);
    /// The lobby's slots and who watches, from the top of the left column. Returns where they end.
    std::int32_t layout_seats(const MultiplayerModel& model);
    /// While the game starts (net::Session::starting()): everyone in the game with how far they are
    /// with it, in place of the slots, from row m_start_first on. Returns where the rows end.
    std::int32_t layout_start_list(const MultiplayerModel& model);
    void layout_connecting();
    /// The game from the host on its way (MultiplayerModel::join), in `box` of the lobby.
    void layout_join_progress(const MultiplayerModel& model, ui::Rect box);
    /// The start dialog over the screen: the host's settings to choose, or to look at.
    /// `level` the difficulty of a new endless game (its preview); `reroll` whether its world
    /// number may change there (only while the host creates the game).
    void open_settings(const EndlessSetup& setup, SetupScope scope, GameSettingsMenu::Mode mode, std::uint8_t level,
                       bool reroll);
    /// Back from the start dialog to the screen it was opened from.
    void close_settings();
    /// The settings button and the summary of the rules below `y` in the right column.
    void settings_button(std::int32_t y, const EndlessSetup& setup, bool can_change);
    void click(const MultiplayerModel& model, ui::Form::Id id, std::vector<MultiplayerEvent>& out);
    /// The text of a field by its Id, nullptr if the Id is no field.
    [[nodiscard]] std::string* field_text(ui::Form::Id id);
    [[nodiscard]] std::size_t field_limit(ui::Form::Id id) const;
    /// The rows of the search list: the games found.
    struct BrowseRow {
        std::string address;  ///< "host:port" to join
        const net::FoundGame* game = nullptr;
    };
    [[nodiscard]] std::vector<BrowseRow> browse_rows(const MultiplayerModel& model) const;
    std::optional<MultiplayerEvent> join_event(const std::string& address) const;
    [[nodiscard]] std::string state_text(const net::GameInfo& info) const;

    ui::Form m_form;
    Screen m_screen = Screen::Browse;
    [[nodiscard]] std::int32_t banner_colour(sim::BannerId banner) const noexcept {
        return banner < m_banners.size() ? m_banners[banner].colour : m_other_banner;
    }
    [[nodiscard]] std::int32_t banner_text_colour(sim::BannerId banner) const noexcept {
        return banner < m_banners.size() ? m_banners[banner].text : m_other_banner;
    }
    [[nodiscard]] std::string banner_label(sim::BannerId banner) const;
    /// Whether this computer may choose the banner of slot `seat` now: its own, or as the host one
    /// that no other person plays.
    [[nodiscard]] static bool may_choose_banner(const MultiplayerModel& model, std::size_t seat);
    /// The colours on offer for m_banner_seat, in place of the chat lines from `top`.
    void layout_banner_choice(const MultiplayerModel& model, std::int32_t top);

    std::vector<MenuBanner> m_banners;
    std::uint8_t m_other_banner = 0;
    std::string m_notice;      ///< on the search screen, why the last session ended
    std::string m_connecting;  ///< the address being connected to
    // Search
    std::string m_name;
    bool m_name_known = false;  ///< m_name was taken from the model
    std::string m_address;
    std::string m_join_password;
    std::optional<std::string> m_selected;  ///< address of the chosen row
    std::size_t m_browse_first = 0;
    // Create
    HostSetup m_setup;
    std::string m_auto_title;  ///< the title last suggested from m_name
    std::string m_seed_text;
    std::string m_port_text;
    std::size_t m_list_first = 0;
    std::optional<std::size_t> m_chosen;  ///< scenario index or savegame slot
    bool m_settings_known = false;        ///< m_setup.settings was taken from the model
    // The start dialog, over the screen it was opened from
    GameSettingsMenu m_settings;
    Screen m_settings_from = Screen::Create;
    // Lobby
    std::string m_chat;
    bool m_ready = false;
    /// A row of the chat as shown: a line wrapped to the chat's width gives one or more.
    struct ChatRow {
        std::size_t line = 0;      ///< index in MultiplayerModel::chat
        std::uint64_t serial = 0;  ///< the line's ChatLine::serial
        std::string text;          ///< the row's part of chat_prefix() + text
        bool first = false;        ///< the line's first row (the one with the name)
    };
    /// The chat's rows for the model, wrapped anew only when the lines or the font changed.
    const std::vector<ChatRow>& chat_rows(const MultiplayerModel& model);
    std::size_t m_chat_back = 0;            ///< rows scrolled back from the newest (mouse wheel)
    std::size_t m_chat_rows = 0;            ///< the chat rows the last layout had room for
    std::uint64_t m_chat_newest = 0;        ///< ChatLine::serial of the newest line at the last layout
    const ui::Font* m_chat_font = nullptr;  ///< the font of the last draw(), to wrap the chat with
    std::vector<ChatRow> m_chat_wrapped;    ///< chat_rows() as last made
    /// What m_chat_wrapped was made of: the first and last lines' serials, their count, the font.
    std::tuple<std::uint64_t, std::uint64_t, std::size_t, const ui::Font*> m_chat_wrapped_for{};
    std::optional<std::size_t> m_banner_seat;     ///< the slot whose banner is being chosen
    std::vector<sim::BannerId> m_banner_choices;  ///< the banners of the buttons shown, in order
    /// (Host) The roster entries and names of the spectators shown with a kick button.
    std::vector<std::pair<std::size_t, std::string>> m_watchers;
    // While the game starts: the list of who is in it and how far they are with it.
    std::size_t m_start_first = 0;  ///< the first row shown
    std::size_t m_start_rows = 0;   ///< rows shown at the last layout (0: no list)
    ui::Rect m_start_box{};         ///< where the list is, for the mouse wheel
    std::int32_t m_mouse_x = 0;     ///< where the mouse last was, in design units
    std::int32_t m_mouse_y = 0;
    double m_scale = 1.0;  ///< of the design units (set_scale())
};

}  // namespace o1602::app
