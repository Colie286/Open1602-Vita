// SPDX-License-Identifier: GPL-3.0-or-later
//
// The side panel of the editor (ADR 0013): below the head with the minimap, compass, rotation and
// zoom, where the game shows its modes. Its pages: what the document is (file, size, islands,
// players) with the buttons for the files and undo; a new map; the files to open; the name to
// save under; what is selected on the map; what the map shows over it; the check list; new islands. The panel only
// edits what is typed and tells what was clicked; the viewer keeps the document and fills the model every frame.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/ui/form.hpp"

namespace o1602::app {

/// The settings of a new map.
struct EditorNewMap {
    /// The sizes offered, in tiles: the original's first.
    static constexpr std::array<std::array<std::uint32_t, 2>, 5> kSizes = {
        {{500, 350}, {700, 500}, {1000, 1000}, {2000, 2000}, {4000, 4000}}};
    /// How many islands: none (only sea), few, as many as the original, many.
    enum class Islands : std::uint8_t { None, Few, Normal, Many };

    std::size_t size = 0;  ///< index into kSizes
    Islands islands = Islands::Normal;
    std::uint8_t players = 4;  ///< with a start (islands only), 1 .. sim::kMaxPlayers
    std::uint64_t seed = 0;    ///< the world the generator makes
    /// The kind of islands (EditorModel::landscapes): 0 the original's templates, then islands of
    /// their own shape after Open1602's presets and the packages'.
    std::size_t landscape = 0;

