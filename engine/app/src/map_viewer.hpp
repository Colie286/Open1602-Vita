// SPDX-License-Identifier: GPL-3.0-or-later
//
// First interactive program: shows the world map of a scenario or savegame and lets the user
// scroll, zoom and rotate it. Becomes the in-game map view once the simulation runs.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app_settings.hpp"
#include "autosave.hpp"
#include "build_tool.hpp"
#include "catchment_view.hpp"
#include "command_sink.hpp"
#include "event_presenter.hpp"
#include "game_sound.hpp"
#include "game_state.hpp"
#include "hud.hpp"
#include "map_input.hpp"
#include "mods_menu.hpp"
#include "multiplayer_menu.hpp"
#include "names.hpp"
#include "stock_notes.hpp"
#include "templates.hpp"
#include "video_controller.hpp"
#include "viewer_options.hpp"
#include "zoom_graphics.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_looks.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/package_scenarios.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/document.hpp"
#include "o1602/editor/terrain.hpp"
#include "o1602/net/enet_transport.hpp"
#include "o1602/net/lan.hpp"
#include "o1602/net/session.hpp"
#include "o1602/platform/mouse_cursor.hpp"
#include "o1602/platform/window.hpp"
#include "o1602/render/ambient.hpp"
#include "o1602/render/animation_clock.hpp"
#include "o1602/render/camera.hpp"
#include "o1602/render/cart_figures.hpp"
#include "o1602/render/combat_effects.hpp"
#include "o1602/render/graphics_preloader.hpp"
#include "o1602/render/land_figures.hpp"
#include "o1602/render/sdl_map_canvas.hpp"
#include "o1602/render/sdl_sprite_renderer.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/sprite_hit_mask.hpp"
#include "o1602/render/walker_figures.hpp"
#include "o1602/render/world_graphics.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/score.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/key_bindings.hpp"
#include "o1602/ui/unit_selection.hpp"

union SDL_Event;

namespace o1602::app {

class MainMenu;

/// The running program: the window, the game and its presentation. Coordinates the parts that
/// take the input (MapInput), present the game's events (EventPresenter) and play the videos
/// (VideoController), and runs the frames.
class MapViewer : private MapInput::Host {
public:
    static core::Result<std::unique_ptr<MapViewer>> create(const assets::GameDirectory& game,
                                                           const ViewerOptions& options);
    ~MapViewer() override;
    MapViewer(const MapViewer&) = delete;
    MapViewer& operator=(const MapViewer&) = delete;

    /// Main loop; returns the process exit code.
    int run();

    /// The user's data folder of Open1602 (settings; the savegames in "saves" below it).
    [[nodiscard]] static std::filesystem::path data_folder();
    /// The player's own scenarios (<data folder>/scenarios), listed after the game folder's;
    /// made if it is missing.
    [[nodiscard]] static std::filesystem::path own_scenario_folder();

private:
    MapViewer(assets::GameDirectory game, assets::BuildingTable buildings, sim::World world, assets::Palette palette,
              assets::FigureTable figures, ViewerOptions options);

    /// Starts loading the zoom levels in the background (window and banners must be there).
    void start_preloading();
    /// The graphics of `zoom` on the GPU; waits for the background if it is not done yet.
    core::Status ensure_graphics(render::Zoom zoom);
    /// The menu is shown: no world is drawn.
    [[nodiscard]] bool in_menu() const noexcept { return m_hud && m_hud->menu_active(); }
    /// Every frame: uploads the zoom levels the background finished.
    void adopt_preloaded_graphics();
    core::Status install_graphics(render::Zoom zoom, render::PreparedGraphics prepared);
    /// The parts of the world's layers that do not change within a frame (graphics of the zoom
    /// level, figures, banners).
    [[nodiscard]] render::WorldLayers world_layers(const ZoomGraphics& graphics) const;
    /// The pictures of a unit as drawn (ship: hull and flag; land unit: body), with the row and
    /// depth that order them; nothing if it is not drawn.
    [[nodiscard]] std::optional<render::FigureSprites> unit_sprites(const ui::UnitRef& ref) const override;
    /// The map tiles whose pictures cover a world pixel, the front-most first.
    void tiles_at_pixel(std::int32_t px, std::int32_t py, std::vector<render::TileHit>& out) const override;
    /// The graphics of the zoom level shown; null while they are not there.
    [[nodiscard]] const ZoomGraphics* graphics() const override;

    // What the input asks of the viewer (MapInput::Host).
    void quick_save() override;
    void quick_load() override;
    void request_screenshot() override;
    void quit() override;
    void toggle_owners() override { m_show_owners = !m_show_owners; }
    void toggle_fps() override { m_show_fps = !m_show_fps; }
    void toggle_coordinates() override { m_show_coordinates = !m_show_coordinates; }
    void toggle_unit_arms() override { m_show_unit_arms = !m_show_unit_arms; }
    void toggle_fertility() override;
    void toggle_fullscreen() override;
    void toggle_pause_video() override;
    void save_template() override;
    void notify(std::string text, std::string_view log) override;
    void editor_key(ui::KeyAction action) override;
    void editor_pick(const EditorPick& pick) override;
    bool editor_place(const render::TilePos& tile) override;
    bool editor_cancel_place() override;
    bool editor_stroke(std::optional<render::TilePos> tile, Stroke phase) override;

