// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original's frame around the map: the panel on the right with the minimap,
// compass, rotate and zoom buttons and the four modes (CTRL.GAD), and the status bar below the map
// with the status line and the icons of the last three messages (LEIST.GAD). Below the head the
// mode's panel: the build menu or the player status (side_panels.hpp); the windows of the
// selections come later.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blueprint_window.hpp"
#include "chat_window.hpp"
#include "diplomacy_strip.hpp"
#include "display_menu.hpp"
#include "editor_panel.hpp"
#include "end_screen.hpp"
#include "goods_window.hpp"
#include "key_help.hpp"
#include "main_menu.hpp"
#include "message_table.hpp"
#include "multiplayer_menu.hpp"
#include "name_edit.hpp"
#include "options_panel.hpp"
#include "selection.hpp"
#include "selection_panels.hpp"
#include "side_panels.hpp"
#include "template_window.hpp"
#include "tutorial.hpp"

#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/render/sdl_ui_renderer.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/panel.hpp"

struct SDL_Renderer;

namespace o1602::app {

class Hud {
public:
    /// What a click on the frame asks for.
    struct Action {
        enum class Kind : std::uint8_t {
            RotateLeft,
            RotateRight,
            ZoomIn,
            ZoomOut,
            Minimap,
            Message,
            Build,         ///< build mode with `building`
            Demolish,      ///< build mode, the demolish tool
            Copy,          ///< build mode, the copy tool (Open1602)
            Move,          ///< build mode, the move tool (Open1602)
            Blueprint,     ///< blueprint mode on or off (Open1602)
            Templates,     ///< build mode, the copy tool, and the templates' list (Open1602)
            TurnBuilding,  ///< the building to place by `step` quarters
            ToolOff,       ///< another mode: no building tool
            OrientToRoad,  ///< buildings with a door turn to the road (`step` 1) or not (0)
            FoundKontor,   ///< the building tool places a Kontor paid from `ship`
            Answer,        ///< the question was answered: `step` 1 yes, 0 no
            SelectShip,    ///< select `ship` and look at it (the ship list)
            Rating,        ///< the score sheets of the running game ("Bewertung")
            EndTutorial,   ///< „Einführungsspiel beenden“ (the X of the tutorial's window): `step` the chapter
        };
        Kind kind = Kind::RotateLeft;
        double tile_x = 0.0;  ///< Minimap and Message: the world tile to look at
        double tile_y = 0.0;
        std::uint32_t building = 0;
        int step = 0;
        std::uint32_t ship = 0;
    };
    /// The modes of the panel (CTRL.GAD).
    static constexpr std::int32_t kModeBuild = side_mode::kBuild;
    static constexpr std::int32_t kModeMilitary = side_mode::kMilitary;
    static constexpr std::int32_t kModeInfo = side_mode::kInfo;
    static constexpr std::int32_t kModeOptions = side_mode::kOptions;

    /// Nothing if the layouts or the panel's art are missing (the viewer then runs without frame).
    static std::unique_ptr<Hud> create(SDL_Renderer* renderer, const assets::GameDirectory& game,
                                       const assets::Palette& palette, const assets::TextTable* texts, bool data_fixes);

    /// Lays the frame out for the window at the interface's scale `ui_scale` (screen pixels per pixel
    /// of the 1024 x 768 design; the menus always fill the window as far as they fit); returns the
    /// rectangle left for the map.
    ui::Rect layout(std::int32_t width, std::int32_t height, double ui_scale);
    [[nodiscard]] const ui::Rect& game_view() const noexcept { return m_regions.game_view; }
    /// The frame's scale (screen pixels per pixel of the original's 1024 x 768).
    [[nodiscard]] double scale() const noexcept { return m_regions.scale; }
    /// Whether a point belongs to the frame (the map does not see the mouse there).
    [[nodiscard]] bool covers(std::int32_t x, std::int32_t y) const noexcept;

    /// The mouse in window pixels; returns what a click asked for. The right button reaches only
    /// the settings dialogs (a right click chooses a setting's previous value).
    std::optional<Action> mouse(std::int32_t x, std::int32_t y, bool down,
                                ui::MouseButton button = ui::MouseButton::Left);