    [[nodiscard]] std::uint32_t width() const noexcept { return kSizes[size % kSizes.size()][0]; }
    [[nodiscard]] std::uint32_t height() const noexcept { return kSizes[size % kSizes.size()][1]; }
    friend bool operator==(const EditorNewMap&, const EditorNewMap&) = default;
};

/// The settings of a new island (the page "Islands").
struct EditorNewIsland {
    bool own_shape = false;  ///< of its own shape, else one of the original's templates
    /// A placeholder of the world plan instead (only a world that has a plan): the island is made
    /// when a game starts.
    bool placeholder = false;
    /// 0 north, 1 south, from 2 a climate of a package (EditorModel::climates[climate - 2]); a
    /// placeholder only north or south.
    std::uint8_t climate = 0;
    std::uint8_t size = 2;   ///< size class 0 .. 4
    std::uint64_t seed = 0;  ///< which template, or the shape
    friend bool operator==(const EditorNewIsland&, const EditorNewIsland&) = default;
};

/// The sides of the terrain brushes, in tiles (editor::kBrushSizes).
inline constexpr std::array<std::uint8_t, 8> kTerrainBrushes = {1, 2, 3, 5, 7, 9, 13, 17};

/// The settings of the terrain tools (the page "Terrain").
struct EditorTerrain {
    /// Brush: the piece where the mouse goes; Land, Water: land or sea with the coast laid
    /// about it; Random: pieces of the group drawn tile by tile; Fill: the tiles joined to the one
    /// clicked that show the same; Replace: every tile of the island that shows the same; Pipette:
    /// takes the piece of the tile clicked; Coast: the coast under the brush laid anew; Select: a
    /// rectangle of an island dragged out, to copy; Paste: the copy laid where clicked.
    enum class Tool : std::uint8_t { Brush, Land, Water, Coast, Random, Fill, Replace, Pipette, Select, Paste };
    static constexpr std::size_t kTools = 10;
    Tool tool = Tool::Brush;
    std::uint8_t group = 0;     ///< editor::TerrainGroup
    std::size_t piece = 0;      ///< index into the group's pieces
    std::uint8_t rotation = 0;  ///< 0..3, a quarter clockwise each
    std::uint8_t brush = 0;     ///< index into editor::kBrushSizes
    std::uint8_t shape = 1;     ///< editor::BrushShape of the shaping tools (Land, Water, Coast, Random)
    friend bool operator==(const EditorTerrain&, const EditorTerrain&) = default;
};

/// The settings of the objects' tools (the page "Objects").
struct EditorObjects {
    /// Build: the building where clicked, for the owner, without costs (dragged, again where it no
    /// longer covers the last); Template: one of the player's saved templates (templates.hpp) about
    /// the mouse, for the owner, without costs; Ship, Unit: a ship on the water, a land unit on an island, for the
    /// owner; Resource: an ore or gold vein in a mountain, a treasure, deer in a forest; Remove:
    /// takes the ship, unit, resource or building clicked away.
    enum class Tool : std::uint8_t { Build, Template, Ship, Unit, Resource, Remove };
    static constexpr std::size_t kTools = 6;
    Tool tool = Tool::Build;
    std::size_t group = 0;      ///< index into the build menu's categories that list buildings
    std::size_t piece = 0;      ///< index into the group's buildings
    std::size_t stamp = 0;      ///< index into the templates (EditorModel::object_pieces while Template)
    std::size_t ship = 0;       ///< index into the ships (EditorModel::object_pieces while Ship)
    std::size_t unit = 0;       ///< index into the land units (EditorModel::object_pieces while Unit)
    std::size_t resource = 0;   ///< editor::Resource: veins, treasure, deer
    std::uint8_t rotation = 0;  ///< 0..3, a quarter clockwise each; ships and units face 2 * rotation
    std::size_t owner = 0;      ///< index into the world's players
    friend bool operator==(const EditorObjects&, const EditorObjects&) = default;
};

/// An object placed lately (the page "Objects" offers it again): the settings it was placed with
/// and its name (Windows-1252).
struct EditorRecent {
    EditorObjects objects;
    std::string name;
};
/// How many objects placed lately the page "Objects" offers.
inline constexpr std::size_t kEditorRecent = 6;

/// The tools of the editor's tool bar (EditorChrome), each with a key of its own: what a click on
/// the map does.
enum class EditorTool : std::uint8_t {
    Select,       ///< picks what is clicked (the page "Selection")
    Island,       ///< a new island of a template or of its own shape (the page "Islands")
    Placeholder,  ///< a new placeholder of the world plan (only a world that has a plan)
    Land,
    Water,
    Coast,
    Brush,
    Random,
    Fill,
    Replace,
    Pipette,
    Area,  ///< EditorTerrain::Tool::Select: a rectangle of an island to copy
    Paste,
    Build,
    Template,
    Ship,
    Unit,
    Resource,
    Remove,
    Marker,  ///< a named point or area for the scenario's scripts (the page "Markers")
};
inline constexpr std::size_t kEditorTools = 20;

/// What the editor's menu bar asks for (EditorChrome), the same as the keys do.
enum class EditorCommand : std::uint8_t {
    New,
    Open,
    Save,
    SaveAs,
    Close,
    Undo,
    Redo,
    View,
    Islands,
    WorldSize,
    Random,
    Preview,
    Players,
    Scenario,
    Rules,
    Goals,
    Check,
    TestPlay,
    ExportImage,
    Find,
    Script,
    Keys,
};

/// What a click on the editor's map picked.
struct EditorPick {
    enum class Kind : std::uint8_t { None, Sea, Island, Building, Ship, Soldier, Placeholder };
    Kind kind = Kind::None;
    /// Island, Building: index into World::islands(); Placeholder: into the world plan's islands.
    std::uint16_t island = 0;
    /// Sea, Island: the world tile clicked; Building: its origin on the island.
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t id = 0;  ///< Ship, Soldier: its id
    friend bool operator==(const EditorPick&, const EditorPick&) = default;
};

/// A marker of the scenario as the page "Markers" lists it (Windows-1252).
struct EditorMarkerRow {
    std::string name;
    std::string place;  ///< where it lies: "point 41, 32", "area 40, 30, 8 x 6"
};

/// A file the editor can open, as the list shows it.
struct EditorFile {
    std::string name;  ///< Windows-1252
    /// The heading of the group the file belongs to (Windows-1252): own scenarios, the original's.
    std::string group;
};

/// A property of what is selected on the map, as the panel shows it (Windows-1252).
struct EditorProperty {
    std::string label;
    std::string value;
};

/// A line of the check list (Windows-1252).
struct EditorCheckLine {
    enum class Kind : std::uint8_t {
        Heading,  ///< a group: what the world needs, what the original cannot have
        Error,    ///< keeps the scenario from being saved
        Warning,  ///< beyond the original's limits while the scenario is to keep to them
        Note,     ///< beyond the original's limits, which the scenario need not keep to
        Fine,     ///< nothing found in the group
    };
    /// Where a click on the line leads: the island `index` on the map, or the dialog that sets
    /// what the line is about (Goals: on the mission `index`).
    enum class Target : std::uint8_t { None, Island, Goals, Scenario, Players, WorldSize };
    std::string text;
    Kind kind = Kind::Note;
    Target target = Target::None;
    std::size_t index = 0;
};

/// What the map shows over the world in the editor (the page "View").
enum class EditorOverlay : std::uint8_t { Owners, IslandNumbers, Coordinates };
inline constexpr std::size_t kEditorOverlays = 3;

/// A good in a city's warehouses (the page "Stock").
struct EditorGood {
    std::uint8_t ware = 0;
    std::string name;          ///< Windows-1252
    std::uint32_t amount = 0;  ///< 1/32 t
};

/// The city of the building selected on the map.
struct EditorCity {
    std::string name;   ///< Windows-1252
    std::string owner;  ///< the owner's name, Windows-1252
    /// Room per ware in 1/32 t (sim::City::capacity); 0: no Kontor, no stock.
    std::uint32_t room = 0;
    std::vector<EditorGood> goods;  ///< the world's trade goods in the order panels list them
};

/// A human's or computer's slot as the page "Players" shows it.
struct EditorPlayer {
    std::uint8_t slot = 0;  ///< sim::PlayerId
    std::string name;       ///< as the game shows it, Windows-1252
    std::string own_name;   ///< its own, empty: the name of its colour
    bool human = false;
    std::uint16_t banner = 0;
    std::int32_t money = 0;
    std::uint16_t monuments_allowed = 0;
    std::uint16_t arches_allowed = 0;
    std::string colour;  ///< the banner's colour, Windows-1252
    /// How it stands at the start with each slot of EditorModel::player_slots, in that order
    /// (its own entry unused).
    struct Standing {
        bool peace = true;
        bool trade = false;
    };
    std::vector<Standing> relations;
};

/// A game setting as the page "Rules" shows it.
struct EditorRule {
    std::string label;    ///< Windows-1252
    std::string value;    ///< Windows-1252
    bool enabled = true;  ///< false: it does not matter with the others as they are
};

/// The rows of the page "Scenario": the name and the briefing in German and English, the stars,
/// the author, the version and whether the scenario keeps to the original's limits.
enum class EditorScenarioRow : std::uint8_t {
    NameDe,
    NameEn,
    BriefingDe,
    BriefingEn,
    Stars,
    Author,
    Version,
    OriginalLimits,
    Count
};
inline constexpr std::size_t kEditorScenarioRows = static_cast<std::size_t>(EditorScenarioRow::Count);
/// Whether a row of the page "Scenario" is a text to type.
[[nodiscard]] constexpr bool scenario_row_is_text(EditorScenarioRow row) noexcept {
    return row != EditorScenarioRow::Stars && row != EditorScenarioRow::OriginalLimits;
}
/// The longest text a row takes, in characters.
[[nodiscard]] constexpr std::size_t scenario_row_length(EditorScenarioRow row) noexcept {
    return row == EditorScenarioRow::BriefingDe || row == EditorScenarioRow::BriefingEn ? 2000
           : row == EditorScenarioRow::Version                                          ? 20
                                                                                        : 80;
}

/// A mission of the scenario as the page "Goals" shows it: its text, its goals and loss conditions
/// (label and value, by editor::GoalField; `enabled` false where it does not matter as the others
/// are), and which of EditorModel::player_slots hold it.
struct EditorMission {
    std::string text;  ///< Windows-1252
    std::vector<EditorRule> goals;
    std::vector<bool> holders;
};

/// An entry of the world plan as the page "Random elements" shows it: x at random, y on the map.
struct EditorRandom {
    std::string label;  ///< Windows-1252
    std::uint32_t random = 0;
    std::uint32_t fixed = 0;
};

/// What the panel shows; the viewer fills it every frame.
struct EditorModel {
    std::string file;  ///< the document's name (Windows-1252), empty: a new map not saved yet
    bool modified = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t islands = 0;
    /// Islands the world plan makes when a game starts (ADR 0013), 0 for a world without a plan.
    std::size_t planned_islands = 0;
    std::size_t players = 0;  ///< humans and computers
    /// The scenario's markers as the page "Markers" lists them, and the one selected.
    std::vector<EditorMarkerRow> markers;
    std::optional<std::size_t> marker;
    /// The scenario's script (the dialog "Script"): its size, the file it was loaded from (its
    /// name, Windows-1252), whether that file can be read again, and what is wrong with the text
    /// (the first syntax error with its line, Windows-1252), empty if nothing.
    std::size_t script_bytes = 0;
    std::size_t script_lines = 0;
    std::string script_file;
    bool script_reload = false;
    std::string script_problem;
    /// The document is a savegame opened as such: Save writes it back, marked as changed.
    bool savegame = false;
    bool can_undo = false;
    bool can_redo = false;
    /// The last thing that happened (saved, an error), Windows-1252.
    std::string notice;
    bool notice_is_error = false;
    /// The files to open (the page "Open"), in the order shown.
    std::vector<EditorFile> files;
    /// The names (Windows-1252, without ".o1602") of the own scenarios: a name typed that is
    /// among them replaces that file.
    std::vector<std::string> own_names;
    /// What is selected on the map (the page "Selection"): its name and its properties.
    std::string selection;
    std::vector<EditorProperty> properties;
    /// The overlays shown, by EditorOverlay.
    std::array<bool, kEditorOverlays> overlays{};
    /// The check list (the page "Check"), as made when the page opened.
    std::vector<EditorCheckLine> check;
    /// The island selected on the map (the page "Selection"), and whether it may be turned,
    /// copied and taken out: no city, buildings or units of a game on it.
    std::optional<std::size_t> island;
    bool island_free = false;
    /// The selected island's fertility (assets::IslandHeader::fertility) and climate.
    std::uint32_t island_fertility = 0;
    std::uint8_t island_climate = 0;
    /// The crops the selected island's climate grows half without its fertility for them, as
    /// fertility bits (sim::climate_crops).
    std::uint32_t island_crops = 0;
    /// The kinds of islands a new map may have (EditorNewMap::landscape), Windows-1252: the
    /// original's templates first.
    std::vector<std::string> landscapes;
    /// The names of the packages' climates the world knows (Windows-1252), offered for new islands
    /// after north and south.
    std::vector<std::string> climates;
    /// Which template a new island of a template takes (the page "Islands"), Windows-1252: its
    /// number among the original's, or a package's island by name.
    std::string island_template;
    /// An island is being placed (a new one, one moved or copied): the map's next click puts it down.
    bool placing = false;
    /// The smallest map that holds the islands, ships, land units and markers (the page "Map
    /// size"): the map cannot be cut below.
    /// The world has a plan of islands made when a game starts: placeholders can be set.
    bool has_plan = false;
    /// The placeholder selected on the map (index into the plan's islands), its climate (0 north,
    /// 1 south, 2 by its place) and size class.
    std::optional<std::size_t> placeholder;
    std::uint8_t placeholder_climate = 0;
    std::uint8_t placeholder_size = 0;
    std::uint32_t used_width = 1;
    std::uint32_t used_height = 1;
    /// The terrain tools' settings, the pieces of the group, and the piece chosen: its id, its
    /// size in tiles and whether it turns.
    EditorTerrain terrain;
    std::size_t terrain_pieces = 0;
    std::uint32_t terrain_id = 0;
    std::uint32_t terrain_width = 1;
    std::uint32_t terrain_height = 1;
    bool terrain_turns = false;
    /// The terrain selected (Select) and the size of the copy (0 x 0: nothing copied).
    bool terrain_selected = false;
    std::uint32_t clip_width = 0;
    std::uint32_t clip_height = 0;
    /// The objects' tools: their settings, the groups and the buildings of the group (or the ships,
    /// the land units), the one chosen (its name, Windows-1252, and whether it turns) and the
    /// owner's name.
    EditorObjects objects;
    std::size_t object_groups = 0;
    std::size_t object_owners = 0;  ///< the players objects are placed for: humans, computers, natives, pirates
    std::size_t object_pieces = 0;
    std::string object_group;
    std::string object_name;
    bool object_turns = false;
    std::string object_owner;
    /// The objects placed lately, the last first (at most kEditorRecent).
    std::vector<EditorRecent> recent_objects;
    /// Why the tool in hand would not place where the mouse is on the map (Windows-1252), empty:
    /// it would, or the mouse is off the map.
    std::string object_hover;
    /// The city of the building selected (the page "Selection"), its stock and owner.
    std::optional<EditorCity> city;
    /// The humans' and computers' slots (the page "Players"), in the order of the slots; whether
    /// another can be added and the last taken away.
    std::vector<EditorPlayer> player_slots;
    bool can_add_player = false;
    bool can_remove_player = false;
    /// The page "Rules": whether the scenario has settings of its own (else the player's apply),
    /// whether they are the original's, and each setting a scenario can have, in the start
    /// dialog's order.
    bool rules_own = false;
    bool rules_original = false;
    std::vector<EditorRule> rules;
    /// The page "Random elements": the world plan's entries (only a world that has a plan).
    std::vector<EditorRandom> randoms;
    /// The page "Goals": the scenario's missions, and how many it can have at most.
    std::vector<EditorMission> missions;
    std::size_t max_missions = 0;
    /// The page "Scenario": its description by EditorScenarioRow, as shown (label and value) and
    /// as typed (the texts, Windows-1252; empty for the rows that are no text).
    std::vector<EditorRule> scenario_rows;
    std::vector<std::string> scenario_texts;
    /// The page "Find": the islands, cities and ships whose names hold the text typed (Windows-1252).
    std::vector<std::string> found;
    /// The dice preview: the map shows the islands of the plan as one start of a game makes them
    /// (roll `preview_roll` of this preview, `preview_islands` islands); the plan waits meanwhile.
    bool previewing = false;
    std::uint32_t preview_roll = 0;
    std::size_t preview_islands = 0;
};

/// What a click or a key asked for.
struct EditorRequest {
    enum class Kind : std::uint8_t {
        New,     ///< a new map with `map`
        Open,    ///< the file `index` of the model's list
        Save,    ///< under the document's file, or ask for a name (SaveAs) if it has none
        SaveAs,  ///< under `name` in the folder of own scenarios
        Undo,
        Redo,
        Close,  ///< out of the editor, back to the main menu
        /// The page "Open" opens: the model's list of files is due.
        ListFiles,
        Overlay,       ///< the overlay `index` (EditorOverlay) on or off
        Check,         ///< the page "Check" opens: the check list is due
        PlaceIsland,   ///< a new island with `island` to place on the map
        MoveIsland,    ///< the island `index` to place anew
        RotateIsland,  ///< the island `index` a quarter clockwise
        CopyIsland,    ///< a copy of the island `index` to place
        RemoveIsland,  ///< the island `index` taken out
        StopPlacing,   ///< what is being placed goes back
        ResizeWorld,   ///< the map made `width` x `height` tiles large
        SetFertility,  ///< the island `index` fertile for the crops of `fertility`
        SaveIsland,    ///< the island `index` saved alone, in the folder of own islands
        /// The page "Load island" opens: the model's list of files is due, the island files.
        ListIslandFiles,
        LoadIsland,         ///< the island of file `index` of the model's list to place
        MovePlaceholder,    ///< the placeholder `index` to place anew
        RemovePlaceholder,  ///< the placeholder `index` taken out
        ChangePlaceholder,  ///< the placeholder `index` of climate `island.climate`, size `island.size`
        Terrain,            ///< the terrain tools set to `terrain`
        TerrainCopy,        ///< the terrain selected copied
        TerrainTurn,        ///< the copy turned a quarter clockwise
        TerrainMirror,      ///< the copy mirrored left to right
        Objects,            ///< the buildings' tools set to `objects`
        RenameCity,         ///< the selected building's city named `name`
        CityGoods,          ///< the city's stock of ware `index` set to `amount` (1/32 t)
        CityOwner,          ///< the city to the next owner (`index` 1) or the one before (0)
        /// The slot `player.slot` with the values of `player`; `index` 1 or 2: its colour the next
        /// or the one before instead; 3: the slot the page shows (EditorPanel::chosen_player) named
        /// `name`, the rest as it is.
        SetPlayer,
        AddPlayer,     ///< a computer player in the next slot
        RemovePlayer,  ///< the last slot taken away
        /// `player.slot` and slot `index` standing at the start as `player.relations[0]`.
        SetRelations,
        /// The scenario's settings: `index` 0 none (the player's), 1 the original's, 2 Open1602's,
        /// 3 those the player last started a scenario with.
        RulesPreset,
        /// The setting EditorModel::rules[`index`] to its next value (`amount` 1) or the one before (0).
        RuleStep,
        /// The entry EditorModel::randoms[`index`] one more at random (`amount` 1) or one fewer (0).
        RandomStep,
        /// The plan's islands made as a game's start makes them, shown instead of the plan (again:
        /// another roll); nothing of it is kept.
        RandomRoll,
        RandomPreviewEnd,  ///< the plan back from the dice preview
        ShowKeys,          ///< the overview of the keys (F1)
        MissionAdd,        ///< a new mission without goals
        MissionRemove,     ///< the mission `index` taken away
        /// The slot EditorModel::player_slots[`amount`] holding the mission `index`, or none if it
        /// holds it already.
        MissionHolder,
        /// The goal `goal` (editor::GoalField) of the mission `index` to its next value (`amount`
        /// 1) or the one before (0).
        GoalStep,
        MissionText,  ///< the mission `index` with the text `name`
        /// The row `index` (EditorScenarioRow) of the description to its next value (`amount` 1)
        /// or the one before (0): the stars, keeping to the original's limits.
        ScenarioStep,
        ScenarioText,  ///< the row `index` (EditorScenarioRow) of the description as `name`
        TestPlay,      ///< a game of the map as it is now; F5 in it leads back
        Recover,       ///< the map kept by the editor's recovery, opened instead of the new one
        ShowIsland,    ///< the island `index` in the view's middle, selected
        ExportImage,   ///< the map as a picture (PNG) in the screenshots' folder
        FindGo,        ///< the view to EditorModel::found[`index`], selected
        MarkerSelect,  ///< the marker `index` selected and in the view's middle
        MarkerRename,  ///< the marker `index` named `name`
        MarkerRemove,  ///< the marker `index` taken away
        /// The page "Load script" opens: the model's list of files is due, the scripts.
        ListScripts,
        ScriptLoad,    ///< the script of file `index` of the model's list as the scenario's
        ScriptReload,  ///< the script read again from the file it was loaded from
        ScriptRemove,  ///< the scenario without a script
    };
    Kind kind = Kind::Close;
    std::size_t index = 0;
    std::string name{};  ///< Windows-1252
    EditorNewMap map{};
    EditorNewIsland island{};
    std::uint32_t width = 0;      ///< ResizeWorld
    std::uint32_t height = 0;     ///< ResizeWorld
    std::uint32_t fertility = 0;  ///< SetFertility
    EditorTerrain terrain{};      ///< Terrain
    EditorObjects objects{};      ///< Objects
    EditorPlayer player{};        ///< SetPlayer
    std::uint32_t amount = 0;     ///< CityGoods
    std::size_t goal = 0;         ///< GoalStep
};

class EditorPanel {
public:
    /// The panel's width in design units: the editor's column right of the map (EditorChrome).
    static constexpr std::int32_t kWidth = 280;
    /// The room left and right of the pages in the column.
    static constexpr std::int32_t kMargin = 12;
    /// Rows of the list of files on one page.
    static constexpr std::size_t kRows = 12;
    /// The longest name a scenario can be saved under.
    static constexpr std::size_t kMaxName = 60;

