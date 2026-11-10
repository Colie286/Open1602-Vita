// SPDX-License-Identifier: GPL-3.0-or-later
#include "options_panel.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "o1602/ui/list_scroll.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

// OPTION.GAD
constexpr std::int32_t kOptionTitle = 47002;
constexpr std::int32_t kPageDisk = 47011;
constexpr std::int32_t kPageMusik = 47012;
constexpr std::int32_t kPageAnno = 47013;
constexpr std::int32_t kMonitor = 47014;
constexpr std::int32_t kMission = 47015;
constexpr std::int32_t kHistoryGameOptions = 47016;  ///< the History Edition's own settings screen
/// The original's three resolutions; Open1602: window, full screen and the window "Anzeige".
constexpr std::int32_t kResolution = 47021;  ///< + 0..2
constexpr std::int32_t kResolutionLabel = 47031;
constexpr std::int32_t kWindowRow = kResolution;
constexpr std::int32_t kFullscreenRow = kResolution + 1;
constexpr std::int32_t kDisplayRow = kResolution + 2;
// DISK.GAD
constexpr std::int32_t kDiskTitle = 48002;
constexpr std::int32_t kSave = 48011;
constexpr std::int32_t kLoad = 48012;
constexpr std::int32_t kQuit = 48013;
constexpr std::int32_t kSlotName = 48021;    ///< + slot
constexpr std::int32_t kSlotHumans = 48041;  ///< + slot
constexpr std::int32_t kSlotFrame = 48061;   ///< + slot
constexpr std::size_t kSlotRows = 12;        ///< rows of the layout at most (the History Edition's: 10)
/// Open1602: the savegames scroll; the last row gives its place to the arrows of MUSIK.GAD (the
/// History Edition has arrows of its own below the list).
constexpr std::int32_t kSlotsDown = 48081;  ///< added to DISK.GAD
constexpr std::int32_t kSlotsUp = 48082;
// The History Edition's DISK.GAD: a button for a new savegame and arrows of its own below the list.
constexpr std::int32_t kHistoryNewSave = 48014;
constexpr std::int32_t kHistorySlotsDown = 48016;
constexpr std::int32_t kHistorySlotsUp = 48017;
// Buttons the History Edition added that Open1602 has no use for yet: they are left out. Taken
// out of this list, a button shows again (and then needs something to do in mouse()).
constexpr std::array<std::int32_t, 2> kUnusedHistoryButtons{kHistoryGameOptions, kHistoryNewSave};
// MUSIK.GAD
constexpr std::int32_t kMusikTitle = 49002;
constexpr std::int32_t kListDown = 49011;
constexpr std::int32_t kListUp = 49012;
constexpr std::int32_t kMusicLess = 49013;
constexpr std::int32_t kMusicMore = 49014;
constexpr std::int32_t kMusicSlider = 49015;
constexpr std::int32_t kEffectsLess = 49016;
constexpr std::int32_t kEffectsMore = 49017;
constexpr std::int32_t kEffectsSlider = 49018;
constexpr std::int32_t kSequential = 49021;
constexpr std::int32_t kRandom = 49022;
constexpr std::int32_t kTest = 49023;
constexpr std::int32_t kMusicSwitch = 49024;
constexpr std::int32_t kMusicLabel = 49025;
constexpr std::int32_t kEffectsSwitch = 49026;
constexpr std::int32_t kEffectsLabel = 49027;
constexpr std::int32_t kTrackBox = 49031;    ///< + row
constexpr std::int32_t kTrackTitle = 49041;  ///< + row
constexpr std::int32_t kTrackFrame = 49051;  ///< + row
constexpr std::size_t kTrackRows = 6;
// ANNO.GAD
constexpr std::int32_t kSpeechBack = 66001;  ///< the box behind the voice rows (12 rows tall)
constexpr std::int32_t kSpeechHeading = 66002;
constexpr std::int32_t kSpeechTitle = 66003;
constexpr std::int32_t kSpeechSwitch = 66004;
constexpr std::int32_t kVideoBack = 66005;  ///< the video part's picture, from the voice box's edge
constexpr std::int32_t kVideoHeading = 66006;
constexpr std::int32_t kVideoTitle = 66007;
constexpr std::int32_t kVideoSwitch = 66008;
constexpr std::int32_t kSpeechBox = 66021;      ///< + kind - 1
constexpr std::int32_t kSpeechLabel = 66031;    ///< + kind - 1
constexpr std::size_t kOriginalSpeechRows = 7;  ///< [SPEECHKIND]
/// Open1602 adds a row for the trade routes (audio::kSpeechRoutes) below the original's seven.
constexpr std::size_t kSpeechRows = audio::kMessageKinds - 1;
static_assert(audio::kSpeechRoutes == kSpeechRows, "the trade routes are the last row");
constexpr std::int32_t kSpeechPitch = 25;  ///< ANNO.GAD's rows
/// The page has room for an eighth row only in the stone around the two headings: the voice
/// heading moves up 4, the voice box 12 (it is a picture of twelve rows,
/// of which seven show in the original; moved up a row more it shows eight), the video heading 7
/// down. The video part's picture starts with the voice box's bottom edge, then stone, the video
/// heading and the video box; it is drawn in three pieces with the stone between them left out.
constexpr std::int32_t kSpeechHeadingUp = 4;
constexpr std::int32_t kSpeechBoxUp = 12;
constexpr std::int32_t kVideoHeadingDown = 7;
struct PictureBand {
    std::int32_t id;     ///< kVideoBack, or a piece added after it
    std::int32_t first;  ///< rows of the picture
    std::int32_t count;
    std::int32_t dy;  ///< moved down
};
constexpr std::array<PictureBand, 3> kVideoBackBands{{
    {kVideoBack, 52, 77, 0},                     // the video box as in the original
    {66091, 0, 4, kSpeechPitch - kSpeechBoxUp},  // the voice box's bottom edge
    {66092, 10, 35, kVideoHeadingDown},          // under the video heading
}};
constexpr std::int32_t kVideoBox = 66041;  ///< kinds {1, 2}, 5, 7
constexpr std::int32_t kVideoLabel = 66051;