    /// The window's events; the rest goes to the input.
    void handle_event(const SDL_Event& event);
    void update(std::uint32_t delta_ms);
    void render();
    void update_title(double fps);
    /// The frame rate and the tile under the mouse in the map's top right corner, as switched on.
    void draw_corner_info();
    /// The unit identification (M, History Edition): the coat of arms over every own land unit.
    void draw_unit_arms();
    /// The crop the fertility view shows now (0: none), and the island of the selected farm it
    /// comes from.
    [[nodiscard]] std::pair<std::uint8_t, std::optional<std::uint16_t>> fertility_view_crop() const;
    /// The fertility view's marks for this frame (fertility_view.hpp).
    void add_fertility_marks();
    /// The English developer readout under O1602_DEBUG_TITLE: the map under the mouse, the selection, fps.
    [[nodiscard]] std::string debug_title(double fps);
    /// Where the camera starts and Home goes: the local player's Kontor, else his first ship,
    /// else the first island.
    [[nodiscard]] render::TilePos home_tile() const override;
    core::Status save_screenshot(const std::filesystem::path& path);
    /// Shows the drawn frame. A screenshot asked for since the last one is read first: SDL can read
    /// the picture back only before it is presented.
    void present();
    /// Where F12 puts the next screenshot: screenshots/open1602-YYYYMMDD-HHMMSS.png in the user's
    /// data folder, never over an older one.
    [[nodiscard]] static std::filesystem::path next_screenshot_path();
    /// Saves the game: Open1602's own savegame, or a .GAM the original loads if the path ends in
    /// .gam. Never into the game's installation. An autosave still being written is finished
    /// first, so it never lands over a newer file.
    core::Status save_game(const std::filesystem::path& path);
    /// Where a savegame for `path` goes (absolute), unless that lies in the game's installation
    /// or cannot be checked; the groups of this computer's player go into the world.
    core::Result<std::filesystem::path> prepare_save(const std::filesystem::path& path);
    /// Saves the game into a slot of the savegame list and names it there (slots.cfg, the panel,
    /// the menu).
    core::Status save_slot(std::size_t slot, std::string name);
    /// Names a slot whose file was just written: slots.cfg, the panel and the menu.
    void remember_slot(std::size_t slot, std::string name, std::uint8_t humans);
    /// Human players in the game now (the savegame list shows them).
    [[nodiscard]] std::uint8_t human_players() const;
    /// Keeps the game as lastgame when DisplaySettings::autosave_minutes of game time have passed
    /// (only while a game is played, not in the menus or while a multiplayer game stands). The game
    /// is captured in the frame; packing and writing the file run on a thread of their own.
    void autosave_if_due();
    /// Waits for the autosave being written (if any) and names its slot; `wait` false: only if it
    /// is done already.
    void finish_autosave(bool wait = true);
    /// The settings and savegame names of the user's data folder into the panel (at the start).
    void load_options();
    /// Chooses the language of Open1602's own texts: --language, else settings.cfg, else the
    /// installation's; catalogues in <data folder>/locale extend the built-in ones.
    void apply_language();
    /// What the settings panel asked for this frame (save, load, quit, sounds, window size).
    void apply_options();
    /// How often the scenery shows itself, from the settings.
    [[nodiscard]] render::Ambient::Config ambient_config() const;
    void save_settings();
    /// Writes a newly drawn player ID into settings.cfg, leaving the file's other lines as they are.
    static void remember_player_id(std::uint64_t id);
    /// Replaces the running game by a savegame (Open1602's own or the original's) while the
    /// program runs (F9); the game goes on as it was if the file cannot be read. `rules`: the
    /// settings chosen for a scenario (a savegame of Open1602 keeps its own).
    core::Status load_game(const std::filesystem::path& path, const sim::Rules* rules = nullptr);
    /// A game loaded alone that does not know this person and has several human players: they
    /// choose which one to play before it starts (ask_choice); the human players to choose from,
    /// empty if the game needs no choice.
    [[nodiscard]] std::vector<sim::PlayerId> player_choice(const sim::World& world) const;
    /// The answer to that question: the game starts with the chosen player.
    void finish_pending_load(std::size_t option);
    /// A savegame or scenario as a world, not started.
    /// `warnings`: how many the loader gave (they are written to the log).
    [[nodiscard]] core::Result<sim::World> read_world(const std::filesystem::path& path,
                                                      std::size_t* warnings = nullptr) const;
    /// The game goes on in another world (a savegame, a scenario, a new game): the new game is
    /// built whole, then everything of the old one goes. `source`: the file it came from, for the
    /// window title when the game has no name of its own. Out of memory leaves the old game running.
    /// `rules`: the settings of a new game; else those of the command line (a loaded game brings its own).
    /// The state of a game with `world`: a new game takes the building values of `rules` under the
    /// mods' (a savegame of Open1602 keeps its own), and m_buildings gets the game's values.
    [[nodiscard]] std::unique_ptr<GameState> make_state(sim::World world, const sim::Rules& rules,
                                                        bool view_only = false);
    /// The presentation takes `next` as the game: everything of the old one goes. `source`: the file
    /// it came from.
    void adopt_state(std::unique_ptr<GameState> next, const std::filesystem::path& source);
    core::Status replace_world(sim::World world, const std::filesystem::path& source = {},
                               const sim::Rules* rules = nullptr);
    /// The name of the game for the window title (UTF-8).
    [[nodiscard]] std::string game_title() const;
    /// The multiplayer part of the window title (players, ended, waiting), or nothing.
    [[nodiscard]] std::string session_title() const;
    /// Tells (on stderr) that a savegame's simulation state was damaged (Simulation::state_damaged).
    void warn_if_state_damaged() const;
    /// A game that loaded with `warnings` (parts skipped, other game data) or without its running
    /// state: a note in the status line, the details are in the log.
    void report_load_problems(std::size_t warnings);
    /// What the mods give a game that does not bring its own (a new game, one from a file of the
    /// original): their factions and balance, and the installed packages as the game's.
    void give_mod_content(sim::World& world) const;
    /// Loads what the game's packages give beside the game, its messages and build menu, from the
    /// packages it names (all installed for a game that names none); packages installed in addition
    /// are not used (ADR 0007 section 7). A missing or different package is noted in the status line.
    void use_game_packages();
    /// What the main menu asked for this frame.
    void apply_menu();
    /// The end screen of a decided game, when it is due (EventPresenter::end_screen_due). Called
    /// every frame the game runs.
    void check_end_screen(std::uint64_t now);
    /// The window of a blueprint: what it shows, and its clicks as commands.
    void update_blueprint_window();
    /// The templates in the user's folder "templates", read on first use.
    TemplateStore& template_store();
    /// The templates (templates.hpp): the list read again from the user's folder and shown.
    void open_templates();
    /// The templates' list as the window shows it (after a change of the templates).
    void show_templates();
    /// The window of the templates: what was asked in it, saved, renamed, deleted or taken into
    /// the copy tool's hand.
    void update_template_window();
    /// The move tool at `spot`: the building it would pick up, or the one in hand drawn where it
    /// would go and taken off its old place, with the status line. Every tile changed in the map
    /// goes to `keep` first, which puts it back after drawing.
    void preview_move(const BuildTool::Spot& spot,
                      const std::function<void(std::uint16_t island, std::uint32_t x, std::uint32_t y)>& keep);
    /// The score sheets: the final screen of a decided game, or the "Bewertung" of a running one.
    void show_end_screen(sim::GameOutcome outcome, bool view_only);
    /// The high score list takes every human still playing, and `defeated` (who is
    /// out at this moment), enters the scenario's high score list.
    void update_high_scores(sim::PlayerId defeated);
    /// A won single player game opens the next mission.
    void record_progress();
    /// The game's identity, with the campaign and part of a scenario as the menu has them.
    [[nodiscard]] sim::GameIdentity game_identity() const;
    /// Humans that take part: more than one is a multiplayer game.
    [[nodiscard]] std::size_t humans() const;
    /// How the game stands for the local player now.
    [[nodiscard]] sim::GameOutcome local_outcome() const;
    /// Makes the start dialog's preview of a new world (its page of the islands).
    [[nodiscard]] GameSettingsMenu::PreviewMaker preview_maker(bool multiplayer) const;
    /// A new endless game of `level` (from the menu): name and banner of the human (one of the
    /// original's four or a further one), with the settings of the start dialog (rules, an own world).
    core::Status start_endless(std::uint8_t level, sim::BannerId banner, const std::string& name,
                               const EndlessSetup& setup);
    /// Open1602's own folder for savegames (the user's data folder, not the installation).
    [[nodiscard]] static std::filesystem::path save_folder();

