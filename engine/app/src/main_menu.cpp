// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_menu.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <map>
#include <random>
#include <utility>

#include "banner_graphic.hpp"
#include "high_score_file.hpp"
#include "multiplayer_menu.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/ui/list_scroll.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

// BASE.GAD: the buttons from the top (IDHAUPT + 2 ...). The History Edition has a sixth one,
// "Options" (its own graphics settings) after Multiplayer, so the ones below it count one further.
// Open1602 shows its window "Anzeige" for it (display_menu.hpp).
constexpr std::int32_t kSingle = 30002;
constexpr std::int32_t kMulti = 30003;
constexpr std::int32_t kCredits = 30004;
constexpr std::int32_t kIntro = 30005;
constexpr std::int32_t kQuit = 30006;
constexpr std::int32_t kHistoryOptions = 30004;
constexpr std::int32_t kHistoryQuit = 30007;
// Open1602's own button on BASE.GAD's screen, below the original's.
constexpr std::int32_t kModsButton = 1;
constexpr ui::Rect kModsButtonRect{24, 714, 160, 38};
constexpr std::int32_t kEditorButton = 2;
constexpr ui::Rect kEditorButtonRect{192, 714, 160, 38};
// HOST.GAD
constexpr std::int32_t kNewGame = 31002;
constexpr std::int32_t kLoadGame = 31003;
constexpr std::int32_t kContinue = 31004;
constexpr std::int32_t kMainMenu = 31005;
constexpr std::int32_t kListUp = 31011;
constexpr std::int32_t kListDown = 31012;
constexpr std::int32_t kRowText = 31021;   ///< + row
constexpr std::int32_t kRowStars = 31041;  ///< + row
constexpr std::int32_t kRowPlug = 31061;   ///< + row
constexpr std::size_t kRows = 14;
// MISSION.GAD
constexpr std::int32_t kStart = 32001;
constexpr std::int32_t kCancel = 32002;
constexpr std::int32_t kScoresHeading = 32004;
constexpr std::int32_t kScoresFrame = 32005;
constexpr std::int32_t kScoresUp = 32006;
constexpr std::int32_t kScoresDown = 32007;
constexpr std::int32_t kChoice = 32011;       ///< + 0..4
constexpr std::int32_t kChoiceLabel = 32021;  ///< + 0..4
constexpr std::int32_t kBriefing = 32031;
constexpr std::int32_t kBriefing2 = 32041;
constexpr std::int32_t kScoreName = 32051;  ///< + 0..3
constexpr std::int32_t kScorePoints = 32061;
constexpr std::int32_t kScoreRank = 32071;
constexpr std::int32_t kScoreStars = 32081;
constexpr std::int32_t kBriefingUp = 32091;
constexpr std::int32_t kBriefingDown = 32092;
[[maybe_unused]] constexpr std::int32_t kBriefingTrack = 32093;
constexpr std::int32_t kBriefingSlider = 32094;
constexpr std::size_t kChoices = 5;
constexpr std::size_t kScoreRows = 4;
// FARBWAHL.GAD
constexpr std::int32_t kNameBox = 35002;
constexpr std::int32_t kNamePrompt = 35003;
constexpr std::int32_t kFlag = 35011;    ///< + colour 0..3: the banners in the hall
constexpr std::int32_t kBanner = 35021;  ///< + colour 0..3: their outlines, which take the click
// Open1602's own: the further colours as coats of arms beside the name board.
constexpr std::int32_t kMoreArmsLabel = 35041;  ///< + side 0..1: the colour under the mouse
constexpr std::int32_t kMoreArms = 35051;       ///< + 0..kMoreBanners - 1, banner kFirstExtraBanner + n
constexpr std::size_t kMoreBanners = sim::kMaxPlayers - sim::kOriginalPlayers;
/// Half of the further colours on either side, the left one taking the odd one.
constexpr std::size_t kArmsPerSide = (kMoreBanners + 1) / 2;
/// Coats of arms side by side; more players than twelve (sim::kMaxPlayers) fill further rows.
constexpr std::size_t kArmsColumns = 4;
constexpr std::size_t kArmsColumnsUsed = kArmsPerSide < kArmsColumns ? kArmsPerSide : kArmsColumns;
constexpr std::size_t kArmsRows = (kArmsPerSide + kArmsColumns - 1) / kArmsColumns;
/// START.BSH: the end screen's buttons with a coat of arms (ENDE.GAD: GFXWAPPEN + 4, from GFXFARB
/// on 14 + 7 + 4 further; red, blue, yellow, white) and the same with a golden frame, 4 further.
constexpr std::int32_t kGfxArmsFromFarb = 25;
constexpr std::int32_t kGfxArmsGold = 4;
constexpr std::int32_t kGfxFarb = 57;  ///< FARBWAHL.GAD: GFXFARB, if the file does not say
constexpr std::int32_t kArmsWidth = 40;
constexpr std::int32_t kArmsHeight = 52;
constexpr std::int32_t kArmsGap = 6;
constexpr std::int32_t kArmsBoardGap = 12;  ///< between the arms and the name board
constexpr std::int32_t kArmsLabelHeight = 22;

constexpr std::uint32_t kTutorial = 0;
constexpr std::uint32_t kEndless = 1;

/// The first digits of a scenario's files, in order: the choices of the mission screen.
std::vector<std::uint8_t> choice_digits(const MenuScenario& s) {
    std::vector<std::uint8_t> digits;
    for (const auto& [digit, path] : s.files) {
        if (digit != 0xFF && std::find(digits.begin(), digits.end(), digit) == digits.end()) {
            digits.push_back(digit);
        }
    }
    return digits;
}