constexpr std::int32_t kMinVolume = -4000;  ///< 1/100 dB
constexpr std::int32_t kVolumeStep = 50;

/// DISK.GAD with the list arrows of MUSIK.GAD (their pictures and sizes) in the place of the last
/// savegame row.
assets::GadgetLayout with_slot_arrows(assets::GadgetLayout disk, const assets::GadgetLayout& musik) {
    if (disk.find(kHistorySlotsDown) != nullptr && disk.find(kHistorySlotsUp) != nullptr) {
        return disk;  // the History Edition's own
    }
    const assets::Gadget* last_row = disk.find(kSlotName + static_cast<std::int32_t>(kSlotRows - 1));
    const assets::Gadget* down = musik.find(kListDown);
    if (last_row == nullptr || down == nullptr) {
        return disk;
    }
    const std::int32_t row_y = last_row->pos[1];  // copied: the gadgets grow below
    for (const auto& [from, to] : {std::pair{kListDown, kSlotsDown}, std::pair{kListUp, kSlotsUp}}) {
        const assets::Gadget* g = musik.find(from);
        if (g == nullptr) {
            continue;
        }
        assets::Gadget arrow = *g;
        arrow.id = to;
        arrow.base = static_cast<std::int32_t>(disk.gadgets.size());
        arrow.pos[1] = row_y + (g->pos[1] - down->pos[1]);
        disk.gadgets.push_back(arrow);
    }
    return disk;
}

