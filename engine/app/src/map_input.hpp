// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input of the map view (ADR 0010): the key table, selecting ships and land units with the left
// button (click, rectangle, Shift, Ctrl, double click), orders with the right button (Shift: queued;
// also on the minimap), picks for attack and patrol, and the questions before what cannot be taken
// back. The original's mouse (orders with the left button) is an option. Owns what the mouse and
// the keys are doing and the selection; works on the viewer's camera, build tool and panel, and
// asks the viewer (Host) for what only it can do.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "build_tool.hpp"
#include "cheat_console.hpp"
#include "command_sink.hpp"
#include "game_sound.hpp"
#include "game_state.hpp"
#include "hud.hpp"
#include "map_gestures.hpp"
#include "zoom_graphics.hpp"

#include "o1602/platform/window.hpp"
#include "o1602/render/camera.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/ui/key_bindings.hpp"
#include "o1602/ui/unit_selection.hpp"

union SDL_Event;

namespace o1602::app {

/// The largest pixel scale (Ctrl +, and the screen's scale).
inline constexpr std::int32_t kMaxPixelScale = 4;
/// Wheel notches a zoom key or button stands for (free zoom: two notches are half a level).
inline constexpr double kKeyZoomSteps = 2.0;

class MapInput {
public:
    /// What only the viewer can do; the input asks it.
    class Host {
    public:
        virtual ~Host() = default;
        /// What a click on the frame asked for (also the modes the input switches to).
        virtual void apply_hud(const Hud::Action& action) = 0;
        /// Zooms in (negative) or out around a screen point by notches of the mouse wheel (parts of
        /// one from a touchpad); the zoom level's graphics may have to be loaded.
        virtual void zoom_by(double steps, double sx, double sy) = 0;
        /// Ctrl +/-: the map twice as large or half as large (`direction` +1, -1).
        virtual void scale_by(int direction, double sx, double sy) = 0;
        /// F5 and F9: the quick savegame.
        virtual void quick_save() = 0;
        virtual void quick_load() = 0;
        /// F12: a screenshot of the next frame, said in the status line.
        virtual void request_screenshot() = 0;
        virtual void quit() = 0;
        /// Ctrl+O: the ownership overlay.
        virtual void toggle_owners() = 0;
        /// Ctrl+B: the fertility view for the crop of the selected farm (fertility_view.hpp).
        virtual void toggle_fertility() = 0;
        /// Ctrl+F, Ctrl+K: the frame rate, the tile under the mouse in the corner of the map.
        virtual void toggle_fps() = 0;
        virtual void toggle_coordinates() = 0;
        virtual void toggle_unit_arms() = 0;
        /// F11, Alt+Enter: window or full screen.
        virtual void toggle_fullscreen() = 0;
        /// The Pause key: the original's pause with its video (without videos the game still stops).
        virtual void toggle_pause_video() = 0;
        /// A short note in the status line (EventPresenter::notify).
        virtual void notify(std::string text, std::string_view log) = 0;
        /// Where Home goes without a selection.
        [[nodiscard]] virtual render::TilePos home_tile() const = 0;
        /// The graphics of the zoom level shown; null while they are not there.
        [[nodiscard]] virtual const ZoomGraphics* graphics() const = 0;
        /// The pictures of a unit as drawn; nothing if it is not drawn.
        [[nodiscard]] virtual std::optional<render::FigureSprites> unit_sprites(const ui::UnitRef& ref) const = 0;
        /// The map tiles whose pictures have a visible pixel at world pixel (px, py) of the zoom
        /// level shown, the front-most first (render::tiles_at_pixel); none while its graphics are
        /// not there.
        virtual void tiles_at_pixel(std::int32_t px, std::int32_t py, std::vector<render::TileHit>& out) const = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
    };

    /// The viewer's parts the input works on. All outlive the input; the game in `state` and
    /// `local_player` change when another game is loaded.
    struct Parts {
        Host& host;
        const assets::BuildingTable& buildings;
        const assets::FigureTable& figures;
        const std::unique_ptr<GameState>& state;
        const sim::PlayerId& local_player;
        render::Camera& camera;
        BuildTool& tool;
        CommandSink& commands;
        GameSound& sound;
        platform::Window& window;
        Hud* hud;  ///< null without the panel
        /// The original's mouse: the left button gives orders too, the right one only cancels.
        bool original_mouse = false;
        /// Ask before scuttling, disbanding and declaring war by an attack.
        bool confirm = true;
        /// The key settings file of the command line (--keys); empty: the one in the user's folder.
        std::filesystem::path keys_file;
    };

    explicit MapInput(const Parts& parts);
    MapInput(const MapInput&) = delete;
    MapInput& operator=(const MapInput&) = delete;

    /// Applies the key settings file (--keys, else `default_file`) over the defaults, or writes
    /// `default_file` with them.
    void load_key_settings(const std::filesystem::path& default_file);