/// The list's ranking for a difficulty of 1 to 5 stars (the original counts from 0 for one star);
/// 0 stars shows none.
std::uint32_t ranking_of_stars(std::uint8_t stars) {
    return stars == 0 ? assets::ScenarioHeader::kNone : std::uint32_t{stars} - 1U;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::optional<ui::Panel> load_panel(const assets::GameDirectory& game, const char* file) {
    auto layout = assets::load_gad(game, file);
    if (!layout) {
        return std::nullopt;
    }
    return ui::Panel(std::move(layout).value(), 1.0);
}

/// FARBWAHL.GAD with the further colours: four coats of arms on either side of the name board (in
/// further rows centred on the board when there are more than eight), each side with a line below
/// for the name of the colour under the mouse. Unchanged without the name board.
assets::GadgetLayout with_more_arms(assets::GadgetLayout farbwahl) {
    const assets::Gadget* name = farbwahl.find(kNameBox);  // as large as the board it is written on
    if (name == nullptr) {
        return farbwahl;
    }
    const std::int32_t gfx = farbwahl.constant("GFXFARB").value_or(kGfxFarb) + kGfxArmsFromFarb;
    const std::int32_t block = farbwahl.constant("BLK_START").value_or(1);
    constexpr std::int32_t kRowsHeight = static_cast<std::int32_t>(kArmsRows) * (kArmsHeight + kArmsGap) - kArmsGap;
    const std::int32_t top = name->pos[1] + (name->size[1] - kRowsHeight) / 2;
    constexpr std::int32_t kSideWidth =
        static_cast<std::int32_t>(kArmsColumnsUsed) * (kArmsWidth + kArmsGap) - kArmsGap;
    const std::array<std::int32_t, 2> left = {name->pos[0] - kArmsBoardGap - kSideWidth,
                                              name->pos[0] + name->size[0] + kArmsBoardGap};
    const assets::Gadget label = *name;  // the name's font and colour (copied: the gadgets grow below)
    for (std::size_t n = 0; n < kMoreBanners; ++n) {
        const std::size_t side = n / kArmsPerSide;
        assets::Gadget arms;
        arms.id = kMoreArms + static_cast<std::int32_t>(n);
        arms.kind = assets::GadgetKind::Graphic;
        arms.base = static_cast<std::int32_t>(farbwahl.gadgets.size());
        arms.block = block;
        arms.gfx = gfx;
        arms.press_offset = kGfxArmsGold;
        const std::size_t place = n % kArmsPerSide;
        arms.pos = {left[side] + static_cast<std::int32_t>(place % kArmsColumns) * (kArmsWidth + kArmsGap),
                    top + static_cast<std::int32_t>(place / kArmsColumns) * (kArmsHeight + kArmsGap)};
        arms.size = {kArmsWidth, kArmsHeight};
        farbwahl.gadgets.push_back(arms);
    }
    for (std::size_t side = 0; side < 2; ++side) {
        assets::Gadget line = label;
        line.id = kMoreArmsLabel + static_cast<std::int32_t>(side);
        line.kind = assets::GadgetKind::TextCentre;
        line.base = static_cast<std::int32_t>(farbwahl.gadgets.size());
        line.no_select = true;
        line.pos = {left[side], top + kRowsHeight + 4};
        line.size = {kSideWidth, kArmsLabelHeight};
        line.offset = {static_cast<std::int16_t>(kSideWidth / 2), static_cast<std::int16_t>(kArmsLabelHeight - 4)};
        farbwahl.gadgets.push_back(line);
    }
    return farbwahl;
}

std::optional<ui::Panel> load_colour_panel(const assets::GameDirectory& game) {
    auto layout = assets::load_gad(game, "FARBWAHL.GAD");
    if (!layout) {
        return std::nullopt;
    }
    return ui::Panel(with_more_arms(std::move(layout).value()), 1.0);
}

}  // namespace

std::vector<MenuScenario> scenario_list(const std::filesystem::path& folder, const std::array<std::string, 4>& headings,
                                        const assets::GameDirectory* game) {
    // One entry per name without the extension and the digits at its end.
    std::map<std::string, MenuScenario> by_name;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec) || lower(core::path_to_utf8(entry.path().extension())) != ".szs") {
            continue;
        }
        // The names in the list in the fonts' Windows-1252, whatever the file system uses.
        const std::u8string u8 = entry.path().stem().u8string();
        std::string stem = utf8_to_cp1252(std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
        std::size_t digits = stem.size();
        while (digits > 0 && std::isdigit(static_cast<unsigned char>(stem[digits - 1])) != 0) {
            --digits;
        }
        const std::uint8_t first = digits < stem.size() ? static_cast<std::uint8_t>(stem[digits] - '0') : 0xFF;
        const std::string name = stem.substr(0, digits);
        if (name.empty()) {
            continue;
        }
        MenuScenario& s = by_name[lower(name)];
        // The entry takes the header of its last part: the original reads every file in alphabetical
        // order (Windows lists them so) and keeps the last header, which shows in its stars. Other
        // file systems list the files in any order, and a part whose header differs (Pirata1 and
        // Feindliche Nachbarn1 say campaign 0, D-015) must not decide the whole entry.
        const bool last = std::all_of(s.files.begin(), s.files.end(), [&](const auto& f) { return first > f.first; });
        if (last) {
            s.name = game != nullptr ? game->expand(name) : name;
            const std::u8string u8name = entry.path().stem().u8string();
            std::size_t cut = u8name.size();
            while (cut > 0 && u8name[cut - 1] >= u8'0' && u8name[cut - 1] <= u8'9') {
                --cut;
            }
            s.base = std::filesystem::path(u8name.substr(0, cut));
        }
        s.files.emplace_back(first, entry.path());
    }
    std::array<std::vector<MenuScenario>, 3> groups;
    for (auto& [key, s] : by_name) {
        // Every file of a name writes its header over the one before, in the order
        // FindFirstFile gives them (alphabetical): the last file's header counts. Pirata1 has no
        // campaign of its own, Pirata3 has it.
        const auto last = std::max_element(s.files.begin(), s.files.end(), [](const auto& a, const auto& b) {
            return lower(core::path_to_utf8(a.second.filename())) < lower(core::path_to_utf8(b.second.filename()));
        });
        if (auto bytes = core::read_file(last->second)) {
            if (auto file = assets::ChunkFile::from_bytes(std::move(bytes).value())) {
                s.header = assets::load_scenario_header(*file);
            }
        }
        std::sort(s.files.begin(), s.files.end());
        const std::size_t g = s.header.mission_number != assets::ScenarioHeader::kNone ? 0
                              : s.header.campaign != assets::ScenarioHeader::kNone     ? 1
                                                                                       : 2;
        groups[g].push_back(std::move(s));
    }
    std::stable_sort(groups[0].begin(), groups[0].end(), [](const MenuScenario& a, const MenuScenario& b) {
        return a.header.mission_number < b.header.mission_number;
    });
    // Campaigns by their stars, then their number; equal ones stay in name order.
    std::stable_sort(groups[1].begin(), groups[1].end(), [](const MenuScenario& a, const MenuScenario& b) {
        return std::pair{a.header.ranking, a.header.campaign} < std::pair{b.header.ranking, b.header.campaign};
    });
    std::stable_sort(groups[2].begin(), groups[2].end(),
                     [](const MenuScenario& a, const MenuScenario& b) { return a.header.ranking < b.header.ranking; });
    std::vector<MenuScenario> out;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (groups[g].empty()) {
            continue;
        }
        MenuScenario heading;
        heading.name = headings[g];
        heading.separator = true;
        out.push_back(std::move(heading));
        for (MenuScenario& s : groups[g]) {
            out.push_back(std::move(s));
        }
    }
    return out;
}