/// ANNO.GAD with an eighth voice row, the trade routes: the same box and label as the seven above
/// it, the page's pictures unchanged but moved closer together (kSpeechHeadingUp ...).
assets::GadgetLayout with_routes_row(assets::GadgetLayout anno) {
    const auto last = static_cast<std::int32_t>(kOriginalSpeechRows - 1);
    const auto back = anno.index_of(kVideoBack);
    const assets::Gadget* box = anno.find(kSpeechBox + last);
    const assets::Gadget* label = anno.find(kSpeechLabel + last);
    if (!back || anno.find(kSpeechBack) == nullptr || box == nullptr || label == nullptr) {
        return anno;
    }
    std::vector<assets::Gadget> rows;
    for (const assets::Gadget* g : {box, label}) {
        assets::Gadget row = *g;
        row.id += 1;
        row.pos[1] += kSpeechPitch - kSpeechBoxUp;
        rows.push_back(row);
    }
    const auto move = [&anno](std::int32_t id, std::int32_t dy) {
        for (assets::Gadget& g : anno.gadgets) {
            if (g.id == id) {
                g.pos[1] += dy;
            }
        }
    };
    move(kSpeechHeading, -kSpeechHeadingUp);
    move(kSpeechTitle, -kSpeechHeadingUp);
    move(kSpeechBack, -kSpeechBoxUp);
    for (std::int32_t k = 0; k <= last; ++k) {
        move(kSpeechBox + k, -kSpeechBoxUp);
        move(kSpeechLabel + k, -kSpeechBoxUp);
    }
    move(kVideoHeading, kVideoHeadingDown);
    move(kVideoTitle, kVideoHeadingDown);
    // The pieces of the video part's picture follow it; the indices behind them grow.
    const auto after = static_cast<std::int32_t>(*back + 1);
    const auto added = static_cast<std::int32_t>(kVideoBackBands.size() - 1);
    for (assets::Gadget& g : anno.gadgets) {
        if (g.base >= after) {
            g.base += added;
        }
    }
    std::vector<assets::Gadget> pieces;
    for (std::size_t i = 1; i < kVideoBackBands.size(); ++i) {
        assets::Gadget piece = anno.gadgets[*back];
        piece.id = kVideoBackBands[i].id;
        piece.pos[1] += kVideoBackBands[i].dy;
        pieces.push_back(piece);
    }
    anno.gadgets.insert(anno.gadgets.begin() + after, pieces.begin(), pieces.end());
    for (assets::Gadget& row : rows) {
        if (row.base >= after) {
            row.base += added;
        }
    }
    anno.gadgets.insert(anno.gadgets.end(), rows.begin(), rows.end());
    return anno;
}

bool track_enabled(const audio::AudioSettings& a, const std::string& title) {
    return std::find(a.disabled_songs.begin(), a.disabled_songs.end(), title) == a.disabled_songs.end();
}

void set_track_enabled(audio::AudioSettings& a, const std::string& title, bool on) {
    std::erase(a.disabled_songs, title);
    if (!on) {
        a.disabled_songs.push_back(title);
    }
}

}  // namespace

OptionsPanel::OptionsPanel(assets::GadgetLayout option, assets::GadgetLayout disk, assets::GadgetLayout musik,
                           assets::GadgetLayout anno, const assets::TextTable* texts)
    : m_option(std::move(option), 1.0),
      m_disk(with_slot_arrows(std::move(disk), musik), 1.0),
      m_musik(std::move(musik), 1.0),
      m_anno(with_routes_row(std::move(anno)), 1.0),
      m_texts(texts) {
    m_option.set_text(kOptionTitle, text("GAME", 25));
    m_disk.set_text(kDiskTitle, text("GAME", 26));
    m_musik.set_text(kMusikTitle, text("GAME", 27));
    m_musik.set_text(kMusicLabel, text("GAME", 30));
    m_musik.set_text(kEffectsLabel, text("GAME", 31));
    m_anno.set_text(kSpeechTitle, text("GAME", 28));
    m_anno.set_text(kVideoTitle, text("GAME", 29));
    for (std::size_t k = 0; k < kOriginalSpeechRows; ++k) {
        m_anno.set_text(kSpeechLabel + static_cast<std::int32_t>(k), text("SPEECHKIND", k));
    }
    for (std::size_t k = 0; k < 3; ++k) {
        m_anno.set_text(kVideoLabel + static_cast<std::int32_t>(k), text("VIDEOKIND", k));
    }
    if (m_anno.layout().find(kVideoBackBands[1].id) != nullptr) {
        for (const PictureBand& b : kVideoBackBands) {
            m_anno.set_rows(b.id, b.first, b.count);
        }
    }
    const assets::GadgetLayout& l = m_disk.layout();
    if (const auto g = l.index_of(kSlotHumans)) {
        m_humans_icon = l.gadgets[*g].gfx;
    }
    while (m_slot_rows < kSlotRows && l.find(kSlotName + static_cast<std::int32_t>(m_slot_rows)) != nullptr) {
        ++m_slot_rows;
    }
    if (l.find(kHistorySlotsDown) != nullptr && l.find(kHistorySlotsUp) != nullptr) {
        m_slots_down = kHistorySlotsDown;
        m_slots_up = kHistorySlotsUp;
        m_slot_rows_shown = m_slot_rows;
    } else {
        m_slots_down = kSlotsDown;
        m_slots_up = kSlotsUp;
        m_slot_rows_shown = m_slot_rows > 0 ? m_slot_rows - 1 : 0;
    }
    for (const std::int32_t id : kUnusedHistoryButtons) {
        m_option.set_visible(id, false);
        m_disk.set_visible(id, false);
    }
}