    /// A message for the local player (MessageTable index) at a world tile, in the colour of
    /// `banner`; `text` (Windows-1252) replaces the message's own text, and that the original's
    /// MELDUNG text, in the status bar.
    void message(MessageId id, sim::BannerId banner, std::optional<std::pair<double, double>> tile,
                 std::uint64_t now_ms, std::string_view text = {});
    /// The messages' icons and texts (the mods' among them); without: the original's. Outlives this.
    void set_messages(const MessageTable* messages) noexcept {
        m_messages = messages != nullptr ? messages : &MessageTable::original();
    }
    /// What the resource bar at the top of the map shows: the
    /// player's money, and of the city under the mouse its tools, wood and bricks (own city) and
    /// inhabitants with the owner's shield. Costs of a pending build order in brackets.
    struct Resources {
        std::optional<std::int64_t> money;  ///< nothing: "???" (a player who is not human)
        std::optional<std::int64_t> money_cost;
        std::optional<std::array<std::int64_t, 3>> goods;  ///< tools, wood, bricks in t (own city)
        std::optional<std::array<std::int64_t, 3>> goods_cost;
        std::optional<std::int64_t> inhabitants;  ///< the city under the mouse (a discovered island)
        std::int32_t shield = -1;                 ///< banner of a human or computer owner, -1 the plain shield
        bool natives = false;                     ///< the city is a native village (the native figure)
    };
    void set_resources(Resources r) { m_resources = std::move(r); }
    /// The local player's points for the info panel, and whether its "Bewertung" may be pressed.
    void set_points(std::int64_t points, bool rating) {
        if (m_info) {
            m_info->set_points(points, rating);
        }
    }
    /// Text of the status line when the mouse is on no gadget (the build tool's report).
    void set_status(std::string text) { m_status = std::move(text); }
    /// The texts of Text.cod the windows show, null before they are loaded.
    [[nodiscard]] const assets::TextTable* texts() const noexcept { return m_texts ? &*m_texts : nullptr; }
    /// The view rotation for the compass, 0..3.
    void set_rotation(std::uint8_t r) noexcept { m_rotation = r; }
    /// The rotation of the building to place (the preview turns with it).
    void set_building_rotation(std::uint8_t r) noexcept { m_building_rotation = r; }
    /// What the build tool does (for the tutorial's bubbles): it holds a building to place, and
    /// what it would place lacks wood.
    void set_build_tool(bool holding, bool short_of_wood) noexcept {
        m_tool_holding = holding;
        m_tool_short_of_wood = short_of_wood;
    }
    /// The tutorial's chapter runs in this game, nothing: no tutorial. Its pages
    /// show in the mission's window and its help bubbles over the frame. After set_game().
    void set_tutorial(std::optional<std::uint8_t> chapter);
    [[nodiscard]] const Tutorial* tutorial() const noexcept { return m_tutorial ? &*m_tutorial : nullptr; }
    /// Which buildings the local player may pick (the build menu).
    void set_available(BuildPanel::Available available) { m_available = std::move(available); }
    [[nodiscard]] std::int32_t mode() const noexcept { return m_mode; }
    /// „Kontor errichten“ in the ship window: the side panel shows the first Kontor of the build
    /// menu with its costs and turning arrows while the tool places it (as the original). Any
    /// other mode ends it; the ship's window comes back with the info mode.
    void found_kontor(std::uint32_t ship, std::uint32_t kontor);
    /// The ship a Kontor is being founded from, nothing otherwise.
    [[nodiscard]] std::optional<std::uint32_t> founding_ship() const noexcept { return m_founding_ship; }
    /// The game the windows of the selections read, and who plays here.
    void set_game(const sim::Simulation& simulation, const assets::BuildingTable& buildings,
                  const assets::FigureTable& figures, sim::PlayerId local) {
        m_simulation = &simulation;
        m_buildings = &buildings;
        m_figure_table = &figures;
        m_local = local;
    }
    /// How players are shown: coats of arms and message icons in their colours. Kept, not copied.
    void set_banners(const render::BannerTable* banners) noexcept {
        m_banners = banners;
        m_end.set_banners(banners);
    }
    /// The unit families of the packages, which the castles' windows offer. Kept, not copied.
    void set_own_units(std::span<const assets::OwnUnit> units) noexcept { m_own_units = units; }
    /// The selected ships and land units: their windows show in the military mode.
    void set_units(ui::UnitSelection* units) noexcept { m_units = units; }
    /// What the player selected on the map; its window shows in the info mode. An object selected
    /// anew gets a new serial, so that its window starts on its first page.
    void set_selection(const Selection& selection) {
        m_selection = stamped_selection(m_selection, selection, m_selection_serial);
    }
    /// The build menu opens next with this building picked in its category (a farm's field after
    /// looking at the farm). False if the menu has no such building.
    bool show_in_build_menu(std::uint32_t id) { return m_build && m_build->show(id); }
    /// Blueprint mode is on: the build menu's title says so.
    void set_blueprint_in_build_menu(bool on) {
        if (m_build) {
            m_build->set_blueprint(on);
        }
    }
    /// Whether the game lets buildings move (sim::Rules::moving): the build menu offers the tool.
    void set_moving_in_build_menu(bool allowed) {
        if (m_build) {
            m_build->set_moving(allowed);
        }
    }
    /// The window of a blueprint (blueprint_window.hpp), opened by a click on one of the player's
    /// blueprints at (island, x, y), its origin. The viewer fills its model every frame and takes
    /// what was clicked.
    void open_blueprint(std::uint16_t island, std::int32_t x, std::int32_t y) {
        m_blueprint_at = BlueprintAt{island, x, y};
    }
    void close_blueprint() noexcept { m_blueprint_at.reset(); }
    struct BlueprintAt {
        std::uint16_t island = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    [[nodiscard]] const std::optional<BlueprintAt>& blueprint_open() const noexcept { return m_blueprint_at; }
    void set_blueprint_model(BlueprintModel model) {
        m_blueprint_model = std::move(model);
        if (!m_blueprint_model.valid) {
            m_blueprint_at.reset();
        }
    }
    [[nodiscard]] std::vector<BlueprintWindow::Result> take_blueprint_clicks() {
        return std::exchange(m_blueprint_clicks, {});
    }
    /// The window of the templates (template_window.hpp), beside the side panel: their list (the
    /// tools' pop-up) or the question for a new template's name (Ctrl+S with a piece in hand). The
    /// viewer fills the model while it is open and takes what was asked.
    void open_templates() {
        if (m_templates) {
            m_templates->open_list();
        }
    }
    void open_template_save(std::string_view name) {
        if (m_templates) {
            m_templates->open_save(name);
        }
    }
    void close_templates() noexcept {
        if (m_templates) {
            m_templates->close();
        }
    }
    [[nodiscard]] bool templates_open() const noexcept {
        return !m_menu_active && m_templates && m_templates->is_open();
    }
    void set_templates_model(TemplatesModel model) { m_templates_model = std::move(model); }
    [[nodiscard]] std::vector<TemplateRequest> take_template_requests() {
        return std::exchange(m_template_requests, {});
    }
    /// The editor runs (ADR 0013): its panel takes the place of the modes below the head, whose
    /// buttons go; the money, the mission's window and the tutorial do not show.
    void set_editing(bool on);
    [[nodiscard]] bool editing() const noexcept { return m_editing; }
    /// The editor's panel, null without the menus' layouts.
    [[nodiscard]] EditorPanel* editor_panel() noexcept { return m_editor.get(); }
    /// What the editor's panel shows; the viewer sets it every frame.
    void set_editor_model(EditorModel model) { m_editor_model = std::move(model); }
    [[nodiscard]] const EditorModel& editor_model() const noexcept { return m_editor_model; }
    /// What the editor's panel asked for since the last call.
    [[nodiscard]] std::vector<EditorRequest> take_editor_requests() { return std::exchange(m_editor_requests, {}); }
    /// The build menu shows a tool a key started (its title names it).
    void show_tool_in_build_menu(BuildPanel::Tool tool) {
        if (m_build) {
            m_build->show_tool(tool);
        }
    }
    /// Opens the ship or city list or the diplomacy window in the info mode (the buttons of the
    /// player status, L, Shift+L, V), or closes it if it is open; returns what the tool should do.
    std::optional<Action> toggle_window(Selection::Kind kind);
    /// Opens the diplomacy window in the info mode at `partner` (an offer's icon in the status bar).
    std::optional<Action> open_diplomacy(sim::PlayerId partner);
    [[nodiscard]] const Selection& selection() const noexcept { return m_selection; }
    /// Whether a window waits for a click on the map (the info mode).
    [[nodiscard]] bool picking();
    /// That click, world tile.
    void map_click(std::uint32_t x, std::uint32_t y);
    /// Escape: a window goes back a page or stops picking; false if nothing did.
    bool back();
    /// A right click: the selected object's window goes back a page if it is on one that steps
    /// back (SelectionPanel::steps_back); false if it is not.
    bool step_back();
    /// Commands the windows gave since the last call.
    [[nodiscard]] std::vector<sim::Command> take_commands() { return std::exchange(m_commands, {}); }
    /// Switches the mode (also from the keys); returns what the tool should do.
    std::optional<Action> set_mode(std::int32_t mode);