    enum class Page : std::uint8_t {
        Document,
        New,
        Open,
        SaveAs,
        Selection,
        View,
        Check,
        Islands,
        WorldSize,
        Fertility,
        IslandFiles,
        Terrain,
        Objects,
        CityName,
        CityGoods,
        Players,
        PlayerName,
        Relations,
        Rules,
        Random,
        Goals,
        GoalText,
        Scenario,
        ScenarioText,
        Find,
        Markers,
        MarkerName,
        Script,
        ScriptFiles
    };

    explicit EditorPanel(ui::FormStyle style = {}) : m_form(style) {}

    void set_style(const ui::FormStyle& style) { m_form.set_style(style); }
    void set_scale(double scale) { m_form.set_scale(scale); }
    [[nodiscard]] double scale() const noexcept { return m_form.scale(); }
    /// The panel's room in the side panel, design units from its top left: from `top` down,
    /// `height` high.
    void set_room(std::int32_t top, std::int32_t height) noexcept {
        m_top = top;
        m_height = height;
    }

    /// The window, design units, and where the column begins in it: the pages of the whole map
    /// (players, rules, random elements, map size, check) open as dialogs in its middle. Without
    /// it (width 0) they stay in the column.
    void set_screen(std::int32_t width, std::int32_t height, std::int32_t column_x) noexcept {
        m_screen_w = width;
        m_screen_h = height;
        m_column_x = column_x;
    }
    /// Whether the page is a dialog over the map (it takes every click, Escape closes it).
    [[nodiscard]] bool dialog() const noexcept;
    /// The dialog's frame, design units from the column's top left (left of it: negative).
    [[nodiscard]] ui::Rect dialog_rect() const noexcept;