std::string OptionsPanel::text(std::string_view section, std::size_t line) const {
    if (m_texts == nullptr) {
        return {};
    }
    const auto& lines = m_texts->section(std::string(section));
    return line < lines.size() ? lines[line] : std::string();
}

void OptionsPanel::set_scale(double scale) {
    for (ui::Panel* p : {&m_option, &m_disk, &m_musik, &m_anno}) {
        p->set_scale(scale);
    }
}

ui::Panel& OptionsPanel::panel() {
    switch (m_page) {
        case Page::Disk:
            return m_disk;
        case Page::Musik:
            return m_musik;
        case Page::Anno:
            return m_anno;
        case Page::Root:
            break;
    }
    return m_option;
}

void OptionsPanel::opened() noexcept {
    m_page = Page::Root;
    stop_editing();
    m_renamed = false;
}

bool OptionsPanel::back() {
    if (m_page == Page::Root) {
        return false;
    }
    stop_editing();
    m_renamed = false;
    m_page = Page::Root;
    return true;
}

void OptionsPanel::stop_editing() {
    m_editing = false;
}

void OptionsPanel::refresh(const OptionsModel& model) {
    switch (m_page) {
        case Page::Root:
            // The original offers no resolutions in a game of several players (0x00439630); window and
            // full screen concern this computer alone, so Open1602 offers them always. The texts are
            // set here: the language may change after the panel was made.
            m_option.set_text(kResolutionLabel, ui::tr("options.window"));
            m_option.set_text(kResolutionLabel + 1, ui::tr("options.fullscreen"));
            m_option.set_text(kResolutionLabel + 2, ui::tr("options.display"));
#ifdef O1602_EXPERIMENTAL_VITA
            m_option.set_hidden(kWindowRow,true);m_option.set_hidden(kFullscreenRow,true);
            m_option.set_hidden(kResolutionLabel,true);m_option.set_hidden(kResolutionLabel+1,true);
#endif
            m_option.set_selected(kWindowRow, !model.fullscreen);
            m_option.set_selected(kFullscreenRow, model.fullscreen);
            m_option.set_selected(kDisplayRow, false);
            m_option.set_selected(kMission, model.mission_window);
            break;
        case Page::Disk:
            refresh_disk(model);
            break;
        case Page::Musik:
            refresh_musik(model);
            break;
        case Page::Anno:
            refresh_anno(model);
            break;
    }
}