    /// The minimap's window on the screen (videos of messages play there, ADR 0010).
    [[nodiscard]] ui::Rect minimap_area() const { return minimap_rect(); }
    /// The world tile of a point on the minimap, nothing elsewhere.
    [[nodiscard]] std::optional<std::pair<double, double>> minimap_tile(std::int32_t x, std::int32_t y) const;
    /// The place of the newest message that has one (Space jumps there).
    [[nodiscard]] std::optional<std::pair<double, double>> last_message_tile() const;
    /// A question over the map with yes and no (ADR 0010: before scuttling, disbanding, war); the
    /// answer comes as Action::Answer from mouse() or answer().
    void ask(std::string question) {
        m_question = std::move(question);
        m_choices.clear();
        m_modal_press = ModalPress::None;
    }
    [[nodiscard]] bool asking() const noexcept { return m_question.has_value(); }
    void close_question() noexcept {
        m_question.reset();
        m_choices.clear();
        m_modal_press = ModalPress::None;
    }
    /// One of several answers, a button each (which player to play in a game loaded alone): with
    /// a banner, a field of its colour beside the text.
    struct ChoiceOption {
        std::string text;
        std::optional<sim::BannerId> banner;
    };
    /// A question over the map with one button per option; the answer comes as Action::Answer with
    /// `step` the index of the option (Enter and Escape choose the first).
    void ask_choice(std::string question, std::vector<ChoiceOption> options) {
        m_question = std::move(question);
        m_choices = std::move(options);
        m_modal_press = ModalPress::None;
    }
    [[nodiscard]] bool choosing() const noexcept { return m_question.has_value() && !m_choices.empty(); }
    [[nodiscard]] std::size_t choice_count() const noexcept { return m_choices.size(); }
    /// The key overview over the map (F1, ADR 0010) in pages with tabs, as in the History Edition.
    /// A click on a tab turns to its page, any other click closes it.
    void show_help(std::array<KeyHelpPage, kKeyHelpPages> pages) {
        m_help = std::move(pages);
        m_help_page = 0;
    }
    [[nodiscard]] bool helping() const noexcept { return m_help.has_value(); }
    /// Turns the key overview `delta` pages on, round at the ends.
    void turn_help_page(std::int32_t delta) noexcept {
        constexpr auto n = static_cast<std::int32_t>(kKeyHelpPages);
        m_help_page = static_cast<std::size_t>(((static_cast<std::int32_t>(m_help_page) + delta) % n + n) % n);
    }
    /// A question, the key overview or the window "Anzeige" or "Warenbilanz" is open: it takes every
    /// click.
    [[nodiscard]] bool modal() const noexcept {
        return m_question.has_value() || m_help.has_value() || m_display_open || m_goods_open;
    }
    /// The window "Warenbilanz" over the map (goods_window.hpp): opened, or closed when open.
    void toggle_goods() noexcept {
        m_goods_open = !m_goods_open && m_goods != nullptr;
        if (m_goods_open) {
            m_goods->open();
            m_display_open = false;
        }
    }
    [[nodiscard]] bool goods_open() const noexcept { return m_goods_open; }
    /// Escape in the window "Warenbilanz": a good's page back to the table, else the window closes.
    void goods_back() noexcept {
        if (m_goods_open && !m_goods->back()) {
            m_goods_open = false;
        }
    }
    /// The mouse wheel at (`x`, `y`): over the side panel the window there scrolls if it can (the
    /// ship and city lists, the savegames and music titles of the settings), over the mission
    /// window its text; while the menu is open its list (MainMenu::wheel, MultiplayerMenu::wheel)
    /// and never the map. False if nothing took it.
    bool panel_wheel(std::int32_t x, std::int32_t y, std::int32_t notches);
    /// The mouse wheel over the map while the window "Warenbilanz" is open: its table scrolls.
    void goods_wheel(std::int32_t notches) noexcept {
        if (m_goods_open) {
            m_goods->wheel(notches);
        }
    }
    /// The window "Anzeige" (display_menu.hpp) showing `model`: over the map, or over the main menu
    /// (the History Edition's "Options").
    void open_display(DisplayModel model) {
        m_display_model = std::move(model);
        m_display_open = m_display != nullptr;
        m_goods_open = m_goods_open && !m_display_open;
        m_display_changed = false;
    }
    void close_display() noexcept { m_display_open = false; }
    /// The goods trend under the stock of a Kontor (DisplaySettings::goods_trend).
    void set_goods_trend(bool on) noexcept { m_goods_trend = on; }
    [[nodiscard]] bool display_open() const noexcept { return m_display_open; }
    /// Keeps the window's size the window shows up to date (the player drags the edge meanwhile).
    void set_display_window(WindowSize size) noexcept { m_display_model.window = size; }
    /// The settings the window "Anzeige" changed since the last call, if it changed any.
    [[nodiscard]] std::optional<DisplaySettings> take_display_change() {
        if (!std::exchange(m_display_changed, false)) {
            return std::nullopt;
        }
        return m_display_model.settings;
    }
    void close_help() noexcept { m_help.reset(); }
    /// Closes the build menu's pop-up of a category (public buildings ...); false if none was open.
    bool close_build_popup() noexcept {
        if (m_build && m_build->popup_open()) {
            m_build->close_popup();
            return true;
        }
        return false;
    }
    /// Another game was loaded: messages, selection, questions and the minimap of the old one go.
    void reset_game();
    /// The render device was reset: the frame's textures are made again when next drawn.
    void release_textures() { m_renderer->release_textures(); }
    /// The packages' icons changed (ui::set_package_icons()): their pictures are read again.
    void forget_package_icons() { m_renderer->forget_package_icons(); }
    /// The render device was lost and the renderer made again: the frame draws with the new one
    /// (release_textures() went before the old one did).
    void set_renderer(SDL_Renderer* renderer) noexcept {
        m_sdl = renderer;
        m_renderer->set_renderer(renderer);
    }
    /// The settings (mode 30014): what they show, and what they asked for since the last call.
    [[nodiscard]] OptionsModel& options_model() noexcept { return m_options_model; }
    [[nodiscard]] std::vector<OptionsEvent> take_option_events() { return std::exchange(m_option_events, {}); }
    /// The chat of a multiplayer game (chat_window.hpp): what it shows besides its lines, which are
    /// those of multiplayer_model(). The viewer sets it every frame; without a game it closes.
    void set_chat(ChatModel model) {
        m_chat_model = std::move(model);
        if (!m_chat_model.available && m_chat) {
            m_chat->close();
        }
    }
    /// The chat key: opens the chat; false outside a multiplayer game.
    bool open_chat();
    [[nodiscard]] bool chat_open() const noexcept { return !m_menu_active && m_chat && m_chat->is_open(); }
    /// The lines the chat sent since the last call.
    [[nodiscard]] std::vector<ChatSay> take_chat() { return std::exchange(m_chat_says, {}); }