std::vector<MenuScenario> package_scenario_list(std::span<const assets::PackageScenario> scenarios,
                                                std::span<const assets::PackageCampaign> campaigns,
                                                std::string_view language, std::string heading) {
    std::vector<MenuScenario> out;
    if (scenarios.empty() && campaigns.empty()) {
        return out;
    }
    MenuScenario head;
    head.name = std::move(heading);
    head.separator = true;
    out.push_back(std::move(head));
    // Its high scores beside the others, by a name no scenario of the game folder has.
    const auto base_of = [](std::string id) {
        std::replace(id.begin(), id.end(), ':', '_');
        return core::path_from_utf8(id);
    };
    for (const assets::PackageCampaign& c : campaigns) {
        MenuScenario s;
        s.name = c.name(language);
        s.base = base_of(c.id);
        s.header.ranking = ranking_of_stars(c.stars);
        s.package_campaign = c.id;
        s.briefing = c.briefing(language);
        for (std::size_t i = 0; i < c.parts.size(); ++i) {
            const assets::PackageScenario* part = assets::find_scenario(scenarios, c.parts[i]);
            if (part == nullptr) {
                break;  // read_campaigns_file() lets only scenarios of the package through
            }
            s.files.emplace_back(static_cast<std::uint8_t>(i), part->file);
            s.package_parts.push_back(MenuScenario::Part{part->id, part->briefing(language)});
        }
        out.push_back(std::move(s));
    }
    for (const assets::PackageScenario& p : scenarios) {
        MenuScenario s;
        s.name = p.name(language);
        s.base = base_of(p.id);
        s.files.emplace_back(std::uint8_t{0xFF}, p.file);
        s.header.ranking = ranking_of_stars(p.stars);
        s.package_scenario = p.id;
        s.briefing = p.briefing(language);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<MenuScenario> own_scenario_list(const std::filesystem::path& folder, std::string heading,
                                            std::span<const MenuScenario> taken, std::vector<std::string>* skipped) {
    std::vector<MenuScenario> own;
    std::error_code ec;
    if (folder.empty() || !std::filesystem::is_directory(folder, ec)) {
        return own;
    }
    const std::array<std::string, 4> no_headings{};
    for (MenuScenario& s : scenario_list(folder, no_headings, nullptr)) {
        if (!s.separator) {
            own.push_back(std::move(s));
        }
    }
    // Games saved by Open1602 (and, later, maps of its own editor): one entry per file.
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec) || lower(core::path_to_utf8(entry.path().extension())) != ".o1602") {
            continue;
        }
        const std::u8string u8 = entry.path().stem().u8string();
        MenuScenario s;
        s.name = utf8_to_cp1252(std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
        if (s.name.empty()) {
            continue;
        }
        s.base = entry.path().stem();
        s.files.emplace_back(std::uint8_t{0xFF}, entry.path());
        own.push_back(std::move(s));
    }
    // A name the list has already would make two entries of one name, which share their high score
    // list and of which opening by name finds only the first: the game folder's (or the .szs) wins.
    std::vector<std::string> names;
    for (const MenuScenario& s : taken) {
        if (!s.separator) {
            names.push_back(lower(s.name));
        }
    }
    std::vector<MenuScenario> out;
    for (MenuScenario& s : own) {
        const std::string key = lower(s.name);
        if (std::find(names.begin(), names.end(), key) != names.end()) {
            if (skipped != nullptr) {
                skipped->push_back(s.name);
            }
            continue;
        }
        names.push_back(key);
        out.push_back(std::move(s));
    }
    if (out.empty()) {
        return out;
    }
    // A scenario of the editor shows the name, stars and introduction of its description (SCNI) in
    // the player's language, where it has them; it is still found by its file's name.
    for (MenuScenario& s : out) {
        if (s.files.size() != 1 || lower(core::path_to_utf8(s.files.front().second.extension())) != ".o1602") {
            continue;
        }
        const auto head = core::read_file_head(s.files.front().second, sim::kSaveInfoHead);
        const auto info = head ? sim::save_info(head->bytes, head->size) : core::Result<sim::SaveInfo>(head.error());
        if (!info || !info->meta || !info->meta->scenario) {
            continue;
        }
        const sim::ScenarioInfo& scenario = *info->meta->scenario;
        s.header.ranking = ranking_of_stars(scenario.stars);
        const std::string_view language = ui::current_language();
        if (const std::string_view name = sim::scenario_text(scenario.names, language); !name.empty()) {
            s.name = utf8_to_cp1252(name);
        }
        s.briefing = utf8_to_cp1252(sim::scenario_text(scenario.briefings, language));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const MenuScenario& a, const MenuScenario& b) { return lower(a.name) < lower(b.name); });
    MenuScenario head;
    head.name = std::move(heading);
    head.separator = true;
    out.insert(out.begin(), std::move(head));
    return out;
}

std::optional<CampaignPart> campaign_part_of(const std::filesystem::path& file) {
    const std::array<std::string, 4> headings{};
    for (const MenuScenario& s : scenario_list(file.parent_path(), headings, nullptr)) {
        if (s.separator || s.header.campaign == assets::ScenarioHeader::kNone) {
            continue;
        }
        for (const auto& [digit, path] : s.files) {
            if (path.filename() == file.filename()) {
                return CampaignPart{s.header.campaign, digit == 0xFF ? 0U : digit};
            }
        }
    }
    return std::nullopt;
}

std::optional<sim::Rules> scenario_rules_of(const std::filesystem::path& file) {
    if (lower(core::path_to_utf8(file.extension())) != ".o1602") {
        return std::nullopt;
    }
    // Only the head of the file: the settings come right after its first section.
    const auto head = core::read_file_head(file, sim::kSaveInfoHead);
    if (!head) {
        return std::nullopt;
    }
    const auto info = sim::save_info(head->bytes, head->size);
    if (!info || !info->meta || !info->meta->scenario) {
        return std::nullopt;
    }
    return info->meta->scenario->rules;
}

const std::string& MainMenu::default_name() {
    return ui::tr("menu.default_name");
}