    // ---- The editor (editor_mode.cpp, ADR 0013) ----
    /// Shows `world` in the editor, as a new document of `file` (empty: a new map). The game that
    /// ran goes; out of memory leaves everything as it was.
    core::Status enter_editor(sim::World world, const std::filesystem::path& file);
    /// The editor with the file of the command line (--editor), or a new map.
    void start_editor();
    /// Opens `file` in the editor (a file of Open1602 or of the original).
    core::Status editor_open(const std::filesystem::path& file);
    /// A new map in the editor.
    core::Status editor_new(const EditorNewMap& map);
    /// Saves the document under `file`; what happened goes to the panel's notice.
    void editor_save(const std::filesystem::path& file);
    /// Out of the editor, to the main menu.
    void leave_editor();
    /// Every frame of the editor: what its panel asked for, and the panel's model.
    void apply_editor();
    /// What the panel or a key asked for; a request that would lose changes asks first.
    void editor_request(const EditorRequest& request, bool confirmed = false);
    /// The model of the editor's panel for this frame.
    [[nodiscard]] EditorModel editor_model() const;
    /// The files the page "Open" lists: the own scenarios, then the game's.
    void list_editor_files();
    /// The editor's note in its panel and the status line (and the log, in English).
    void editor_notice(std::string text, bool error, std::string_view log);
    /// What the panel shows of the object picked on the map: its name and properties; nothing if
    /// it is gone (undone, another map).
    void editor_properties(EditorModel& model) const;
    /// The check list of the document as it is now.
    [[nodiscard]] std::vector<EditorCheckLine> editor_check() const;
    /// Over the editor's map: the islands' numbers and the mark of what is picked.
    void draw_editor_marks();
    /// The requests of the editor's islands: a new one, moved, turned, copied, taken out.
    void editor_island_request(const EditorRequest& request);
    void editor_save_island(std::size_t index);
    void editor_placeholder_request(const EditorRequest& request);
    void editor_load_island(const std::filesystem::path& file, std::uint8_t climate);
    [[nodiscard]] static std::filesystem::path own_island_folder();
    /// `op` done as a step of the document; false (and a note why) if it could not be made.
    bool editor_perform(core::Result<std::unique_ptr<editor::Operation>> op);
    /// The terrain piece chosen on the page "Terrain" (nothing if its group has none).
    [[nodiscard]] const assets::BuildingType* editor_terrain_piece() const;
    /// The terrain tool lays at the world tile (x, y) into `stroke`; `going_on`: the mouse moved
    /// there with the button held (a piece of several tiles is not laid over the last one).
    void editor_paint(editor::TerrainStroke& stroke, std::int64_t x, std::int64_t y, bool going_on);
    /// Room for land painted at the world tiles (x0, y0) to (x1, y1): the island there, or the
    /// nearest, grows over them with a margin, or a new island of open sea is made there. Each is a
    /// step of its own; the stroke drawn so far is one before it.
    void editor_make_room(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1);
    /// The stroke being drawn ends: one step of undo, or a note why nothing changed.
    void editor_end_stroke();
    /// The selection dragged from select_from to the world tile (x, y); false if they are not on one island.
    bool editor_select(std::int64_t x, std::int64_t y);
    /// The copy laid about the world tile (x, y) as one step.
    void editor_paste(std::int64_t x, std::int64_t y);
    /// The terrain tool's settings from the panel.
    void editor_terrain_request(const EditorRequest& request);
    /// The building chosen on the page "Buildings" (nothing if its group has none).
    [[nodiscard]] const assets::BuildingType* editor_object_piece() const;
    /// The page "Buildings" of the model: its settings, groups, building and owner.
    void editor_object_model(EditorModel& m) const;
    /// The buildings' tools' settings from the panel.
    void editor_objects_request(const EditorRequest& request);
    /// The order that places the chosen building about the world tile (x, y) for the chosen owner,
    /// free; nothing on the open sea or without players.
    [[nodiscard]] std::optional<sim::BuildOrder> editor_object_order(std::int64_t x, std::int64_t y) const;
    /// Places the chosen building about the world tile (x, y) into object_steps; `tell`: a note why
    /// not.
    bool editor_place_object(std::int64_t x, std::int64_t y, bool tell);
    /// A click or drag of the page "Buildings" on the map.
    bool editor_objects_stroke(std::optional<render::TilePos> tile, Stroke phase);
    /// The drag of the page "Buildings" ends: what it placed is one step of undo.
    void editor_end_objects();
    /// The pipette at the world tile (x, y).
    void editor_pipette(std::uint32_t x, std::uint32_t y);
    /// The islands changed: what the viewer keeps of them is made anew (flags, minimap), and what
    /// the panel shows of an island that is gone goes.
    void editor_islands_changed();
    /// Where an island being placed would lie with the mouse on `tile`: its top left.
    [[nodiscard]] std::pair<std::int64_t, std::int64_t> editor_place_origin(const render::TilePos& tile) const;
    /// An empty sea of `width` x `height` tiles with the mods' content, as behind the menu.
    [[nodiscard]] sim::World sea_world(std::uint32_t width, std::uint32_t height) const;