    /// A name or a line of the chat is being typed (a savegame's, the player's in the menu, an own
    /// ship's or city's in its window): the keys go there (Windows-1252 text, Ctrl+A 1, Backspace 8,
    /// Tab 9, Enter 13, Escape 27).
    [[nodiscard]] bool typing() const noexcept {
        if (m_menu_active) {
            return multiplayer_open() ? m_multiplayer->typing() : m_menu && m_menu->typing();
        }
        if (chat_open()) {
            return true;
        }
        if (templates_open() && m_templates->typing()) {
            return true;
        }
        if (m_editing && m_editor && m_editor->typing()) {
            return true;
        }
        if (naming()) {
            return true;
        }
        return m_options && m_mode == kModeOptions && m_options->editing();
    }
    void type_text(std::string_view text) {
        if (multiplayer_open()) {
            m_multiplayer->type(text);
        } else if (m_menu_active && m_menu) {
            m_menu->type(text);
        } else if (chat_open()) {
            m_chat->type(text);
        } else if (templates_open() && m_templates->typing()) {
            m_templates->type(text);
        } else if (m_editing && m_editor && m_editor->typing()) {
            m_editor->type(text);
        } else if (naming()) {
            m_names.type(text);
        } else if (m_options) {
            m_options->type(text);
        }
    }
    void type_key(int key) {
        if (chat_open()) {
            if (auto say = m_chat->key(m_chat_model, key)) {
                m_chat_says.push_back(std::move(*say));
            }
        } else if (templates_open() && m_templates->typing()) {
            if (auto request = m_templates->key(key)) {
                m_template_requests.push_back(std::move(*request));
            }
        } else if (m_editing && m_editor && m_editor->typing()) {
            if (auto request = m_editor->key(key)) {
                m_editor_requests.push_back(std::move(*request));
            }
        } else if (naming()) {
            if (auto c = m_names.key(key)) {
                m_commands.push_back(std::move(*c));
            }
        } else if (multiplayer_open()) {
            for (MultiplayerEvent& e : m_multiplayer->key(m_multiplayer_model, key)) {
                m_multiplayer_events.push_back(std::move(e));
            }
        } else if (m_menu_active && m_menu) {
            if (auto e = m_menu->key(key)) {
                m_menu_events.push_back(std::move(*e));
            }
        } else if (m_options) {
            if (auto e = m_options->key(key)) {
                m_option_events.push_back(std::move(*e));
            }
        }
    }