    /// Input events but the window's (mouse positions in output pixels).
    void handle_event(const SDL_Event& event);
    /// The left button went up while a video took the input.
    void release_left() noexcept { m_left_down = false; }
    /// Every frame: the map scrolls while its keys are held.
    void scroll_with_keys(std::uint32_t delta_ms);
    /// Every frame with edge scrolling on: the map scrolls at `speed_percent` of the keys' speed
    /// while the mouse rests at the screen's edge, where it cannot leave the window
    /// (Window::mouse_confined), and no window or menu takes the mouse.
    void scroll_at_edges(std::uint32_t delta_ms, std::int32_t speed_percent);
    /// A menu, a question or a window over the map takes the mouse, or a name is typed: no edge
    /// scrolling now.
    [[nodiscard]] bool edge_scroll_paused() const noexcept;
    /// Every frame, in the menu as in the game: the platform's text input runs exactly while a
    /// name is typed.
    void sync_text_input();
    /// Every frame of the game: a window changed the selection (filters, formations), or units
    /// died, sank or changed hands: the windows follow.
    void follow_selection();

    /// The selection changed: the windows follow (a single ship in the info mode, groups and land
    /// units in the military mode).
    void selection_changed(bool box);
    /// The units are no longer selected: from the military mode back to the player status.
    void leave_military_mode();
    /// Orders to the simulation; those that cannot be taken back wait for an answer (ADR 0010).
    void submit_orders(std::vector<sim::Command> orders, bool sound = true);
    /// The field of the farm (plantation, forester) at the island tile on the open ground it
    /// reaches, as far as the money goes; nothing for another building.
    void plant_fields_at(std::uint16_t island, std::int32_t x, std::int32_t y);
    /// The answer to the question before the waiting orders.
    void answer(bool yes);
    /// Blueprint mode on or off (U, the build menu's switch): the build and copy tools then place
    /// blueprints. Switched on outside the build mode, it opens the build menu.
    void toggle_blueprint();
    /// A building (or the demolish tool) was taken from the menu: the selected ships and units
    /// are let go, so a right click puts the building away instead of giving them an order.
    void drop_units();
    /// Another game: no selection, pick, order or press of the old one is left.
    void reset_game();
    /// The groups the player stored, for the savegame, and back from a loaded one.
    [[nodiscard]] sim::UnitGroups saved_groups() const { return m_units.saved_groups(); }
    void restore_groups(const sim::UnitGroups& groups) { m_units.restore_groups(groups); }

    /// Health bars over the selected units and the selection rectangle.
    void draw_overlay();

    /// World tile the player sees under a screen point: its ground diamond drawn at its height, so
    /// the land (raised above the sea) is picked where it is drawn.
    [[nodiscard]] std::optional<render::TilePos> tile_at(double sx, double sy) const;
    /// Map tile under the mouse; none while a window takes all of the mouse (a question).
    [[nodiscard]] std::optional<render::TilePos> tile_at_mouse() const;
    /// Island tile under the mouse, nullopt over the sea.
    [[nodiscard]] std::optional<BuildTool::Spot> spot_at_mouse() const;
    /// The ship or land unit under the mouse, as a click would pick it.
    [[nodiscard]] std::optional<ui::UnitRef> unit_at_mouse() const;
    [[nodiscard]] double mouse_x() const noexcept { return m_mouse_x; }
    [[nodiscard]] double mouse_y() const noexcept { return m_mouse_y; }
    [[nodiscard]] ui::UnitSelection& units() noexcept { return m_units; }
    [[nodiscard]] const ui::UnitSelection& units() const noexcept { return m_units; }
    /// The number line of the cheats (drawn by the viewer while open).
    [[nodiscard]] const CheatConsole& cheats() const noexcept { return m_cheats; }