MainMenu::MainMenu(const assets::GameDirectory& game, const assets::TextTable* texts)
    : m_game(&game),
      m_texts(texts),
      m_base(load_panel(game, "BASE.GAD")),
      m_host(load_panel(game, "HOST.GAD")),
      m_mission(load_panel(game, "MISSION.GAD")),
      m_colour(load_colour_panel(game)),
      m_banners(render::BannerTable::original()) {
    if (m_host) {
        m_host->set_selected(kNewGame, true);
        if (const assets::Gadget* stars = m_host->layout().find(kRowStars); stars != nullptr && stars->gfx > 0) {
            m_gfx_stars = stars->gfx;
        }
    }
    if (m_base && m_base->layout().find(kHistoryQuit) != nullptr) {
        m_history_buttons = true;
    }
    m_banners.add_more_players();
    if (m_colour) {
        m_colour->set_text(kNamePrompt, text("GAME", 81));
        for (std::size_t k = 0; k < m_hall.size(); ++k) {
            const assets::Gadget* flag = m_colour->layout().find(kFlag + static_cast<std::int32_t>(k));
            const assets::Gadget* outline = m_colour->layout().find(kBanner + static_cast<std::int32_t>(k));
            if (flag != nullptr && outline != nullptr) {
                m_hall[k] = HallBanner{flag->gfx, outline->gfx, outline->press_offset};
            }
        }
    }
}

std::string MainMenu::text(std::string_view section, std::size_t line) const {
    if (m_texts == nullptr) {
        return {};
    }
    const auto& lines = m_texts->section(std::string(section));
    return line < lines.size() ? lines[line] : std::string();
}

void MainMenu::open() {
    m_screen = Screen::Base;
    m_typing = false;
}

void MainMenu::set_scale(double scale) {
    for (auto* p : {&m_base, &m_host, &m_mission, &m_colour}) {
        if (*p) {
            (*p)->set_scale(scale);
        }
    }
    m_settings.set_scale(scale);
    m_mods.set_scale(scale);
    m_base_form.set_scale(scale);
}

ui::Panel* MainMenu::panel() {
    std::optional<ui::Panel>* p = &m_base;
    switch (m_screen) {
        case Screen::Host:
            p = &m_host;
            break;
        case Screen::Mission:
            p = &m_mission;
            break;
        case Screen::Settings:  // drawn by its form; the screen before it stands behind it
            p = m_settings.scope() == SetupScope::Scenario ? &m_mission : &m_colour;
            break;
        case Screen::Mods:  // drawn by its form
            break;
        case Screen::Colour:
            p = &m_colour;
            break;
        case Screen::Base:
            break;
    }
    return *p ? &**p : nullptr;
}

void MainMenu::refresh_host() {
    ui::Panel& p = *m_host;
    if (m_mode == ListMode::Load) {
        // The savegames without stars (the original's twelve fit without arrows; Open1602
        // keeps any number, so the list scrolls like the scenarios).
        const std::size_t count = m_slots.size();
        m_first = std::min(m_first, count > kRows ? count - kRows : 0);
        for (std::size_t r = 0; r < kRows; ++r) {
            const auto k = static_cast<std::int32_t>(r);
            const std::size_t i = m_first + r;
            const bool shown = i < count;
            p.set_visible(kRowText + k, shown);
            // A long name (with the time played after it) ends in "..." instead of running past the list.
            p.set_fit(kRowText + k, true);
            p.set_visible(kRowStars + k, false);
            p.set_visible(kRowPlug + k, false);
            if (!shown) {
                continue;
            }
            // Open1602 adds the time played from the savegame's META (the full line in the settings).
            std::string text = m_slots.names[i].empty() ? std::string("-") : m_slots.names[i];
            if (!m_slots.names[i].empty() && i < m_slots.meta.size() && m_slots.meta[i]) {
                text += "  (" + save_time(m_slots.meta[i]->time_ms) +
                        (m_slots.meta[i]->edited ? ui::tr("save.edited") : std::string()) + ")";
            }
            p.set_text(kRowText + k, text);
        }
        p.set_visible(kListUp, m_first > 0);
        p.set_visible(kListDown, m_first + kRows < count);
        return;
    }
    m_first = std::min(m_first, m_scenarios.size() > kRows ? m_scenarios.size() - kRows : 0);
    for (std::size_t r = 0; r < kRows; ++r) {
        const auto k = static_cast<std::int32_t>(r);
        const std::size_t i = m_first + r;
        const bool shown = i < m_scenarios.size();
        p.set_visible(kRowText + k, shown);
        p.set_fit(kRowText + k, true);
        p.set_visible(kRowPlug + k, false);  // the players of a network game
        const bool stars =
            shown && !m_scenarios[i].separator && m_scenarios[i].header.ranking != assets::ScenarioHeader::kNone;
        p.set_visible(kRowStars + k, stars);
        if (!shown) {
            continue;
        }
        p.set_text(kRowText + k, m_scenarios[i].name);
        if (stars) {
            p.set_graphic(
                kRowStars + k,
                m_gfx_stars + static_cast<std::int32_t>(std::min<std::uint32_t>(m_scenarios[i].header.ranking, 4)), 0);
        }
    }
    p.set_visible(kListUp, m_first > 0);
    p.set_visible(kListDown, m_first + kRows < m_scenarios.size());
}