    /// The main menu over the whole window (no game shows meanwhile); nothing if its layouts are
    /// missing.
    [[nodiscard]] MainMenu* menu() noexcept { return m_menu.get(); }
    [[nodiscard]] bool menu_active() const noexcept { return m_menu_active; }
    void open_menu() {
        if (m_menu) {
            m_menu_active = true;
            m_multiplayer_shown = false;
            m_display_open = false;  // the game's window "Anzeige" stays behind
            m_menu->open();
        }
    }
    void close_menu() noexcept {
        m_menu_active = false;
        m_multiplayer_shown = false;
    }
    /// The multiplayer menu over the whole window, in place of the main menu (`notice`: why the
    /// last session ended). Its screens show m_multiplayer_model, which the viewer keeps up to date.
    [[nodiscard]] MultiplayerMenu* multiplayer() noexcept { return m_multiplayer.get(); }
    [[nodiscard]] bool multiplayer_open() const noexcept { return m_menu_active && m_multiplayer_shown; }
    void open_multiplayer(std::string notice = {}) {
        if (m_menu) {
            m_menu_active = true;
            m_multiplayer_shown = true;
            m_display_open = false;
            m_multiplayer->open(std::move(notice));
        }
    }
    /// Back to the main menu.
    void close_multiplayer() noexcept { m_multiplayer_shown = false; }
    [[nodiscard]] MultiplayerModel& multiplayer_model() noexcept { return m_multiplayer_model; }
    [[nodiscard]] std::vector<MultiplayerEvent> take_multiplayer_events() {
        return std::exchange(m_multiplayer_events, {});
    }
    [[nodiscard]] std::vector<MenuEvent> take_menu_events() { return std::exchange(m_menu_events, {}); }
    /// The score sheets over the whole window, like the menu (menu_active() holds meanwhile, so
    /// the game stands still behind them, as in the original). The player's choice comes back
    /// through take_end_choice().
    void open_end_screen(sim::EndScreenSummary summary, bool view_only);
    [[nodiscard]] bool end_screen_active() const noexcept { return m_end_active; }
    void end_screen_step(int direction) { m_end.step(direction); }
    [[nodiscard]] std::optional<EndScreen::Choice> take_end_choice() { return std::exchange(m_end_choice, {}); }
    /// Leaves the end screen: the main menu (Resume, or no menu: the game).
    void close_end_screen(EndScreen::Choice choice);
    /// The menu's picture frame on the screen (the credits play there).
    [[nodiscard]] ui::Rect menu_video_rect() const;
    void draw_menu();