void OptionsPanel::refresh_disk(const OptionsModel& model) {
    const std::size_t count = model.slot_names.size();
    m_slot_first = std::min(m_slot_first, count > m_slot_rows_shown ? count - m_slot_rows_shown : 0);
    for (std::size_t r = 0; r < m_slot_rows; ++r) {
        const auto k = static_cast<std::int32_t>(r);
        const std::size_t i = m_slot_first + r;
        const bool shown = r < m_slot_rows_shown && i < count;
        m_disk.set_visible(kSlotName + k, shown);
        m_disk.set_visible(kSlotFrame + k, shown);
        if (!shown) {
            m_disk.set_visible(kSlotHumans + k, false);
            continue;
        }
        // The name being typed shows with the cursor; it is kept only when saved (U-037).
        const bool typing = m_editing && i == m_slot;
        const bool renamed = m_renamed && i == m_slot;
        const std::uint8_t humans = i < model.slot_humans.size() ? model.slot_humans[i] : 0;
        // The icons show two to four humans; more are counted after the name (LC-6).
        std::string name = renamed ? m_name : model.slot_names[i];
        if (humans > 4 && !typing) {
            name = ui::tr("options.slot.humans", {name, std::to_string(humans)});
        }
        if (name.empty()) {
            name = ui::tr("options.slot.empty");  // a free slot shows as one, not as a blank row
        }
        m_disk.set_text(kSlotName + k, typing ? m_name + "_" : name);
        // A long name ends in "..." instead of running past the row, or under the icon of the
        // humans; the one being typed is whole.
        std::optional<std::int32_t> room;
        const auto name_at = m_disk.position(kSlotName + k);
        const auto icon_at = m_disk.position(kSlotHumans + k);
        if (humans >= 2 && name_at && icon_at && (*icon_at)[0] > (*name_at)[0]) {
            room = (*icon_at)[0] - (*name_at)[0];
        }
        m_disk.set_fit(kSlotName + k, !typing, room);
        m_disk.set_visible(kSlotHumans + k, humans >= 2);
        if (humans >= 2) {
            m_disk.set_graphic(kSlotHumans + k, m_humans_icon - 1 + std::min<std::int32_t>(humans, 4), 0);
        }
        m_disk.set_selected(kSlotFrame + k, i == m_slot);
        m_disk.set_hidden(kSlotFrame + k, i != m_slot);
    }
    const bool scrolls = count > m_slot_rows_shown;
    m_disk.set_visible(m_slots_up, scrolls);
    m_disk.set_visible(m_slots_down, scrolls);
    m_disk.set_disabled(m_slots_up, m_slot_first == 0);
    m_disk.set_disabled(m_slots_down, m_slot_first + m_slot_rows_shown >= count);
    m_disk.set_disabled(kSave, m_slot < model.reserved_slots);
    // A client of a game of several players cannot load (0x00439630).
    m_disk.set_visible(kLoad, !model.multiplayer);
}

void OptionsPanel::refresh_musik(const OptionsModel& model) {
    const audio::AudioSettings& a = model.audio;
    const std::size_t count = model.tracks.size();
    m_list_first = std::min(m_list_first, count > kTrackRows ? count - kTrackRows : 0);
    for (std::size_t r = 0; r < kTrackRows; ++r) {
        const auto k = static_cast<std::int32_t>(r);
        const std::size_t t = m_list_first + r;
        const bool shown = t < count;
        m_musik.set_visible(kTrackBox + k, shown);
        m_musik.set_visible(kTrackTitle + k, shown);
        m_musik.set_visible(kTrackFrame + k, shown);
        if (!shown) {
            continue;
        }
        m_musik.set_text(kTrackTitle + k, model.tracks[t]);
        m_musik.set_selected(kTrackBox + k, track_enabled(a, model.tracks[t]));
        m_musik.set_hidden(kTrackFrame + k, model.tracks[t] != model.playing);
    }
    // The original hides the up arrow at the top but only greys the down arrow at the bottom (M7);
    // Open1602 greys both.
    m_musik.set_disabled(kListUp, m_list_first == 0);
    m_musik.set_disabled(kListDown, m_list_first + kTrackRows >= count);
    if (m_musik.held() != kMusicSlider) {
        m_musik.set_slider(kMusicSlider, 0, a.music_volume - kMinVolume, -kMinVolume);
    }
    if (m_musik.held() != kEffectsSlider) {
        m_musik.set_slider(kEffectsSlider, 0, a.effect_volume - kMinVolume, -kMinVolume);
    }
    m_musik.set_selected(kSequential, !a.random_music);
    m_musik.set_selected(kRandom, a.random_music);
    m_musik.set_selected(kMusicSwitch, !a.music);  // crossed out: off
    m_musik.set_selected(kEffectsSwitch, !a.effects);
}

void OptionsPanel::refresh_anno(const OptionsModel& model) {
    const audio::AudioSettings& a = model.audio;
    // Set here: the language may change after the panel was made.
    m_anno.set_text(kSpeechLabel + static_cast<std::int32_t>(kSpeechRows - 1), ui::tr("options.speech.routes"));
    m_anno.set_selected(kSpeechSwitch, !a.speech[0]);  // crossed out: all off
    for (std::size_t k = 1; k < a.speech.size(); ++k) {
        const auto id = kSpeechBox + static_cast<std::int32_t>(k - 1);
        m_anno.set_selected(id, a.speech[k]);
        m_anno.set_disabled(id, !a.speech[0]);  // drawn faded while all are off
    }
    m_anno.set_selected(kVideoSwitch, !a.video[0]);
    m_anno.set_selected(kVideoBox, a.video[1] || a.video[2]);
    m_anno.set_selected(kVideoBox + 1, a.video[5]);
    m_anno.set_selected(kVideoBox + 2, a.video[7]);
    for (std::int32_t i = 0; i < 3; ++i) {
        m_anno.set_disabled(kVideoBox + i, !a.video[0]);
    }
}