void MainMenu::load_scenarios() {
    if (m_texts == nullptr) {
        return;
    }
    // Read again every time: a won game may have opened a mission since.
    m_progress = {};
    if (!m_progress_file.empty()) {
        if (auto bytes = core::read_file(m_progress_file)) {
            m_progress =
                assets::parse_progress(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        }
    }
    std::array<std::string, 4> headings;
    for (std::size_t g = 0; g < headings.size(); ++g) {
        headings[g] = text("GAME", 85 + g);
    }
    m_scenarios.clear();
    if (const auto folder = m_game->resolve("Szenes")) {
        m_scenarios = scenario_list(*folder, headings, m_game);
    }
    // The numbered missions above the progress are not offered.
    std::erase_if(m_scenarios, [&](const MenuScenario& s) {
        const std::uint32_t n = s.header.mission_number;
        return !s.separator && n >= assets::CampaignProgress::kFirstNumbered &&
               n <= assets::CampaignProgress::kLastNumbered && n > m_progress.numbered;
    });
    const auto append = [&](std::vector<MenuScenario> more) {
        m_scenarios.insert(m_scenarios.end(), std::make_move_iterator(more.begin()),
                           std::make_move_iterator(more.end()));
    };
    std::vector<std::string> skipped;
    append(own_scenario_list(m_own_scenario_folder, ui::tr("menu.own_scenarios"), m_scenarios, &skipped));
    if (skipped != m_own_skipped) {
        for (const std::string& name : skipped) {
            std::fprintf(stderr,
                         "warning: own scenario \"%s\" left out: the list has a scenario of that name already\n",
                         core::cp1252_to_utf8(name).c_str());
        }
        m_own_skipped = std::move(skipped);
    }
    append(package_scenario_list(m_package_scenarios, m_package_campaigns, ui::current_language(),
                                 ui::tr("menu.package_scenarios")));
}

std::size_t MainMenu::campaign_parts_open(const MenuScenario& s) const {
    // MISSION.GAD: a part above the campaign's progress is disabled; other scenarios have all.
    constexpr std::size_t kAll = 4;
    if (!s.package_campaign.empty()) {
        return m_progress.package_parts_open(s.package_campaign);
    }
    if (s.header.campaign == assets::ScenarioHeader::kNone) {
        return kAll;
    }
    const auto it = m_progress.campaigns.find(s.header.campaign);
    return it != m_progress.campaigns.end() ? it->second : 0;
}

std::filesystem::path MainMenu::tutorial_file(std::uint8_t chapter) {
    load_scenarios();
    for (const MenuScenario& s : m_scenarios) {
        if (s.separator || s.header.mission_number != kTutorial) {
            continue;
        }
        for (const auto& [digit, path] : s.files) {
            if (digit == chapter) {
                return path;
            }
        }
    }
    return {};
}

bool MainMenu::open_mission(std::string_view name) {
    open();
    if (!m_host || !m_mission || name.empty()) {
        return false;
    }
    load_scenarios();
    for (std::size_t i = 0; i < m_scenarios.size(); ++i) {
        const MenuScenario& s = m_scenarios[i];
        if (!s.separator && (lower(s.name) == lower(name) || lower(core::path_to_utf8(s.base)) == lower(name))) {
            select_scenario(i);
            m_screen = Screen::Mission;
            return true;
        }
    }
    return false;
}

void MainMenu::select_scenario(std::size_t index) {
    m_chosen = index;
    const MenuScenario& s = m_scenarios[index];
    m_variant = s.files.size() >= 2 ? s.files.front().first : 0xFF;
    // The tutorial at the chapter of the settings, if it has that file.
    if (s.header.mission_number == kTutorial &&
        std::any_of(s.files.begin(), s.files.end(), [&](const auto& f) { return f.first == m_tutorial_chapter; })) {
        m_variant = m_tutorial_chapter;
    }
    m_scores_first = 0;
    load_details();
}

void MainMenu::load_details() {
    const MenuScenario& s = m_scenarios[m_chosen];
    m_briefing_line = 0;
    // The briefing: the mission of the human in the file (AUFTRAG4 of the player record).
    // A package's scenario may have one of its own; otherwise the map's.
    m_briefing = s.briefing;
    if (const std::size_t part = m_variant == 0xFF ? 0 : m_variant;
        part < s.package_parts.size() && !s.package_parts[part].briefing.empty()) {
        m_briefing = s.package_parts[part].briefing;
    }
    // A scenario of the editor: the introduction of its description, read whole (the list read
    // only the head of the file).
    if (m_briefing.empty() && lower(core::path_to_utf8(chosen_file().extension())) == ".o1602") {
        if (const auto head = core::read_file_head(chosen_file(), sim::kScenarioHead)) {
            if (const auto info = sim::scenario_info_of(head->bytes, head->size)) {
                m_briefing = utf8_to_cp1252(sim::scenario_text(info->briefings, ui::current_language()));
            }
        }
    }
    std::optional<core::Bytes> map_bytes;
    if (m_briefing.empty() && lower(core::path_to_utf8(chosen_file().extension())) != ".o1602") {
        if (auto read = core::read_file(chosen_file())) {
            map_bytes = std::move(read).value();
        }
    }
    if (map_bytes) {
        if (auto file = assets::ChunkFile::from_bytes(std::move(*map_bytes))) {
            const auto players = assets::load_players(*file);
            const auto missions = assets::load_missions(*file);
            if (missions && !missions->empty()) {
                std::size_t m = 0;
                if (players) {
                    for (const assets::PlayerRecord& p : *players) {
                        if (p.kind == assets::PlayerKind::Human && p.mission < missions->size()) {
                            m = p.mission;
                            break;
                        }
                    }
                }
                m_briefing = m_game->expand((*missions)[m].text);
            }
        }
    }
    append_level_notes();
    m_scores.clear();
    if (!s.files.empty()) {
        // A campaign part has a list of its own, named by its label.
        std::filesystem::path name = s.base;
        name += ".hss";
        if (s.header.campaign != assets::ScenarioHeader::kNone && m_texts != nullptr) {
            sim::GameIdentity part_of;
            part_of.name = s.name;
            part_of.campaign = s.header.campaign;
            part_of.ranking = m_variant == 0xFF ? 0 : m_variant;  // the part, not the file's stars
            if (auto label = high_score_file(part_of, m_texts->section("KAMPAGNE"), false); !label.empty()) {
                name = std::move(label);
            }
        }
        const std::filesystem::path hss = s.files.front().second.parent_path() / name;
        const std::filesystem::path own = m_score_folder / name;
        auto bytes = m_score_folder.empty() ? core::read_file(hss) : core::read_file(own);
        if (!bytes && !m_score_folder.empty()) {
            bytes = core::read_file(hss);
        }
        if (bytes) {
            m_scores = assets::parse_high_scores(
                std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        }
    }
}

void MainMenu::append_level_notes() {
    // What sets the levels of the endless game apart, as the History Edition lists it under the
    // briefing. Its own briefings carry these lines already; the other installations get them here.
    const MenuScenario& s = m_scenarios[m_chosen];
    if (s.header.mission_number != kEndless || m_variant >= sim::NewGameOptions::kDifficulties ||
        m_briefing.find("\n* ") != std::string::npos) {
        return;
    }
    const std::string notes = ui::tr("menu.endless_level_notes." + std::to_string(m_variant));
    if (notes.empty()) {
        return;
    }
    if (!m_briefing.empty()) {
        m_briefing += '\n';
    }
    m_briefing += notes;
}

std::filesystem::path MainMenu::chosen_file() const {
    if (m_chosen >= m_scenarios.size()) {
        return {};
    }
    const MenuScenario& s = m_scenarios[m_chosen];
    for (const auto& [digit, path] : s.files) {
        if (digit == m_variant) {
            return path;
        }
    }
    return s.files.empty() ? std::filesystem::path{} : s.files.front().second;
}

void MainMenu::refresh_mission() {
    ui::Panel& p = *m_mission;
    const MenuScenario& s = m_scenarios[m_chosen];
    // The choices: the levels of the endless game, the parts of the tutorial, else the digits.
    const std::vector<std::uint8_t> digits = choice_digits(s);
    for (std::size_t i = 0; i < kChoices; ++i) {
        const auto k = static_cast<std::int32_t>(i);
        const bool shown = i < digits.size();
        p.set_visible(kChoice + k, shown);
        p.set_visible(kChoiceLabel + k, shown);
        if (!shown) {
            continue;
        }
        std::string label;
        if (s.header.mission_number == kEndless) {
            label = text("ENDLESSKIND", digits[i]);
        } else if (s.header.mission_number == kTutorial) {
            label = text("TUTORKIND", digits[i]);
        } else if (s.header.campaign != assets::ScenarioHeader::kNone) {
            label = text("KAMPAGNE", std::size_t{5} * s.header.campaign + digits[i]);  // "Der Retter"
        }
        if (label.empty()) {
            label = ui::tr("menu.part", {std::to_string(digits[i] + 1)});
        }
        p.set_text(kChoiceLabel + k, label);
        p.set_selected(kChoice + k, digits[i] == m_variant);
        // Locked parts are faded (gadget +0x19 bit 4), not greyed; clicks on them are ignored below.
        p.set_faded(kChoice + k, i > campaign_parts_open(s));
    }
    p.set_text(kBriefing, m_briefing);
    p.set_visible(kBriefing2, false);
    // The high scores, four at a time; not for the tutorial.
    const bool scores = s.header.mission_number != kTutorial;
    for (const std::int32_t id : {kScoresHeading, kScoresFrame, kScoresUp, kScoresDown}) {
        p.set_visible(id, scores);
    }
    for (std::size_t r = 0; r < kScoreRows; ++r) {
        const auto k = static_cast<std::int32_t>(r);
        const std::size_t i = m_scores_first + r;
        const bool shown = scores && i < m_scores.size() && !m_scores[i].name.empty();
        for (const std::int32_t id : {kScoreName, kScorePoints, kScoreRank}) {
            p.set_visible(id + k, shown);
        }
        // The level of an endless game as stars: sprite 21 + level, one star for "Einfach".
        const bool stars = shown && s.header.mission_number == kEndless;
        p.set_visible(kScoreStars + k, stars);
        if (!shown) {
            continue;
        }
        p.set_text(kScoreName + k, m_scores[i].name);
        p.set_text(kScorePoints + k, std::to_string(m_scores[i].points));
        p.set_text(kScoreRank + k, std::to_string(i + 1) + ".");
        if (stars) {
            p.set_graphic(kScoreStars + k,
                          m_gfx_stars + static_cast<std::int32_t>(std::min<std::uint32_t>(m_scores[i].stars, 4)), 0);
        }
    }
}

void MainMenu::refresh_colour() {
    ui::Panel& p = *m_colour;
    p.set_text(kNameBox, m_typing ? m_name + "_" : m_name);
    // The further colours' coats of arms, golden under the mouse; the banner in the hall with the
    // same pattern shows the colour (render/shape_tint.hpp).
    // While the name is typed the mouse does not choose (mouse()): no coat of arms lights up.
    const std::optional<std::int32_t> hovered = m_typing ? std::nullopt : p.hovered();
    const render::Banner* preview = nullptr;
    for (std::size_t n = 0; n < kMoreBanners; ++n) {
        const std::int32_t id = kMoreArms + static_cast<std::int32_t>(n);
        const render::Banner* banner = m_banners.get(static_cast<sim::BannerId>(sim::kFirstExtraBanner + n));
        const assets::Gadget* g = p.layout().find(id);
        if (g == nullptr) {
            continue;
        }
        const bool lit = hovered == id || (!m_typing && p.held() == id);
        set_banner_graphic(p, id, render::BannerArt::arms_at(lit ? g->gfx + kGfxArmsGold : g->gfx), banner,
                           lit ? 0 : kGfxArmsGold);
        if (lit) {
            preview = banner;
        }
    }
    for (std::size_t side = 0; side < 2; ++side) {
        const std::optional<std::int32_t> arms =
            hovered && *hovered >= kMoreArms && *hovered < kMoreArms + static_cast<std::int32_t>(kMoreBanners)
                ? hovered
                : std::nullopt;
        const bool here = arms && static_cast<std::size_t>(*arms - kMoreArms) / kArmsPerSide == side;
        p.set_text(kMoreArmsLabel + static_cast<std::int32_t>(side),
                   here && preview != nullptr ? banner_name(preview->id) : std::string());
    }
    for (std::size_t k = 0; k < m_hall.size(); ++k) {
        if (!m_hall[k]) {
            continue;
        }
        const HallBanner& h = *m_hall[k];
        const auto flag = kFlag + static_cast<std::int32_t>(k);
        const auto outline = kBanner + static_cast<std::int32_t>(k);
        if (preview != nullptr && preview->art_pattern == k) {
            // The cloth is what the outline's selected picture encloses.
            ui::ColourTint cloth;
            cloth.colour = preview->colour;
            cloth.shape = h.outline + h.outline_press - h.flag;
            p.set_graphic(flag, h.flag, 0, cloth);
            if (!p.selected(outline)) {
                ui::ColourTint edge;
                edge.colour = preview->colour;
                edge.shape = h.outline_press;
                p.set_graphic(outline, h.outline, h.outline_press, edge);
                continue;
            }
        } else {
            p.set_graphic(flag, h.flag, 0);
        }
        p.set_graphic(outline, h.outline, h.outline_press);
    }
}

std::vector<ui::DrawItem> MainMenu::draw(const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    ui::Panel* p = panel();
    if (p == nullptr) {
        return {};
    }
    if (m_screen == Screen::Host) {
        refresh_host();
    } else if (m_screen == Screen::Mission && m_chosen < m_scenarios.size()) {
        refresh_mission();
        // The briefing's end is known only with the fonts: "down" past it would blank the text.
        const std::int32_t last = p->max_scroll(kBriefing, fonts);
        m_briefing_line = std::min(m_briefing_line, last);
        p->set_scroll(kBriefing, m_briefing_line);
        m_briefing_last = last;
        m_briefing_rows = p->flow_rows(kBriefing);
        // Its arrows and slider are faded while the whole briefing fits; then they
        // scroll nothing and the knob rests in the middle (with 50 of 100).
        for (const std::int32_t id : {kBriefingUp, kBriefingDown, kBriefingSlider}) {
            p->set_faded(id, last == 0);
        }
        // The knob at (first line + rows / 2) of (lines + rows - 1), counted from the
        // bottom.
        if (last > 0) {
            p->set_slider(kBriefingSlider, 1, briefing_slider_range() - m_briefing_rows / 2 - m_briefing_line,
                          briefing_slider_range());
        } else {
            p->set_slider(kBriefingSlider, 1, 50, 100);
        }
    } else if (m_screen == Screen::Colour) {
        refresh_colour();
    } else if (m_screen == Screen::Settings) {
        return m_settings.draw(fonts);
    } else if (m_screen == Screen::Mods) {
        return m_mods.draw(m_mods_model, fonts);
    }
    std::vector<ui::DrawItem> out = p->draw(fonts);
    if (m_screen == Screen::Base) {
        layout_base_form();
        const std::vector<ui::DrawItem> own = m_base_form.draw(fonts);
        out.insert(out.end(), own.begin(), own.end());
    }
    return out;
}

void MainMenu::layout_base_form() {
    m_base_form.clear();
#ifndef O1602_EXPERIMENTAL_VITA
    m_base_form.button(kModsButton, kModsButtonRect, ui::tr("menu.mods"));
#endif
    m_base_form.button(kEditorButton, kEditorButtonRect, ui::tr("menu.editor"));
}

std::vector<MenuEvent> MainMenu::mouse(std::int32_t x, std::int32_t y, bool down, ui::MouseButton button) {
    std::vector<MenuEvent> out;
    if (m_screen == Screen::Settings) {
        switch (m_settings.mouse(x, y, down, button)) {
            case GameSettingsMenu::Result::Start: {
                if (m_settings.scope() == SetupScope::Scenario) {
                    MenuEvent ev{MenuEvent::Kind::Scenario};
                    if (!m_scenario_own_rules) {
                        m_scenario = m_settings.setup();
                    }
                    ev.path = chosen_file();
                    ev.name = m_name;
                    ev.setup = m_settings.setup();
                    ev.scenario_rules = m_scenario_own_rules;
                    const MenuScenario& chosen = m_scenarios[m_chosen];
                    ev.package_scenario = chosen.package_scenario;
                    if (!chosen.package_campaign.empty()) {
                        const std::size_t part = m_variant == 0xFF ? 0 : m_variant;
                        ev.package_campaign = chosen.package_campaign;
                        ev.package_scenario =
                            part < chosen.package_parts.size() ? chosen.package_parts[part].scenario : std::string();
                        ev.level = static_cast<std::uint8_t>(part);
                    }
                    out.push_back(std::move(ev));
                    break;
                }
                m_endless = m_settings.setup();
                MenuEvent ev{MenuEvent::Kind::NewEndless};
                ev.level = m_variant == 0xFF ? 0 : m_variant;
                ev.banner = m_banner;
                ev.name = m_name;
                ev.setup = m_endless;
                out.push_back(std::move(ev));
                m_endless.world_seed = 0;  // the next game a new world, unless the islands' page shows one
                break;
            }
            case GameSettingsMenu::Result::Back:
                back();
                break;
            case GameSettingsMenu::Result::None:
                break;
        }
        return out;
    }
    if (m_screen == Screen::Mods) {
        if (const auto e = m_mods.mouse(m_mods_model, x, y, down, button)) {
            const ModsModel::Entry* entry =
                e->index < m_mods_model.entries.size() ? &m_mods_model.entries[e->index] : nullptr;
            if (e->kind == ModsMenu::Event::Kind::Close) {
                m_screen = Screen::Base;
            } else if (entry != nullptr) {
                MenuEvent ev{e->kind == ModsMenu::Event::Kind::Toggle ? MenuEvent::Kind::ToggleMod
                                                                      : MenuEvent::Kind::AdoptMod};
                ev.name = entry->id;
                ev.path = entry->folder;
                out.push_back(std::move(ev));
            }
        }
        return out;
    }
    if (m_screen == Screen::Base && button == ui::MouseButton::Left) {
        layout_base_form();
        const auto clicked = m_base_form.mouse(x, y, down);
        if (clicked == kModsButton) {
            m_mods.open();
            m_mods_model.notice.clear();
            m_screen = Screen::Mods;
            out.push_back(MenuEvent{MenuEvent::Kind::Mods});
            return out;
        }
        if (clicked == kEditorButton) {
            out.push_back(MenuEvent{MenuEvent::Kind::Editor});
            return out;
        }
    }
    ui::Panel* p = panel();
    if (p == nullptr || button != ui::MouseButton::Left) {
        return out;
    }
    if (m_screen == Screen::Colour && m_typing) {
        // The name first: banners and coats of arms answer only once Enter confirmed it, which
        // also brings "Unter welcher Flagge wollt Ihr segeln?" (MenuEvent::Kind::NameEntered).
        return out;
    }
    for (const ui::PanelEvent& e : p->mouse(x, y, down)) {
        if (e.kind == ui::PanelEvent::Kind::Slid && m_screen == Screen::Mission && e.id == kBriefingSlider &&
            m_briefing_last > 0) {
            const std::int32_t range = briefing_slider_range();
            m_briefing_line =
                std::clamp(range - m_briefing_rows / 2 - p->slider(kBriefingSlider, 1, range), 0, m_briefing_last);
            continue;
        }
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            continue;
        }
        switch (m_screen) {
            case Screen::Base: {
                const std::int32_t below = m_history_buttons ? 1 : 0;  // the buttons after Options
                if (e.id == kSingle && m_host) {
                    load_scenarios();
                    m_screen = Screen::Host;
                } else if (e.id == kMulti) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Multiplayer});
                } else if (m_history_buttons && e.id == kHistoryOptions) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Display});
                } else if (e.id == kCredits + below) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Credits});
                } else if (e.id == kIntro + below) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Intro});
                } else if (e.id == kQuit + below) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Quit});
                }
                break;
            }
            case Screen::Host:
                if (e.id == kNewGame) {
                    m_mode = ListMode::New;
                    m_first = 0;
                } else if (e.id == kLoadGame) {
                    m_mode = ListMode::Load;
                    m_first = 0;
                } else if (e.id == kContinue) {
                    out.push_back(MenuEvent{MenuEvent::Kind::ContinueLast});
                } else if (e.id == kMainMenu) {
                    m_screen = Screen::Base;
                } else if (e.id == kListUp) {
                    m_first -= std::min(m_first, kRows);
                } else if (e.id == kListDown) {
                    m_first += kRows;
                } else if (e.id >= kRowText && e.id < kRowText + static_cast<std::int32_t>(kRows)) {
                    const auto r = static_cast<std::size_t>(e.id - kRowText);
                    if (m_mode == ListMode::Load) {
                        if (const std::size_t i = m_first + r; i < m_slots.size() && !m_slots.names[i].empty()) {
                            MenuEvent load{MenuEvent::Kind::LoadSlot};
                            load.slot = i;
                            out.push_back(std::move(load));
                        }
                    } else if (m_first + r < m_scenarios.size() && !m_scenarios[m_first + r].separator && m_mission) {
                        select_scenario(m_first + r);
                        m_screen = Screen::Mission;
                    }
                }
                break;
            case Screen::Mission:
                if (e.id >= kChoice && e.id < kChoice + static_cast<std::int32_t>(kChoices)) {
                    const std::vector<std::uint8_t> digits = choice_digits(m_scenarios[m_chosen]);
                    const auto i = static_cast<std::size_t>(e.id - kChoice);
                    if (i < digits.size() && i <= campaign_parts_open(m_scenarios[m_chosen])) {
                        m_variant = digits[i];
                        load_details();  // the briefing of the chosen part
                    }
                } else if (e.id == kStart) {
                    const MenuScenario& s = m_scenarios[m_chosen];
                    if (s.header.mission_number == kEndless && m_colour) {
                        m_screen = Screen::Colour;  // name and banner first
                        // Each visit shows the further colours in other patterns (two runs of four).
                        m_banners.shuffle_art_patterns(sim::kFirstExtraBanner, kMoreBanners, std::random_device{}());
                        // The name can be typed at once (as the original, mode 5);
                        // the last one, or "Namenlos", stands there until the first key.
                        if (m_name.empty()) {
                            m_name = default_name();
                        }
                        m_typing = true;
                        m_name_fresh = true;
                    } else if (s.header.mission_number != kTutorial) {
                        // The rules first (Open1602's own dialog; the original starts here).
                        // A scenario of the editor made for settings of its own begins from them.
                        EndlessSetup setup = m_scenario;
                        const std::optional<sim::Rules> own = scenario_rules_of(chosen_file());
                        m_scenario_own_rules = own.has_value();
                        if (own) {
                            setup.rules = *own;
                        }
                        m_settings.open(setup, SetupScope::Scenario);
                        m_screen = Screen::Settings;
                    } else {
                        // The tutorial teaches the game as it is: no settings.
                        MenuEvent ev{MenuEvent::Kind::Scenario};
                        ev.path = chosen_file();
                        ev.name = m_name;
                        ev.tutorial_chapter = static_cast<std::uint8_t>(m_variant == 0xFF ? 0 : m_variant);
                        out.push_back(std::move(ev));
                    }
                } else if (e.id == kCancel) {
                    m_screen = Screen::Host;
                } else if (e.id == kBriefingUp) {
                    m_briefing_line = std::max(0, m_briefing_line - 1);
                } else if (e.id == kBriefingDown) {
                    ++m_briefing_line;
                } else if (e.id == kScoresUp) {
                    m_scores_first -= std::min<std::size_t>(m_scores_first, kScoreRows);
                } else if (e.id == kScoresDown && m_scores_first + kScoreRows < 12) {
                    m_scores_first += kScoreRows;
                }
                break;
            case Screen::Colour:
                if (e.id == kNameBox) {
                    m_typing = true;
                } else if ((e.id >= kBanner && e.id < kBanner + static_cast<std::int32_t>(sim::kOriginalPlayers)) ||
                           (e.id >= kMoreArms && e.id < kMoreArms + static_cast<std::int32_t>(kMoreBanners))) {
                    // Then the game's settings (Open1602's own dialog; the original starts here).
                    m_typing = false;
                    m_banner = e.id < kMoreArms
                                   ? static_cast<sim::BannerId>(e.id - kBanner)
                                   : static_cast<sim::BannerId>(sim::kFirstExtraBanner + (e.id - kMoreArms));
                    m_settings.open(m_endless, SetupScope::Endless);
                    m_settings.set_level(m_variant == 0xFF ? 0 : m_variant);
                    m_screen = Screen::Settings;
                }
                break;
            case Screen::Settings:
            case Screen::Mods:
                break;  // the forms', above
        }
    }
    return out;
}