    // ---- Multiplayer (multiplayer.cpp, ADR 0011) ----
    /// Hosts or joins the multiplayer game of the command line.
    core::Status start_multiplayer();
    /// What the multiplayer session reported this frame: lines of the chat, the menu's screens.
    void apply_session_events();
    /// A chat line for each person's slot the computer took over since the last frame.
    void announce_take_overs();
    /// The game a host sent: replaces the world (also after a desync); nullptr if it cannot be read.
    sim::Simulation* load_from_host(std::span<const std::uint8_t> save);
    /// The same in two steps (net::ClientSession::load_in_background()): reads the game on a worker
    /// thread and returns what starts it on the game thread.
    [[nodiscard]] net::ClientSession::Finish prepare_host_game(std::vector<std::uint8_t> save);
    /// Starts the game a host sent, read as `world` (with the `warnings` of reading it).
    sim::Simulation* start_host_game(core::Result<sim::World> world, const std::vector<std::string>& warnings);
    /// Fingerprint of the game data the simulation depends on: computers of a multiplayer game
    /// must have the same.
    [[nodiscard]] std::uint64_t content_fingerprint() const;
    /// The multiplayer menu was opened from the main menu: the search starts.
    /// The original's cursors from Language.dll (the system's where it has none).
    void load_cursors();
    /// Arrow, target while something waits for a click on the map, none during a full-screen video.
    void update_cursor(bool video_full_screen);
    void open_multiplayer_menu();
    /// The multiplayer menu: its model, what it asked for, the search on the local network.
    void apply_multiplayer_menu();
    /// How far the game from the host has come, into the multiplayer menu's model.
    void join_to_model();
    void handle_multiplayer(const MultiplayerEvent& e);
    /// Answers searches for the hosted game, in the menu and in the game.
    void update_lan_beacon();
    /// Opens a lobby for `setup` on its port.
    core::Status host_lobby(const HostSetup& setup);
    /// The host starts the game of its lobby: makes the world, gives the slots their players.
    core::Status start_hosted_game();
    /// Joins the host at `address`; with `fetch_packages` it asks the host for the packages of the
    /// game this computer lacks, which the person agreed to download (docs/modding/packages.md).
    core::Status join_game(const std::string& address, std::uint16_t port, const std::string& password,
                           bool fetch_packages = false);
    /// The host has packages of its game this computer lacks and would send them: asks the person,
    /// the answer joins again with them or goes back to the search (m_fetch_offer).
    void offer_packages(std::span<const assets::ContentPackage> packages, std::uint64_t bytes, std::string refusal);
    /// The person's answer to offer_packages().
    void answer_package_offer(bool yes);
    /// The packages this computer has, its own and those received, as a host sends them to
    /// computers that lack them.
    [[nodiscard]] std::vector<net::SharedPackage> shared_packages() const;
    /// The screen "Mods": the packages of the mods folder with their state, then those received.
    [[nodiscard]] ModsModel mods_model() const;
    /// Loads the mods folder's packages anew after one was turned on or off or added: what new
    /// games get from them (a running game keeps what it has).
    void reload_mods();
    /// Copies the received package in `folder` into the mods folder; what the screen "Mods" says.
    std::string adopt_mod(const std::filesystem::path& folder);
    /// Turns the package `id` of the mods folder on (with the packages it needs) or off, and
    /// reloads the packages; returns what the screen "Mods" tells about it (Windows-1252).
    std::string toggle_mod(const std::string& id);
    /// The scenarios and campaigns of m_packages (docs/modding/scenarios.md), for the menu.
    void load_package_scenarios();
    /// Starts the package scenario `id`: its map with its money and goals, its script with the
    /// packages' (`rules`: of the start dialog, else the options'). As part `part` (from 0) of the
    /// package campaign `campaign`, if not empty, with what the part before left.
    core::Status start_package_scenario(const std::string& id, const sim::Rules* rules,
                                        const std::string& campaign = {}, std::uint8_t part = 0);
    /// A won part of a package's campaign: opens the next part in `progress` and keeps what it
    /// leaves to it (campaign_carry_file).
    void record_campaign_part(assets::CampaignProgress& progress, const sim::World::CampaignPart& part);
    /// The packages a game may use here: those in `mods`, then those received from hosts.
    [[nodiscard]] std::vector<assets::ModPackage> known_packages() const;
    /// Drops the session, its beacon and the hosted game; the menus stay as they are.
    void end_session();
    /// Out of the session, back to the search (with `notice`, why).
    void leave_session(std::string notice = {});
    /// The host of the running game left: the game moves on to the successor the host named (host
    /// migration): this computer hosts it on its port, or follows the successor there. False if it
    /// cannot (no successor, the port is taken).
    bool migrate();
    /// The host of the running game left and the game cannot move on: the client saves the game it
    /// holds as a whole and says where; loaded, it plays on alone or is hosted anew.
    void save_after_host_left();
    /// A player's line for the chat (add_chat_line), to everyone or private; a notice of whom a
    /// private line of this computer's player missed.
    void chat_line(const net::SessionEvent& e);
    /// The names of slots, joined with commas (the lobby's, else the world's players').
    [[nodiscard]] std::string seat_names(const std::vector<sim::PlayerId>& seats) const;
    /// What the chat over the map shows besides its lines, and the lines it sent (every frame).
    void update_chat();
    /// A notice of the session in the chat, in the log by `log` ("multiplayer: <log>", English).
    void session_line(std::string text, std::string_view log, sim::PlayerId seat = sim::kNoPlayer);
    /// The same for a text of the multiplayer catalogue with its "{1}".
    void session_line(MpText id, std::string_view value = {}, sim::PlayerId seat = sim::kNoPlayer);
    [[nodiscard]] net::HostSession* host_session() const noexcept;
    [[nodiscard]] net::ClientSession* client_session() const noexcept;
    /// Closes the lobby for the game: it runs now on this computer.
    void close_lobby();