    [[nodiscard]] Page page() const noexcept { return m_page; }
    /// Back to the document's page (a new document, or Escape).
    void show_document() noexcept;
    /// The page "Save as" with `name` offered.
    void ask_name(std::string_view name);
    /// The page of what was selected on the map (a click on the map); false on a page that a
    /// click on the map must not leave (a new map, the files, a name being typed).
    bool show_selection() noexcept;
    /// The page "New map" with `map` offered.
    void ask_new(const EditorNewMap& map);
    /// A name is being typed: the keys go here.
    [[nodiscard]] bool typing() const noexcept {
        return m_page == Page::SaveAs || m_page == Page::CityName || m_page == Page::PlayerName ||
               m_page == Page::GoalText || m_page == Page::ScenarioText || m_page == Page::Find ||
               m_page == Page::MarkerName;
    }
    /// The text typed on the page "Find" (Windows-1252); empty on another page.
    [[nodiscard]] const std::string& find_text() const noexcept {
        static const std::string kNone;
        return m_page == Page::Find ? m_text : kNone;
    }
    /// The mission the page "Goals" shows (index into EditorModel::missions).
    [[nodiscard]] std::size_t chosen_mission() const noexcept { return m_mission; }
    /// The slot the page "Players" shows (index into EditorModel::player_slots).
    [[nodiscard]] std::size_t chosen_player() const noexcept { return m_player; }
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }
    [[nodiscard]] const EditorNewMap& new_map() const noexcept { return m_new; }
    [[nodiscard]] const EditorNewIsland& new_island() const noexcept { return m_island; }