    /// Draws the frame. `view` are the corners of the visible map in world tiles (the minimap's
    /// frame), `world` the game for the minimap (kept up to date one island per frame).
    void draw(const sim::World& world, const assets::BuildingTable& buildings, const render::BannerTable& banners,
              const std::array<std::pair<double, double>, 4>& view, std::uint64_t now_ms,
              const sim::PlayerFigures& figures, std::uint8_t colour);
    /// Texts over the map (the stock of production buildings), before the frame: `items` in screen
    /// pixels of the map, drawn at the interface's scale like the original's fonts.
    void draw_over_map(const std::vector<ui::DrawItem>& items) { m_renderer->draw(items, 0, 0, m_regions.scale); }
    /// A mark of the unit identification (M, History Edition): the coat of arms fitted into `arms`
    /// on a pole at x `pole_x` from `pole_top` down to `pole_foot`, in screen pixels.
    struct UnitArms {
        ui::Rect arms;
        std::int32_t pole_x = 0;
        std::int32_t pole_top = 0;
        std::int32_t pole_foot = 0;
    };
    /// The marks with the coat of arms of `banner` (the top bar's, recoloured for further colours).
    void draw_unit_arms(const std::vector<UnitArms>& marks, sim::BannerId banner);
    /// After the frame is shown (the text cache keeps what the frame drew).
    void next_frame() { m_renderer->next_frame(); }

private:
    Hud(SDL_Renderer* renderer, const assets::GameDirectory& game, const assets::Palette& palette,
        assets::GadgetLayout ctrl, assets::GadgetLayout leist, const assets::TextTable* texts, bool data_fixes);
    /// What the build tool should do in the build mode now.
    [[nodiscard]] std::optional<Action> build_tool_action();
    void draw_resources();
    void build_minimap(const sim::World& world, const assets::BuildingTable& buildings,
                       const render::BannerTable& banners);
    /// Redraws one island's tiles on the minimap; true if a pixel changed.
    bool refresh_minimap_island(const sim::World& world, const assets::BuildingTable& buildings,
                                const render::BannerTable& banners, std::size_t island);
    [[nodiscard]] ui::Rect minimap_rect() const;
    /// The editor's panel's room in the side panel (design units): below the head, to the bottom.
    void place_editor_panel();
    /// Where the world's picture lies: all of minimap_rect() once there is a world.
    [[nodiscard]] ui::Rect minimap_image() const;
    /// m_minimap (a pixel per tile) shrunk to the minimap's w x h pixels into m_minimap_small.
    void shrink_minimap(std::int32_t w, std::int32_t h);