    /// Tells the sound where the view is (every frame, before events are heard).
    void place_listener();
    /// Gives the map the window without the frame.
    void lay_out();
    /// The window went to a screen with another scale (or the desktop's scale changed): the map's
    /// pixel scale follows; a change the player made with the keys stays on top of it.
    void follow_display_scale();
    /// The render device was reset (a driver update, a GPU timeout, sleep on some laptops): every
    /// texture lost its contents, all are made again.
    void recreate_textures();
    /// The render device was lost: the window's renderer is made again, everything that draws
    /// takes the new one and makes its textures again. Without a renderer the game ends.
    void recreate_renderer();
    /// The world's sprites of every zoom level go and are loaded again in the background; the map
    /// stays black meanwhile (m_graphics_reloading).
    void reload_graphics();
    /// What a click on the frame asked for.
    void apply_hud(const Hud::Action& action) override;
    /// The resource bar: money, and goods and inhabitants of the city under the mouse.
    [[nodiscard]] Hud::Resources resources() const;
    /// Shots, hits, victory and defeat: sounds, messages and lines in the terminal.
    void apply_game_events();
    /// What the game's scripts posted and printed since the last frame: messages for this computer's
    /// player, script errors in the status line, everything in the log.
    void apply_script_output();
    /// Plays what the EventPresenter made of the events: messages with their voices (a message that
    /// asks for it slows the game to normal speed, flag 0x02) and videos, high score lists.
    void show_notices(const std::vector<Notice>& notices);
    /// Posts a message by its id ("mymod:lighthouse") with its placeholders filled, as the scripts
    /// do (game.message); false if there is no such message.
    bool announce(std::string_view id, const MessageArgs& args = {},
                  std::optional<std::pair<double, double>> tile = std::nullopt,
                  std::optional<sim::PlayerId> about = std::nullopt);
    void zoom_by(double steps, double sx, double sy) override;
    void scale_by(int direction, double sx, double sy) override;
    /// Free zoom: goes on towards the magnification asked for (smoothly, a few frames).
    void animate_zoom(std::uint32_t delta_ms);
    /// Shows a magnification of the free zoom around (sx, sy); its level's graphics may have to be
    /// loaded first.
    void show_magnification(double magnification, double sx, double sy);
    /// The map's sprites of this frame onto the screen: straight, or through m_map_canvas at a
    /// scale that is no whole number.
    void draw_map(ZoomGraphics& graphics);
    /// Applies how the game is shown (the window "Anzeige", F11): full screen, the window's size,
    /// the interface's size, the zoom and vertical sync. Saved with the other settings.
    void apply_display(const DisplaySettings& display);
    /// Every frame: keeps the mouse in the window (DisplaySettings::grab_mouse, or full screen
    /// while edge scrolling can act) or lets it go.
    void follow_mouse_grab(bool video_full_screen);
    /// What the window "Anzeige" shows: the settings and the window's size now.
    [[nodiscard]] DisplayModel display_model() const;
    /// The window's size went to the settings: remembered for the next start.
    void remember_window_size();
    /// What the world dims: while building the land the player cannot build on, or everything
    /// outside the circle of the building being placed or of the player's clicked building.
    [[nodiscard]] render::TileLighting tile_lighting() const;
    /// A catchment given in tiles of `island` in world tiles.
    [[nodiscard]] sim::Catchment world_catchment(std::uint16_t island, sim::Catchment area) const;
    /// Lighting of the local player's clicked building `clicked`: the land inside its circle (and a
    /// Kontor's harbour); clicked again, inside the circles of all the player's buildings of its type
    /// on the island. None for other buildings and those without a circle.
    [[nodiscard]] std::optional<render::TileLighting> building_lighting(const Selection& clicked) const;
    /// The status line over the map under the mouse: the unit or what stands on the tile.
    [[nodiscard]] std::string tile_status() const;
    /// Results of building and demolishing (messages, flags of new buildings).
    void apply_construction();
    /// Animation phases of buildings that started or stopped work and the texts over buildings
    /// whose store gained a ton or is full (presentation only).
    void apply_production_events();
    /// Game time of the frame for the texts over the buildings, with the share of the next tick.
    [[nodiscard]] double stock_note_time() const;
    /// The texts over the buildings (m_stock_notes), over the map and under the interface.
    void draw_stock_notes();
    /// Volcano fire and lava of the current game time (m_effects).
    void collect_effects();
    /// Townsfolk, sea creatures and jugglers of the game time since the last frame.
    void update_ambient();