    /// Everything to draw, relative to the side panel's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const EditorModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The mouse in pixels relative to the side panel's top left; the right button goes back
    /// through the values of the page "New map".
    std::optional<EditorRequest> mouse(std::int32_t x, std::int32_t y, bool down, const EditorModel& model,
                                       ui::MouseButton button = ui::MouseButton::Left);
    /// Text typed (Windows-1252).
    void type(std::string_view text);
    /// Ctrl+A 1, Backspace 8, Enter 13 saves, Escape 27 goes back to the document's page.
    std::optional<EditorRequest> key(int key);
    /// Escape outside a field: a page back to the document's; false if it showed already.
    bool back() noexcept;
    /// What the menu bar or a key asked for, as the buttons of the pages do it.
    std::optional<EditorRequest> command(EditorCommand command, const EditorModel& model);
    /// The tool chosen in the tool bar or by its key: its page, and its settings for the viewer.
    std::optional<EditorRequest> choose_tool(EditorTool tool, const EditorModel& model);
    /// The tool the page and the model's settings stand for, if the page is a tool's.
    [[nodiscard]] std::optional<EditorTool> active_tool(const EditorModel& model) const noexcept;

    /// The widgets as laid out last (tests).
    [[nodiscard]] const ui::Form& form() const noexcept { return m_form; }
    /// Lays the page out for `model` (draw() and mouse() do it themselves).
    void layout(const EditorModel& model);