std::vector<OptionsEvent> OptionsPanel::mouse(std::int32_t x, std::int32_t y, bool down, OptionsModel& model) {
    std::vector<OptionsEvent> out;
    audio::AudioSettings& a = model.audio;
    const auto audio_changed = [&] { out.push_back(OptionsEvent{OptionsEvent::Kind::Audio}); };
    for (const ui::PanelEvent& e : panel().mouse(x, y, down)) {
        if (e.kind == ui::PanelEvent::Kind::Slid) {
            // Sent every frame while the slider is held: only a new value is passed on, and only
            // as a live change, so that the settings are written once when it is let go.
            std::int32_t* volume = e.id == kMusicSlider     ? &a.music_volume
                                   : e.id == kEffectsSlider ? &a.effect_volume
                                                            : nullptr;
            if (volume != nullptr) {
                const std::int32_t now = m_musik.slider(e.id, 0, -kMinVolume) + kMinVolume;
                if (now != *volume) {
                    *volume = now;
                    m_volume_unsaved = true;
                    out.push_back(OptionsEvent{OptionsEvent::Kind::Audio, 1});
                }
            }
            continue;
        }
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            continue;
        }
        switch (m_page) {
            case Page::Root:
                if (e.id == kPageDisk) {
                    m_page = Page::Disk;
                } else if (e.id == kPageMusik) {
                    m_page = Page::Musik;
                } else if (e.id == kPageAnno) {
                    m_page = Page::Anno;
                } else if (e.id == kMission) {
                    model.mission_window = !model.mission_window;
                    out.push_back(OptionsEvent{OptionsEvent::Kind::MissionWindow});
                } else if (e.id == kWindowRow || e.id == kFullscreenRow) {
                    model.fullscreen = e.id == kFullscreenRow;
                    out.push_back(OptionsEvent{OptionsEvent::Kind::Fullscreen, model.fullscreen ? 1U : 0U});
                } else if (e.id == kDisplayRow || e.id == kMonitor) {
                    out.push_back(OptionsEvent{OptionsEvent::Kind::Display});
                }
                break;
            case Page::Disk:
                if (e.id >= kSlotFrame && e.id < kSlotFrame + static_cast<std::int32_t>(m_slot_rows_shown)) {
                    // Choosing a slot starts typing its name (0x0043F7F4).
                    const std::size_t slot = m_slot_first + static_cast<std::size_t>(e.id - kSlotFrame);
                    if (slot >= model.slot_names.size()) {
                        break;
                    }
                    stop_editing();
                    m_renamed = false;
                    m_slot = slot;
                    if (slot < model.reserved_slots) {
                        break;  // lastgame: chosen to load, not named
                    }
                    m_name = model.slot_names[m_slot];
                    m_new_slot = m_name.empty();
                    m_editing = true;
                    m_renamed = true;
                } else if (e.id == m_slots_up && m_slot_first > 0) {
                    --m_slot_first;
                } else if (e.id == m_slots_down && m_slot_first + m_slot_rows_shown < model.slot_names.size()) {
                    ++m_slot_first;
                } else if (e.id == kSave && m_slot >= model.reserved_slots) {
                    const std::string name = m_renamed ? m_name : model.slot_names[m_slot];
                    stop_editing();
                    m_renamed = false;
                    out.push_back(OptionsEvent{OptionsEvent::Kind::Save, m_slot, name});
                } else if (e.id == kLoad) {
                    stop_editing();
                    m_renamed = false;  // the name typed is not kept without saving (U-037)
                    out.push_back(OptionsEvent{OptionsEvent::Kind::Load, m_slot});
                } else if (e.id == kQuit) {
                    stop_editing();
                    out.push_back(OptionsEvent{OptionsEvent::Kind::Quit});
                }
                break;
            case Page::Musik:
                if (e.id == kListDown && m_list_first + kTrackRows < model.tracks.size()) {
                    ++m_list_first;
                } else if (e.id == kListUp && m_list_first > 0) {
                    --m_list_first;
                } else if (e.id == kMusicLess || e.id == kMusicMore) {
                    a.music_volume =
                        std::clamp(a.music_volume + (e.id == kMusicMore ? kVolumeStep : -kVolumeStep), kMinVolume, 0);
                    audio_changed();
                } else if (e.id == kEffectsLess || e.id == kEffectsMore) {
                    a.effect_volume = std::clamp(a.effect_volume + (e.id == kEffectsMore ? kVolumeStep : -kVolumeStep),
                                                 kMinVolume, 0);
                    audio_changed();
                } else if (e.id == kSequential || e.id == kRandom) {
                    a.random_music = e.id == kRandom;
                    audio_changed();
                } else if (e.id == kTest) {
                    out.push_back(OptionsEvent{OptionsEvent::Kind::TestEffects});
                } else if (e.id == kMusicSwitch) {
                    a.music = !a.music;
                    audio_changed();
                } else if (e.id == kEffectsSwitch) {
                    a.effects = !a.effects;
                    audio_changed();
                } else if (e.id >= kTrackBox && e.id < kTrackBox + static_cast<std::int32_t>(kTrackRows)) {
                    const std::size_t t = m_list_first + static_cast<std::size_t>(e.id - kTrackBox);
                    if (t < model.tracks.size()) {
                        set_track_enabled(a, model.tracks[t], !track_enabled(a, model.tracks[t]));
                        audio_changed();
                    }
                } else if (e.id >= kTrackFrame && e.id < kTrackFrame + static_cast<std::int32_t>(kTrackRows)) {
                    const std::size_t t = m_list_first + static_cast<std::size_t>(e.id - kTrackFrame);
                    if (t < model.tracks.size()) {
                        set_track_enabled(a, model.tracks[t], true);
                        audio_changed();
                        out.push_back(OptionsEvent{OptionsEvent::Kind::PlayTrack, 0, model.tracks[t]});
                    }
                }
                break;
            case Page::Anno:
                if (e.id == kSpeechSwitch) {
                    a.speech[0] = !a.speech[0];
                    audio_changed();
                } else if (e.id >= kSpeechBox && e.id < kSpeechBox + static_cast<std::int32_t>(kSpeechRows) &&
                           a.speech[0]) {
                    const auto k = static_cast<std::size_t>(e.id - kSpeechBox + 1);
                    a.speech[k] = !a.speech[k];
                    audio_changed();
                } else if (e.id == kVideoSwitch) {
                    a.video[0] = !a.video[0];
                    audio_changed();
                } else if (e.id == kVideoBox && a.video[0]) {
                    const bool on = !(a.video[1] || a.video[2]);
                    a.video[1] = on;  // kinds 1 and 2 share the box
                    a.video[2] = on;
                    audio_changed();
                } else if ((e.id == kVideoBox + 1 || e.id == kVideoBox + 2) && a.video[0]) {
                    const std::size_t k = e.id == kVideoBox + 1 ? 5 : 7;
                    a.video[k] = !a.video[k];
                    audio_changed();
                }
                break;
        }
    }
    if (m_volume_unsaved && m_musik.held() != kMusicSlider && m_musik.held() != kEffectsSlider) {
        m_volume_unsaved = false;
        audio_changed();  // let go: now it is saved
    }
    return out;
}