    /// The pixel scale the screen asked for, last time it was looked at.
    std::int32_t m_display_pixel_scale = 1;
    /// Free zoom: the magnification the wheel asked for and the screen point it keeps in place.
    double m_zoom_target = 0.0;  ///< 0: none asked for
    double m_zoom_anchor_x = 0.0;
    double m_zoom_anchor_y = 0.0;

    assets::GameDirectory m_game;
    /// The buildings as the running game has them: m_original_buildings with the values of its balance
    /// (sim::apply_building_values, docs/modding/balance.md).
    assets::BuildingTable m_buildings;
    assets::BuildingTable m_original_buildings;  ///< as loaded (with the data fixes), for the next game
    assets::Palette m_palette;
    /// The figures as the running game has them: m_original_figures with the values of its balance
    /// (sim::apply_figure_values, docs/modding/values.md).
    assets::FigureTable m_figures;
    assets::FigureTable m_original_figures;  ///< as loaded (with the data fixes), for the next game
    render::BannerTable m_banners;           ///< colours and flags of the players (ADR 0007)
    MessageTable m_messages;                 ///< the original's messages and the mods' (docs/modding/messages.md)
    /// The factions a new game starts with: the base game's as the mods change them
    /// (docs/modding/factions.md). A savegame of Open1602 keeps its own.
    std::vector<sim::Faction> m_factions = sim::base_factions();
    std::vector<sim::FactionReplacement> m_faction_replacements;
    /// The buildings the packages added and the Ids the tables gave them (docs/modding/own-buildings.md);
    /// a game keeps them (section OWNB), so its tiles name the same buildings again.
    std::vector<assets::OwnBuilding> m_own_buildings;
    /// The figures the packages added, with the numbers m_original_figures gave them.
    std::vector<assets::OwnFigure> m_own_figures;
    std::vector<assets::OwnUnit> m_own_units;  ///< the unit families among them, for the castles' windows
    std::vector<assets::OwnShip> m_own_ships;  ///< the ships among them, for the shipyards' windows
    /// The goods a new game trades: the original's and the packages' (docs/modding/goods.md).
    assets::Goods m_goods;
    /// The classes of inhabitants of a new game as the packages change and add them, empty when
    /// none does (docs/modding/classes.md).
    std::vector<assets::ResidentClass> m_classes;
    /// The unlock levels of a new game as the packages change and add them, empty when none does
    /// (docs/modding/levels.md).
    std::vector<assets::InfraLevel> m_levels;
    /// The images of the packages installed, with their sprite numbers (docs/modding/graphics.md).
    std::vector<assets::PackageImage> m_package_images;
    /// The figures the packages draw with their images (figure_images.toml), applied to m_figures.
    std::vector<assets::FigureLook> m_figure_looks;
    /// The content packages installed in <data folder>/mods, in their load order (docs/modding/packages.md).
    std::vector<assets::ModPackage> m_packages;
    /// Packages received from hosts, in <data folder>/mods-cache: used only by games that name them.
    std::vector<assets::ModPackage> m_cached_packages;
    /// The scenarios of m_packages, in their load order.
    std::vector<assets::PackageScenario> m_package_scenarios;
    std::vector<assets::PackageCampaign> m_package_campaigns;
    sim::Balance m_balance;  ///< the numbers a new game starts with, as the mods change them (docs/modding/balance.md)
    /// Flags, smoke and icons: buildings do not move, so they are collected once at load.
    std::vector<render::BuildingFigure> m_building_figures;
    std::vector<std::string> m_city_names;  ///< Text.cod [STAEDTE], [SHIPS]: for every game loaded
    std::vector<std::string> m_ship_names;
    std::vector<std::string> m_campaign_labels;     ///< [KAMPAGNE]: the high score lists of campaign parts
    std::optional<CampaignPart> m_campaign_part;    ///< of the scenario loaded, as the original records it
    sim::PlayerId m_local_player = sim::kNoPlayer;  ///< whose buildings show status icons
    render::SoldierFigures m_soldiers;              ///< land units in their owners' colours
    render::WalkerFigures m_walkers;                ///< carriers, carts, workers, deer and hunters
    render::CartFigures m_carts;                    ///< the players' trade carts
    render::ShipTrail m_ship_trail;                 ///< ships drawn between two ticks
    double m_tick_fraction = 1.0;                   ///< share of the next tick that has passed
    std::uint64_t m_title_real_ms = 0;              ///< real and game time of the last title (the speed reached)
    std::uint64_t m_title_game_ms = 0;
    AutosaveTimer m_autosave;
    /// An autosave on its way to the disk (packing and writing on another thread).
    struct AutosaveJob {
        std::future<core::Status> written;
        std::uint8_t humans = 0;
        std::int32_t minutes = 0;
        std::uint64_t capture_us = 0;  ///< the part in the frame
        std::chrono::steady_clock::time_point started;
    };
    std::optional<AutosaveJob> m_autosave_job;
    bool m_debug_title = core::environment_variable("O1602_DEBUG_TITLE").has_value();
    ViewerOptions m_options;
    std::unique_ptr<GameState> m_state;  ///< the running game (after m_buildings, m_figures, m_options)
    /// Messages, the end screen and the notes of the status line (quick save, screenshot, errors).
    EventPresenter m_events;
    /// A multiplayer game: drives the simulation in place of Simulation::advance().
    std::unique_ptr<net::Session> m_session;
    bool m_host_game_loaded = false;                ///< this client runs the game its host sent
    std::vector<sim::PlayerRole> m_session_roles;   ///< who steered each player last frame (empty: not yet)
    std::string m_host_address;                     ///< of the host joined
    std::uint16_t m_host_port = net::kDefaultPort;  ///< of the host joined: the successor hosts on it too
    /// The slot this computer last played and its token, at the host it was taken in by: joining the
    /// same host again asks for that slot back (MS-4), for as long as the program runs.
    struct Rejoin {
        std::string address;
        std::uint16_t port = net::kDefaultPort;
        sim::PlayerId seat = sim::kNoPlayer;
        std::uint64_t token = 0;
    };
    std::optional<Rejoin> m_rejoin;
    /// Packages a host would send, waiting for the person's answer, and where to join with them.
    struct FetchOffer {
        std::string address;
        std::uint16_t port = net::kDefaultPort;
        std::string password;
        std::string refusal;  ///< the host's refusal, shown if the person says no
    };
    std::optional<FetchOffer> m_fetch_offer;
    /// A game loaded alone that waits for the person to choose their player (player_choice).
    struct PendingLoad {
        sim::World world;
        std::filesystem::path path;
        std::optional<sim::Rules> rules;
        std::size_t warnings = 0;
        std::vector<sim::PlayerId> players;  ///< the options, in order
        std::vector<Hud::ChoiceOption> options;
    };
    std::optional<PendingLoad> m_pending_load;
    std::string m_join_password;       ///< of the game joined last, for joining it again with its packages
    bool m_fetching_packages = false;  ///< the game joined last was asked for its packages
    bool m_migrating = false;          ///< following a successor, its game not here yet
    std::string m_successor_name;      ///< the one followed
    /// The local player's commands: to m_state's simulation, or to m_session while there is one.
    CommandSink m_command_sink;
    std::unique_ptr<net::LanBrowser> m_lan_browser;  ///< while the multiplayer menu looks for games
    std::unique_ptr<net::LanBeacon> m_lan_beacon;    ///< while this computer hosts a game shown on the network
    std::uint64_t m_lan_search_ms = 0;               ///< real time of the last search
    std::uint64_t m_lan_update_ms = 0;               ///< real time of the last look at the answers
    std::optional<HostSetup> m_hosted;               ///< the game this computer hosts
    std::optional<sim::World> m_hosted_world;        ///< its scenario or savegame until the start