    /// A name as a file may carry it: what the file systems refuse goes, spaces at the ends too.
    [[nodiscard]] static std::string clean_name(std::string_view name);
    /// Whether `name` is one of the own scenarios' (letters in any case).
    [[nodiscard]] static bool name_taken(const EditorModel& model, std::string_view name);

private:
    /// The page's widgets within m_x, m_w, m_top, m_height.
    void layout_page(const EditorModel& model);
    void layout_document(const EditorModel& model);
    void layout_new(const EditorModel& model);
    void layout_open(const EditorModel& model);
    void layout_save(const EditorModel& model);
    void layout_selection(const EditorModel& model);
    void layout_view(const EditorModel& model);
    void layout_check(const EditorModel& model);
    void layout_islands(const EditorModel& model);
    void layout_world_size(const EditorModel& model);
    void layout_fertility(const EditorModel& model);
    void layout_terrain(const EditorModel& model);
    void layout_objects(const EditorModel& model);
    void layout_city_name(const EditorModel& model);
    void layout_city_goods(const EditorModel& model);
    void layout_players(const EditorModel& model);
    void layout_player_name(const EditorModel& model);
    void layout_relations(const EditorModel& model);
    void layout_rules(const EditorModel& model);
    void layout_random(const EditorModel& model);
    void layout_preview(const EditorModel& model);
    /// The dialogs' wide forms: every player in a row, the diplomacy as a table, the settings in
    /// two columns.
    void layout_players_table(const EditorModel& model);
    void layout_relations_table(const EditorModel& model);
    void layout_settings_grid(const EditorModel& model, bool rules);
    void layout_goals(const EditorModel& model);
    void layout_goal_text(const EditorModel& model);
    void layout_scenario(const EditorModel& model);
    void layout_find(const EditorModel& model);
    void layout_markers(const EditorModel& model);
    void layout_marker_name(const EditorModel& model);
    void layout_script(const EditorModel& model);
    /// The height of a line of the check list in a dialog `width` design units wide.
    [[nodiscard]] static std::int32_t check_line_h(const EditorCheckLine& line, std::int32_t width) noexcept;
    /// The notice (or the note `note_key`, `short_key` where it does not fit whole) from `y`
    /// down and the button `back` labelled `back_key` at the panel's foot.
    void note_and_back(const EditorModel& model, std::int32_t y, const char* note_key, const char* short_key,
                       ui::Form::Id back, const char* back_key);
    void layout_placeholder_actions(const EditorModel& model, std::int32_t& y);
    /// The buttons of an island selected: move, turn, copy, take out; or a note while placing.
    void layout_island_actions(const EditorModel& model, std::int32_t& y);
    std::optional<EditorRequest> commit_name();
    /// What the widget `id` does when clicked (`forward`: with the left button).
    std::optional<EditorRequest> press(ui::Form::Id id, const EditorModel& model, bool forward);