bool OptionsPanel::wheel(std::int32_t notches, const OptionsModel& model) {
    const auto scroll = [notches](std::size_t& first, std::size_t count, std::size_t rows) {
        first = ui::wheel_scroll(first, count, rows, notches);
    };
    switch (m_page) {
        case Page::Disk:
            scroll(m_slot_first, model.slot_names.size(), m_slot_rows_shown);
            return true;
        case Page::Musik:
            scroll(m_list_first, model.tracks.size(), kTrackRows);
            return true;
        case Page::Root:
        case Page::Anno:
            break;
    }
    return false;
}

std::optional<std::string> OptionsPanel::help(const std::vector<std::string>& leiste, const OptionsModel& model) {
    const auto id = panel().hovered();
    if (!id) {
        return std::nullopt;
    }
    std::optional<std::size_t> line;
    const audio::AudioSettings& a = model.audio;
    switch (m_page) {
        case Page::Root:
            if (*id == kPageDisk) {
                line = 177;
            } else if (*id == kPageMusik) {
                line = 178;
            } else if (*id == kPageAnno) {
                line = 179;
            } else if (*id == kMission) {
                line = 181;
            } else if (*id == kWindowRow || *id == kFullscreenRow) {
                return ui::tr("display.mode");  // the original's line 180 names its resolutions
            } else if (*id == kDisplayRow || *id == kMonitor) {
                return ui::tr("display.title");
            }
            break;
        case Page::Disk:
            if (*id == kSave) {
                line = 173;
            } else if (*id == kLoad) {
                line = 172;
            } else if (*id == kQuit) {
                line = 174;
            } else if (*id >= kSlotHumans && *id < kSlotHumans + 12) {
                line = 176;
            } else if (*id >= kSlotFrame && *id < kSlotFrame + 12) {
                const std::size_t i = m_slot_first + static_cast<std::size_t>(*id - kSlotFrame);
                if (i < model.slot_details.size() && !model.slot_details[i].empty()) {
                    return model.slot_details[i];  // Open1602: what the savegame holds
                }
                line = 175;
            } else if (*id == m_slots_up || *id == m_slots_down) {
                line = 182;  // the music list's "scroll the list"
            }
            break;
        case Page::Musik:
            if (*id == kListDown || *id == kListUp) {
                line = 182;
            } else if (*id >= kMusicLess && *id <= kEffectsSlider) {
                line = 183 + static_cast<std::size_t>(*id - kMusicLess);
            } else if (*id == kSequential) {
                line = 189;
            } else if (*id == kRandom) {
                line = 190;
            } else if (*id == kTest) {
                line = 191;
            } else if (*id == kMusicSwitch) {
                line = a.music ? 192 : 193;
            } else if (*id == kEffectsSwitch) {
                line = a.effects ? 194 : 195;
            } else if (*id >= kTrackBox && *id < kTrackBox + static_cast<std::int32_t>(kTrackRows)) {
                const std::size_t t = m_list_first + static_cast<std::size_t>(*id - kTrackBox);
                line = t < model.tracks.size() && track_enabled(a, model.tracks[t]) ? 197 : 198;
            } else if (*id >= kTrackFrame && *id < kTrackFrame + static_cast<std::int32_t>(kTrackRows)) {
                line = 196;
            }
            break;
        case Page::Anno:
            if (*id == kSpeechSwitch) {
                line = a.speech[0] ? 236 : 237;
            } else if (*id >= kSpeechBox && *id < kSpeechBox + static_cast<std::int32_t>(kSpeechRows)) {
                line = a.speech[static_cast<std::size_t>(*id - kSpeechBox + 1)] ? 234 : 235;
            } else if (*id == kVideoSwitch) {
                line = a.video[0] ? 230 : 231;
            } else if (*id >= kVideoBox && *id < kVideoBox + 3) {
                line = m_anno.selected(*id) ? 228 : 229;
            }
            break;
    }
    if (!line || *line >= leiste.size()) {
        return std::nullopt;
    }
    return leiste[*line];
}

void OptionsPanel::type(std::string_view text) {
    if (!m_editing) {
        return;
    }
    constexpr std::size_t kMaxName = 18;  // the original's limit (0x00409910 with 0x13)
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && m_name.size() < kMaxName) {
            m_name.push_back(c);
        }
    }
}

std::optional<OptionsEvent> OptionsPanel::key(int key) {
    if (!m_editing) {
        return std::nullopt;
    }
    if (key == 8 && !m_name.empty()) {
        m_name.pop_back();
    } else if (key == 13) {
        m_editing = false;
        if (m_new_slot && !m_name.empty()) {
            // Open1602: a new savegame is saved with its name; a renamed one waits for "Sichern".
            m_renamed = false;
            return OptionsEvent{OptionsEvent::Kind::Save, m_slot, m_name};
        }
    } else if (key == 27) {
        m_editing = false;  // the name typed is dropped
        m_renamed = false;
    }
    return std::nullopt;
}

}  // namespace o1602::app