bool MainMenu::wheel(std::int32_t notches) {
    switch (m_screen) {
        case Screen::Host: {
            const std::size_t count = m_mode == ListMode::Load ? m_slots.size() : m_scenarios.size();
            m_first = ui::wheel_scroll(m_first, count, kRows, notches);
            return true;
        }
        case Screen::Mission:
            // As its arrows: a line per notch, up to where the end of the text shows.
            m_briefing_line = std::clamp(m_briefing_line - notches, 0, std::max(0, m_briefing_last));
            return true;
        case Screen::Mods:
            m_mods.wheel(m_mods_model, notches);
            return true;
        case Screen::Base:
        case Screen::Settings:
        case Screen::Colour:
            break;
    }
    return false;
}

bool MainMenu::back() {
    m_typing = false;
    switch (m_screen) {
        case Screen::Base:
            return false;
        case Screen::Host:
            m_screen = Screen::Base;
            break;
        case Screen::Mission:
            m_screen = Screen::Host;
            break;
        case Screen::Colour:
            m_screen = Screen::Mission;
            break;
        case Screen::Mods:
            m_screen = Screen::Base;
            break;
        case Screen::Settings:
            if (m_settings.back()) {
                break;  // from the victory conditions to the settings
            }
            // Kept for the next time.
            if (m_settings.scope() == SetupScope::Scenario) {
                if (!m_scenario_own_rules) {
                    m_scenario = m_settings.setup();
                }
                m_screen = Screen::Mission;
            } else {
                m_endless = m_settings.setup();
                m_screen = Screen::Colour;
            }
            break;
    }
    return true;
}

void MainMenu::type(std::string_view text) {
    if (!m_typing) {
        return;
    }
    constexpr std::size_t kMaxName = 19;
    if (std::exchange(m_name_fresh, false) && !text.empty()) {
        m_name.clear();
    }
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && m_name.size() < kMaxName) {
            m_name.push_back(c);
        }
    }
}

std::optional<MenuEvent> MainMenu::key(int key) {
    if (!m_typing) {
        return std::nullopt;
    }
    if (key == 8) {
        if (std::exchange(m_name_fresh, false)) {
            m_name.clear();
        } else if (!m_name.empty()) {
            m_name.pop_back();
        }
    } else if (key == 13) {
        m_typing = false;
        m_name_fresh = false;
        if (m_name.empty()) {
            m_name = default_name();
        }
        return MenuEvent{MenuEvent::Kind::NameEntered};
    } else if (key == 27) {
        back();  // straight back to the choice of the game, as Escape outside the field
    }
    return std::nullopt;
}

}  // namespace o1602::app