    struct Event {
        MessageId id = 0;
        sim::BannerId banner = 0;
        std::optional<std::pair<double, double>> tile;
        std::uint64_t time_ms = 0;
        std::string text;  ///< empty: the MELDUNG text the message names
    };

    SDL_Renderer* m_sdl;
    assets::GameDirectory m_game;  ///< a copy: its History Edition texts fill the missions' placeholders
    assets::Palette m_palette;
    std::unique_ptr<render::SdlUiRenderer> m_renderer;
    ui::Panel m_ctrl;
    ui::Panel m_leist;
    ui::ScreenRegions m_regions;
    std::optional<assets::TextTable> m_texts;
    std::vector<std::string> m_leiste;   ///< Text.cod [LEISTE]: help texts of the status line
    std::vector<std::string> m_meldung;  ///< Text.cod [MELDUNG]
    const MessageTable* m_messages = &MessageTable::original();
    std::string m_status;
    std::uint8_t m_rotation = 0;
    std::uint8_t m_building_rotation = 0;
    std::int32_t m_mode = kModeInfo;  ///< the original starts in the info mode
    std::unique_ptr<BuildPanel> m_build;
    std::unique_ptr<InfoPanel> m_info;
    BuildPanel::Available m_available;
    Resources m_resources;
    std::unique_ptr<SelectionPanels> m_selection_panels;
    Selection m_selection;
    std::uint32_t m_selection_serial = 0;
    ui::UnitSelection* m_units = nullptr;
    const sim::Simulation* m_simulation = nullptr;
    const render::BannerTable* m_banners = nullptr;
    const assets::BuildingTable* m_buildings = nullptr;
    const assets::FigureTable* m_figure_table = nullptr;
    std::span<const assets::OwnUnit> m_own_units;
    sim::PlayerId m_local = sim::kNoPlayer;
    std::vector<sim::Command> m_commands;
    NameEdit m_names;  ///< the name of a ship or city being typed in its window
    /// A ship's or city's name is typed in the window of the info or military mode.
    [[nodiscard]] bool naming() const noexcept {
        return !m_menu_active && (m_mode == kModeInfo || m_mode == kModeMilitary) && m_names.active();
    }
    /// The window of the current selection, nothing without one or without the game.
    [[nodiscard]] SelectionPanel* selection_panel(std::optional<PanelContext>& context);
    std::int32_t m_gfx_baumat = 46;          ///< TOOLS.INC GFXBAUMAT
    std::int32_t m_gfx_tutor = 645;          ///< TOOLS.INC GFXTUTOR: the mission window's art
    std::deque<Event> m_events;              ///< newest first, at most three
    std::vector<diplomacy::Offer> m_offers;  ///< the offers the status bar shows, as last drawn
    std::vector<std::uint8_t> m_minimap;
    std::vector<std::uint8_t> m_minimap_small;  ///< as drawn: a pixel per pixel of the minimap
    std::int32_t m_minimap_w = 0;               ///< size of m_minimap_small
    std::int32_t m_minimap_h = 0;
    std::uint64_t m_minimap_ms = 0;
    bool m_minimap_rebuild = true;   ///< the whole picture must be made again (a new game)
    bool m_minimap_upload = true;    ///< the texture must be uploaded again
    bool m_minimap_changed = false;  ///< pixels changed since the last upload
    std::size_t m_minimap_islands = 0;
    std::size_t m_minimap_next = 0;  ///< the island refreshed next
    std::uint32_t m_world_w = 0;
    std::uint32_t m_world_h = 0;
    /// Top left of the menu's 1024 x 768 design on the screen (centred).
    void menu_origin(std::int32_t& x, std::int32_t& y) const;
    std::unique_ptr<MainMenu> m_menu;
    std::unique_ptr<MultiplayerMenu> m_multiplayer;  ///< with the main menu
    MultiplayerModel m_multiplayer_model;
    bool m_multiplayer_shown = false;
    std::vector<MultiplayerEvent> m_multiplayer_events;
    bool m_menu_active = false;
    std::vector<MenuEvent> m_menu_events;
    EndScreen m_end;
    bool m_end_active = false;
    std::optional<EndScreen::Choice> m_end_choice;
    std::unique_ptr<DisplayMenu> m_display;
    DisplayModel m_display_model;
    bool m_display_open = false;
    bool m_display_changed = false;
    /// Top left of the window "Anzeige" on the screen (centred on the map).
    [[nodiscard]] ui::Rect display_rect() const;
    std::unique_ptr<ChatWindow> m_chat;
    std::unique_ptr<BlueprintWindow> m_blueprint;
    std::optional<BlueprintAt> m_blueprint_at;
    BlueprintModel m_blueprint_model;
    std::vector<BlueprintWindow::Result> m_blueprint_clicks;
    [[nodiscard]] ui::Rect blueprint_rect() const;
    std::unique_ptr<TemplateWindow> m_templates;
    TemplatesModel m_templates_model;
    std::unique_ptr<EditorPanel> m_editor;
    EditorModel m_editor_model;
    std::vector<EditorRequest> m_editor_requests;
    bool m_editing = false;
    std::vector<TemplateRequest> m_template_requests;
    /// The templates' window on the screen (at the upper right of the map, by the side panel).
    [[nodiscard]] ui::Rect templates_rect() const;
    ChatModel m_chat_model;
    std::vector<ChatSay> m_chat_says;
    /// The chat window on the screen (at the lower left of the map).
    [[nodiscard]] ui::Rect chat_rect() const;
    std::unique_ptr<GoodsWindow> m_goods;
    bool m_goods_open = false;
    std::vector<std::uint8_t> m_goods_parchment;  ///< its ground, RGBA (parchment.hpp)
    std::uint8_t m_goods_shadow = 0;
    bool m_goods_trend = true;
    /// The window "Warenbilanz" on the screen (centred on the map).
    [[nodiscard]] ui::Rect goods_rect() const;
    double m_menu_scale = 1.0;  ///< of the menus over the whole window: as large as fits
    std::unique_ptr<OptionsPanel> m_options;
    OptionsModel m_options_model;
    std::vector<OptionsEvent> m_option_events;
    std::optional<ui::Panel> m_tutor;  ///< the mission's window
    /// Its gadgets at the right end with their places in the layout, and the text's size there.
    std::vector<std::pair<std::int32_t, std::array<std::int32_t, 2>>> m_tutor_right;
    std::array<std::int32_t, 2> m_tutor_text_size{};
    std::int32_t m_tutor_text_x = 0;    ///< TUTOR.GAD: the text's left edge
    std::int32_t m_tutor_arrows_x = 0;  ///< TUTOR.GAD: the scroll arrows' left edge
    std::int32_t m_tutor_line = 0;      ///< first line of its text shown
    std::optional<std::string> m_question;
    std::optional<std::array<KeyHelpPage, kKeyHelpPages>> m_help;
    std::size_t m_help_page = 0;
    /// Where the left button went down while the question or the key overview was shown: they
    /// answer to a click (press and release), never to the pointer merely passing over a button.
    enum class ModalPress : std::uint8_t { None, Yes, No, Elsewhere };
    ModalPress m_modal_press = ModalPress::None;
    std::vector<ChoiceOption> m_choices;  ///< the options of ask_choice(), empty for yes and no
    /// The window of the question (ask, ask_choice), built as Open1602's other windows (the
    /// settings of a game): a heading, a button per answer, a field of a banner's colour beside it.
    ui::Form m_question_form;
    /// The banners' colours in the palette (by BannerId) for the options' fields; grey for others.
    std::vector<std::int32_t> m_banner_palette;  ///< the banners' colours (ui::rgb_ink)
    std::uint8_t m_other_banner_palette = 0;
    void draw_question();
    /// Lays the question out in m_question_form; its place on the screen.
    [[nodiscard]] ui::Rect layout_question();
    [[nodiscard]] std::optional<Action> question_mouse(std::int32_t x, std::int32_t y, bool down);
    /// "Auftrag ansehen": the mission's text in the window above the status bar (TUTOR.GAD).
    void draw_mission(const sim::World& world);
    void draw_help();
    /// The key overview's window and the tab of page `page` in it, for drawing and clicks.
    [[nodiscard]] ui::Rect help_box() const;
    [[nodiscard]] ui::Rect help_tab(const ui::Rect& box, std::size_t page) const;
    /// The tutorial's step of this frame: the page of its window and the bubbles to draw.
    void step_tutorial(const BuildPanel::Available& available, std::uint64_t now_ms);
    [[nodiscard]] TutorialView tutorial_view(const BuildPanel::Available& available);
    /// The help bubbles of the step, each kept beside what it points at.
    void draw_bubbles();

    std::optional<Tutorial> m_tutorial;
    std::optional<ui::Panel> m_bubble_panel;  ///< BUBBLE.GAD
    std::vector<std::uint8_t> m_bubbles;      ///< drawn this frame
    std::vector<std::string> m_tutor_lines;   ///< Text.cod [TUTOR]: six lines per page
    TutorialFacts m_tutorial_facts;
    std::uint64_t m_tutorial_facts_ms = 0;         ///< when they were last read
    std::optional<std::uint32_t> m_founding_ship;  ///< found_kontor()
    bool m_tool_holding = false;
    bool m_tool_short_of_wood = false;
};

}  // namespace o1602::app