    std::unique_ptr<platform::SdlSystem> m_sdl;
    std::unique_ptr<platform::Window> m_window;
    /// The map at a scale that is no whole number (after the window, before SDL quits).
    std::unique_ptr<render::SdlMapCanvas> m_map_canvas;
    bool m_software_renderer = false;  ///< SDL draws without a GPU
    /// After a device reset: the game goes on with a black map and the interface until the
    /// background has the zoom level shown, instead of standing still for seconds (FB-11).
    bool m_graphics_reloading = false;
    platform::MouseCursors m_cursors;  ///< the original's arrow and target (after the window, before SDL quits)
    std::array<std::optional<ZoomGraphics>, 3> m_graphics;
    std::unique_ptr<render::GraphicsPreloader> m_preloader;  ///< the CPU side of the zoom levels

    render::Camera m_camera;
    render::AnimationClock m_clock;
    render::ProductionPhases m_production_phases;  ///< animation phases, presentation only
    StockNotes m_stock_notes;                     ///< "Lager: 3", "Lager voll" rising over the local player's buildings
    std::vector<render::DrawCommand> m_commands;  ///< reused every frame
    render::DrawListScratch m_draw_scratch;       ///< reused every frame

    /// The editor's document while the editor runs (ADR 0013): it edits m_state's world.
    struct EditorSession {
        editor::Document document;
        std::filesystem::path file;  ///< where Save writes; empty: not saved as Open1602's yet
        std::string source_name;     ///< the name of the file opened (Windows-1252), empty: a new map
        std::string notice;          ///< Windows-1252
        bool notice_error = false;
        EditorPick pick{};                     ///< what the panel's page "Selection" shows
        std::vector<EditorCheckLine> check{};  ///< the check list, made when its page opened
        /// An island being placed: the map's next click puts it down.
        struct Placing {
            enum class Kind : std::uint8_t { New, Move, Copy, NewPlaceholder, MovePlaceholder };
            Kind kind = Kind::New;
            std::size_t index = 0;                       ///< Move, Copy: the island; MovePlaceholder: the placeholder
            assets::ScenarioPlan::Island placeholder{};  ///< NewPlaceholder, MovePlaceholder
            std::optional<sim::WorldIsland> island;      ///< New: the island made
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            EditorPanel::Page page = EditorPanel::Page::Islands;  ///< leaving it puts the island back
        };
        std::optional<Placing> placing{};
        EditorTerrain terrain{};  ///< the terrain tools' settings
        /// A stroke of the brush being drawn, where it laid last and the last piece of several
        /// tiles laid (its world tile).
        std::unique_ptr<editor::TerrainStroke> stroke{};
        /// What the stroke did before it made room for its land, and the room made: one step with it.
        std::vector<std::unique_ptr<editor::Operation>> stroke_steps{};
        std::optional<std::pair<std::int64_t, std::int64_t>> stroke_at{};
        std::optional<std::pair<std::int64_t, std::int64_t>> stroke_large{};
        std::uint64_t strokes = 0;  ///< strokes drawn, for the random fill's pieces
        /// The terrain selected (tool Select): the island and its tiles x0..x1, y0..y1; the world
        /// tile the drag began at; the copy (the clipboard).
        struct TerrainSelection {
            std::uint16_t island = 0;
            std::uint32_t x0 = 0;
            std::uint32_t y0 = 0;
            std::uint32_t x1 = 0;
            std::uint32_t y1 = 0;
        };
        std::optional<TerrainSelection> selection{};
        std::optional<std::pair<std::int64_t, std::int64_t>> select_from{};
        editor::TerrainClip clip{};
        bool paste_refused = false;  ///< the preview: a click where the mouse is would be refused
        EditorObjects objects{};     ///< the buildings' tools' settings
        /// What a drag of the page "Buildings" placed so far (done already), and where it placed last.
        std::vector<std::unique_ptr<editor::Operation>> object_steps{};
        std::optional<std::pair<std::int64_t, std::int64_t>> object_at{};
        bool object_drag = false;
    };
    std::optional<EditorSession> m_editor;
    /// A request of the editor waiting for the answer to "discard the changes?".
    std::optional<EditorRequest> m_editor_pending;
    /// The files of the page "Open", in the order of the panel's list.
    std::vector<std::filesystem::path> m_editor_files;
    std::vector<EditorFile> m_editor_file_rows;
    EditorNewMap m_editor_new;  ///< the settings of the last new map
    bool m_running = true;
    bool m_show_owners = false;              ///< ownership overlay (key O)
    bool m_show_island_numbers = false;      ///< the islands' numbers over the map (editor)
    bool m_show_fps = false;                 ///< the frame rate in the map's corner (Ctrl+F)
    bool m_show_coordinates = false;         ///< the tile under the mouse in the map's corner (Ctrl+K)
    bool m_show_unit_arms = false;           ///< coats of arms over the own land units (M)
    bool m_show_fertility = false;           ///< fertility view (Ctrl+B): the build tool's or selected farm's crop
    double m_fps = 0.0;                      ///< frames a second, measured for the title
    bool m_quit_pending = false;             ///< "Ende" waits for the answer to its question
    AppSettings m_app_settings;              ///< settings.cfg in the user's data folder
    SaveSlots m_slots;                       ///< names of the savegames of the panel DISK
    std::uint64_t m_slots_revision = 0;      ///< counts the changes of m_slots
    std::uint64_t m_slots_shown = 0;         ///< the revision the menu has
    const MainMenu* m_slots_menu = nullptr;  ///< the menu that has it
    std::size_t m_sprites_drawn = 0;
    std::size_t m_batches_drawn = 0;  ///< draw calls of the map's sprites in the last frame (FB-7)
    BuildTool m_tool;
    std::optional<TemplateStore> m_templates;  ///< made on first use (template_store)
    /// Until then a note in the status line goes before what the build tool would do (a template saved).
    std::uint64_t m_notice_over_tool_until = 0;
    static constexpr std::uint64_t kNoticeOverToolMs = 3000;
    std::string m_tool_status;          ///< what the build tool would do at the mouse
    bool m_tool_short_of_wood = false;  ///< ... and that it lacks wood for it (the tutorial's bubble)
    std::size_t m_load_problems = 0;    ///< of the game loaded last (report_load_problems)
    struct ScreenshotRequest {
        std::filesystem::path path;
        bool notify = false;  ///< say in the status line how it went (F12)
    };
    std::optional<ScreenshotRequest> m_screenshot;  ///< read in the next present()
    std::optional<std::string> m_screenshot_error;  ///< why the last one failed
    std::vector<render::EffectFigure> m_effects;    ///< volcano fire, lava, fights, scenery of this frame
    render::Ambient m_ambient;                      ///< life in the picture, no part of the game
    render::CombatEffects m_combat;                 ///< hits, cannon smoke and wrecks, only a picture
    std::unique_ptr<GameSound> m_sound;             ///< effects, voices and music (after the window)
    std::unique_ptr<Hud> m_hud;                     ///< panel and status bar (after the window), may be null
    /// Mouse and keys on the map, the selection (after the window, the sound and the panel).
    std::unique_ptr<MapInput> m_input;
    std::vector<render::TileMark> m_tile_marks;  ///< red ground under what cannot be built, a dragged field
    render::SpriteTints m_sprite_tints;          ///< the blueprints and the blueprint preview, this frame
    render::SpriteTints m_sprite_checker;        ///< buildings being placed or moved, this frame
    render::TileLighting m_lighting;             ///< tiles drawn darker this frame
    /// The circles of the clicked building (catchment_circles), kept while the selection and the
    /// buildings stay the same: for a type without production entries (towers) they come from a
    /// search of the whole island.
    struct CircleCache {
        Selection clicked;
        bool same_type = false;
        std::uint64_t construction = 0;
        std::optional<CatchmentCircles> circles;
        bool valid = false;
    };
    mutable CircleCache m_circle_cache;
    std::uint64_t m_construction_serial = 0;       ///< counts the frames in which buildings came or went
    std::optional<sim::BuildBill> m_pending_bill;  ///< costs of the order the build tool shows
    sim::PlayerFigures m_player_figures;           ///< the info panel's figures, refreshed once a second
    std::int64_t m_player_points = 0;              ///< the info panel's "Punkte", refreshed with them
    std::uint64_t m_figures_ms = 0;
    /// The status line under the mouse, worked out when the mouse moves or 10 times a second
    /// (FB-1: it picks the unit under the mouse).
    std::string m_tile_status;
    render::TileBox m_drawn_tiles;  ///< what may be on screen, from update_ambient (FB-9, FB-10)
    std::uint64_t m_tile_status_ms = 0;
    std::pair<double, double> m_tile_status_mouse{-1.0, -1.0};
    /// The videos (after the window and the sound); plays none without videos.
    std::unique_ptr<VideoController> m_video;
};

}  // namespace o1602::app