    /// A pick the next click on the map answers: an attack (A) or a patrol (P).
    enum class Pick : std::uint8_t { None, Attack, Patrol };
    [[nodiscard]] Pick pick() const noexcept { return m_pick; }

private:
    /// The keys of ADR 0010.
    void setup_keys();
    /// F1: the keys as they are bound now, over the map.
    void show_key_help();
    void handle_key(int key, int scancode, std::uint16_t mod);
    [[nodiscard]] ui::KeyContext key_context() const;
    void run_action(const ui::KeyBinding& binding);
    /// Switches the panel to a mode (and what that asks of the viewer).
    void set_mode(std::int32_t mode);
    /// Tells the panel which building is under the mouse for its window (none: the player status).
    void select_building_at_mouse(const Selection& before);
    /// The key "Plant all farm fields": the selected farm's field on the open ground it reaches.
    void plant_fields();
    /// Shift+U: the local player's blueprints of the city under the mouse are built.
    void build_blueprints();
    /// A click on one of the local player's blueprints opens its window; false if there is none.
    bool open_blueprint_at_mouse();
    /// G: closes the local player's gates to foreign soldiers, or opens them again.
    void toggle_gates();
    /// Closes the open pop-up of a build category, else lays the building in hand away (the
    /// original's right click). False if there was neither.
    bool cancel_build_step();
    /// A right click on the panel, or on the map with nothing to order: straight back to the
    /// player status, the general status page, from any mode, window or page of one. False if it
    /// was showing already (the click is an order then).
    bool back_to_status();
    /// Pages a window or the settings go back at most in one click (a guard against a loop).
    static constexpr int kMaxBackSteps = 8;
    /// Screen position of a world position in tiles.
    [[nodiscard]] std::pair<double, double> screen_of(double tx, double ty) const;
    /// World pixel of the zoom level shown under a screen point.
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> world_pixel(double sx, double sy) const;
    /// Which buildings a pick looks for: the tile of island `island` and what stands on it.
    using BuildingFilter = std::function<bool(std::uint16_t island, const sim::SubTile& tile)>;
    /// The building under a screen point by its picture: the front-most tile whose visible pixel is
    /// there and that `wanted` accepts. Pictures it does not accept (ground, trees, roads, ...) do
    /// not hide what stands behind them. Where no accepted picture is hit (the graphics not loaded
    /// yet, a gap in a picture), the tile under the point if accepted, with the row of
    /// kBehindEverything.
    [[nodiscard]] std::optional<render::TileHit> building_tile_at(double sx, double sy,
                                                                  const BuildingFilter& wanted) const;
    static constexpr std::int32_t kBehindEverything = std::numeric_limits<std::int32_t>::min();
    /// Where a unit is drawn on screen: its box (x, y, w, h).
    [[nodiscard]] std::optional<std::array<double, 4>> unit_box(const ui::UnitRef& ref) const;
    /// Ships and land units whose picture lies in the rectangle (screen), with the part inside.
    [[nodiscard]] std::vector<ui::UnitCandidate> units_in(double x0, double y0, double x1, double y1) const;
    [[nodiscard]] std::vector<ui::UnitCandidate> without_carts(std::vector<ui::UnitCandidate> units) const;
    /// The unit under a screen point: the one whose visible pixel is there, unless a map tile is
    /// drawn over it; zoomed out, else a small one near it, the local player's first.
    [[nodiscard]] std::optional<ui::UnitRef> unit_at(double sx, double sy) const;
    /// A key for the cheats' number line or a cheat (cheat_console.hpp); whether it took it.
    bool cheat_key(int pressed, int scancode, std::uint16_t mod);
    /// Left button released on the map (no tool): a click or the end of a rectangle.
    void map_left_up(double x, double y, std::uint16_t mod, int clicks);
    /// An order of the selection to the screen point (right button; the left one with the
    /// original's mouse); `queue` with Shift, `on_object` with Ctrl (a tree or field is shot down).
    void map_order(double x, double y, bool queue, bool on_object = false);
    /// An order of the selection to a world position (the minimap).
    void order_to_tile(double tx, double ty, bool queue);
    /// Escape or the right button (original mouse): out of a pick, back a page, deselect.
    void cancel_action();
    /// The orders as submit_orders() gives them, `ask` first if they cannot be taken back.
    void submit(std::vector<sim::Command> orders, bool sound, bool ask);

    [[nodiscard]] const sim::World& world() const noexcept { return m_state->world; }
    [[nodiscard]] const sim::Simulation& simulation() const noexcept { return m_state->simulation; }
    [[nodiscard]] sim::PlayerId local() const noexcept { return m_local_player; }

    // The viewer's parts (Parts).
    Host& m_host;
    const assets::BuildingTable& m_buildings;
    const assets::FigureTable& m_figures;
    const std::unique_ptr<GameState>& m_state;
    const sim::PlayerId& m_local_player;
    render::Camera& m_camera;
    BuildTool& m_tool;
    CommandSink& m_commands;
    GameSound& m_sound;
    platform::Window& m_window;
    Hud* m_hud;
    bool m_original_mouse;
    bool m_confirm;
    std::filesystem::path m_keys_file;

    ui::KeyBindings m_keys;
    ui::UnitSelection m_units;  ///< the selected ships or land units
    CheatConsole m_cheats;
    Pick m_pick = Pick::None;
    LeftPress m_press;  ///< the left button pressed on the map
    MapDrag m_drag;     ///< the map dragged with the right or middle button
    GroupKey m_group_key;
    std::optional<std::size_t> m_last_city;            ///< the city the key N went to last (World::cities())
    std::size_t m_formation = 0;                       ///< the next formation of the F key
    std::vector<sim::Command> m_pending_orders;        ///< waiting for the answer to a question
    mutable std::vector<render::TileHit> m_tile_hits;  ///< working memory of picks
    double m_mouse_x = 0.0;                            ///< in output pixels
    double m_mouse_y = 0.0;
    bool m_left_down = false;   ///< the left mouse button, for the frame's buttons
    float m_wheel_rest = 0.0F;  ///< wheel notches begun (touchpads) the goods window did not scroll yet
    bool m_text_input = false;  ///< the platform's text input is on (a name is typed)
};

}  // namespace o1602::app