    ui::Form m_form;
    Page m_page = Page::Document;
    /// Where the page is laid out, design units from the column's top left: the column below
    /// the minimap, or a dialog's inside.
    std::int32_t m_x = kMargin;
    std::int32_t m_w = kWidth - 2 * kMargin;
    std::int32_t m_top = 0;
    std::int32_t m_height = 400;
    std::string m_text;        ///< the name being typed
    std::size_t m_marker = 0;  ///< the marker whose name is typed (the page MarkerName)
    EditorNewMap m_new;
    EditorNewIsland m_island;
    /// The size the page "Map size" offers.
    std::uint32_t m_size_width = 0;
    std::uint32_t m_size_height = 0;
    std::size_t m_first = 0;         ///< the first file shown on the page "Open", the first line on "Check"
    std::size_t m_page_rows = 1;     ///< lines of the check list on its page as laid out last
    std::size_t m_player = 0;        ///< the slot the page "Players" shows
    std::size_t m_mission = 0;       ///< the mission the page "Goals" shows
    std::size_t m_scenario_row = 0;  ///< the row of the page "Scenario" being typed
    std::size_t m_partner = 0;       ///< the other slot the page "Relations" shows
    std::size_t m_first_column = 0;  ///< the first slot of the diplomacy table's columns
    std::int32_t m_screen_w = 0;     ///< the window, design units (0: no dialogs)
    std::int32_t m_screen_h = 0;
    std::int32_t m_column_x = 0;             ///< where the column begins in the window
    Page m_laid_page = Page::Document;       ///< the page laid out last
    std::vector<std::size_t> m_page_starts;  ///< where the pages before began (the check list, the stock)
    std::int32_t m_check_h = 580;            ///< the height the check list's dialog wants
    std::int32_t m_files_h = 0;              ///< the height the dialog of the scripts to load needs
    bool m_previewing = false;               ///< the dice preview, as laid out last (its page is the column's)
};

}  // namespace o1602::app
