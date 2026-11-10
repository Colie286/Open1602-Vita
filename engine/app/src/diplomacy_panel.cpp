// SPDX-License-Identifier: GPL-3.0-or-later
//
// The diplomacy window (VERTRAG.GAD with its tribute page TRIBUT.GAD; Diplo_Refresh 0x00433C00,
// Tribut_Refresh 0x00433E70, clicks in 0x0043CE00; docs/re/ui.md): opened from the player status or
// with V. One row per other human or computer player: trade and peace treaty (offer, withdraw,
// accept, break) and, for computers, a hand that shows the mood; the hand opens the tribute page.
//
// VERTRAG.GAD has rows for three partners. With more (Open1602, up to eleven) the window is the
// strip of diplomacy_strip.hpp: the partners' coats of arms on top, the chosen one's treaties, hand
// and tribute below, all on one page.
#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "banner_graphic.hpp"
#include "diplomacy_strip.hpp"
#include "panel_common.hpp"

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr std::uint32_t kMaxTribute = sim::DiplomacyOrder::kMaxTribute;
constexpr std::uint32_t kTributeStep = 100;

/// The next value of a treaty button (0x0043FC83): none -> offer, offered or signed -> none
/// (withdraw, break), received -> accept.
sim::Treaty next_step(sim::Treaty now) {
    switch (now) {
        case sim::Treaty::None:
            return sim::Treaty::Offered;
        case sim::Treaty::Received:
            return sim::Treaty::Signed;
        case sim::Treaty::Offered:
        case sim::Treaty::Signed:
            break;
    }
    return sim::Treaty::None;
}

/// The hand of the mood: 0 thumb up (over 90) .. 6 thumb down (-90 or less).
std::int32_t mood_pose(std::int32_t mood) {
    constexpr std::array<std::int32_t, 6> kLimits = {90, 55, 20, -20, -55, -90};
    std::int32_t pose = 0;
    for (const std::int32_t limit : kLimits) {
        if (mood <= limit) {
            ++pose;
        }
    }
    return pose;
}

class DiplomacyPanel final : public SelectionPanel {
public:
    DiplomacyPanel(assets::GadgetLayout vertrag, assets::GadgetLayout tribut)
        : m_vertrag(std::move(vertrag), 1.0), m_tribut(tribut, 1.0), m_strip(strip_layout(std::move(tribut)), 1.0) {
        // The code adds the state to the sprites the layout gives (0x0042C2D5).
        const assets::GadgetLayout& l = m_vertrag.layout();
        const auto gfx = [&](std::int32_t id, std::int32_t fallback) {
            const auto i = l.index_of(id);
            return i ? l.gadgets[*i].gfx : fallback;
        };
        m_shield = gfx(kShield, 548);
        m_trade = gfx(kTrade, 563);
        m_peace = gfx(kPeace, 571);
        m_hand = gfx(kHand, 579);
        for (std::size_t r = 0; r < diplomacy::kOriginalRows; ++r) {
            m_rows = r + 1;
            if (!l.index_of(kName + static_cast<std::int32_t>(r))) {
                m_rows = r;
                break;
            }
        }
    }

    void set_scale(double scale) override {
        m_vertrag.set_scale(scale);
        m_tribut.set_scale(scale);
        m_strip.set_scale(scale);
    }
    ui::Panel& panel() override {
        if (m_strip_shown) {
            return m_strip;
        }
        return m_tribute ? m_tribut : m_vertrag;
    }

    void opened() override {
        m_tribute = false;
        m_amount = 0;
    }
    bool back() override {
        if (m_tribute && !m_strip_shown) {
            m_tribute = false;  // right mouse / Escape: back to the treaties (0x0040A0D6)
            return true;
        }
        return false;
    }

    void refresh(const Selection& selection, const PanelContext& c) override {
        const std::vector<sim::PlayerId> partners = partners_of(c);
        m_strip_shown = diplomacy::uses_strip(partners.size(), m_rows);
        if (m_strip_shown) {
            refresh_strip(selection, partners, c);
            return;
        }
        if (m_tribute && std::find(partners.begin(), partners.end(), m_partner) == partners.end()) {
            m_tribute = false;
        }
        if (m_tribute) {
            refresh_tribute(m_tribut, c);
            return;
        }
        for (std::size_t r = 0; r < m_rows; ++r) {
            const auto row = static_cast<std::int32_t>(r);
            const bool shown = r < partners.size();
            for (const std::int32_t id : {kBoard, kName, kShieldShadow, kShield, kTrade, kPeace, kHand}) {
                m_vertrag.set_visible(id + row, shown);
            }
            for (const std::int32_t id : {kTrade, kPeace, kHand}) {
                m_vertrag.set_no_select(id + row, !shown);
            }
            if (shown) {
                fill(m_vertrag, partners[r], row, c, kName, kShield, kShieldShadow, kTrade, kPeace);
                const sim::Player* p = c.world().player(partners[r]);
                if (p != nullptr && p->role == sim::PlayerRole::Computer) {
                    m_vertrag.set_graphic(kHand + row, m_hand + 2 * pose(c, partners[r], 0), 1);
                } else {
                    m_vertrag.set_graphic(kHand + row, m_hand + 16, 1);  // a human: the coin
                }
            }
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& /*selection*/, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        if (m_strip_shown) {
            const std::int32_t k = e.id - kStripFrame;
            if (e.kind == ui::PanelEvent::Kind::Clicked && k >= 0 &&
                k < static_cast<std::int32_t>(diplomacy::kMaxPartners)) {
                const std::vector<sim::PlayerId> partners = partners_of(c);
                if (static_cast<std::size_t>(k) < partners.size() &&
                    partners[static_cast<std::size_t>(k)] != m_partner) {
                    m_partner = partners[static_cast<std::size_t>(k)];
                    m_amount = 0;
                }
                return;
            }
            clicked_tribute(e, c, out);
            return;
        }
        if (m_tribute) {
            clicked_tribute(e, c, out);
            return;
        }
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const std::vector<sim::PlayerId> partners = partners_of(c);
        for (const std::int32_t base : {kTrade, kPeace, kHand}) {
            const std::int32_t r = e.id - base;
            if (r < 0 || r >= static_cast<std::int32_t>(m_rows) || static_cast<std::size_t>(r) >= partners.size()) {
                continue;
            }
            const sim::PlayerId b = partners[static_cast<std::size_t>(r)];
            if (base == kHand) {
                // The tribute page for this partner; the amount starts at 0 for every partner
                // (the original keeps it, U-034).
                m_tribute = true;
                m_partner = b;
                m_amount = 0;
            } else {
                out.emplace_back(treaty_order(c, b, base == kTrade));
            }
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& /*selection*/,
                                                  const PanelContext& c) const override {
        const std::vector<sim::PlayerId> partners = partners_of(c);
        const auto state = [&](sim::PlayerId b, bool trade) {
            const sim::Diplomacy& d = c.world().diplomacy();
            if (c.local >= d.size() || b >= d.size()) {
                return std::size_t{0};
            }
            const sim::Relation& rel = d.relation(c.local, b);
            return static_cast<std::size_t>(trade ? rel.trade : rel.peace);
        };
        // 199..202 trade, 203..206 peace by the state: none, received, offered, signed.
        const auto treaty_line = [&](sim::PlayerId b, bool trade) {
            constexpr std::array<std::size_t, 4> kOrder = {0, 2, 1, 3};
            return panel_detail::help_line(c, (trade ? 199 : 203) + kOrder[std::min<std::size_t>(state(b, trade), 3)]);
        };
        if (m_strip_shown) {
            const std::int32_t k = id - kStripFrame;
            if (k >= 0 && static_cast<std::size_t>(k) < partners.size()) {
                const sim::Player* p = c.world().player(partners[static_cast<std::size_t>(k)]);
                // Open1602: with whom the partner is at war, which the original's window does not show.
                return p != nullptr ? ui::tr("diplomacy.partner", {player_name(*p)}) + " - " +
                                          diplomacy::wars_text(c.world(), partners[static_cast<std::size_t>(k)])
                                    : std::string();
            }
        }
        if (m_tribute || m_strip_shown) {
            switch (id) {
                case kTributeTrade:
                    return treaty_line(m_partner, true);
                case kTributePeace:
                    return treaty_line(m_partner, false);
                case kLess:
                    return panel_detail::help_line(c, 209);
                case kMore:
                    return panel_detail::help_line(c, 210);
                case kSlider:
                    return panel_detail::help_line(c, 211);
                case kPay:
                    return panel_detail::help_line(c, 212);
                case kTributeHand:
                    return panel_detail::help_line(c, 213);
                case kTributeShield:
                    return diplomacy::wars_text(c.world(), m_partner);
                default:
                    return std::nullopt;
            }
        }
        for (const std::int32_t base : {kTrade, kPeace, kHand, kShield}) {
            const std::int32_t r = id - base;
            if (r < 0 || static_cast<std::size_t>(r) >= std::min(partners.size(), m_rows)) {
                continue;
            }
            const sim::PlayerId b = partners[static_cast<std::size_t>(r)];
            if (base == kHand) {
                return panel_detail::help_line(c, 207);
            }
            if (base == kShield) {
                return diplomacy::wars_text(c.world(), b);  // Open1602: the partner's wars
            }
            return treaty_line(b, base == kTrade);
        }
        return std::nullopt;
    }

private:
    // VERTRAG, + row
    static constexpr std::int32_t kBoard = 56001;
    static constexpr std::int32_t kName = 56011;
    static constexpr std::int32_t kShieldShadow = 56021;
    static constexpr std::int32_t kShield = 56031;
    static constexpr std::int32_t kTrade = 56041;
    static constexpr std::int32_t kPeace = 56051;
    static constexpr std::int32_t kHand = 56061;
    // TRIBUT
    static constexpr std::int32_t kTributeBoard = 64001;
    static constexpr std::int32_t kTributeName = 64002;
    static constexpr std::int32_t kTributeShieldShadow = 64003;
    static constexpr std::int32_t kTributeShield = 64004;
    static constexpr std::int32_t kTributeTrade = 64005;
    static constexpr std::int32_t kTributePeace = 64006;
    static constexpr std::int32_t kTributeHand = 64007;
    static constexpr std::int32_t kPay = 64008;
    static constexpr std::int32_t kPayLabel = 64009;
    static constexpr std::int32_t kAmount = 64010;
    static constexpr std::int32_t kLess = 64022;
    static constexpr std::int32_t kMore = 64023;
    static constexpr std::int32_t kSlider = 64024;
    // The strip (Open1602), added to a copy of TRIBUT: + place 0..10
    static constexpr std::int32_t kStripArms = 64101;
    static constexpr std::int32_t kStripFrame = 64121;
    /// PIRATORD.GAD's frame to choose a player (GFXPIRAT + 11), golden 4 further on.
    static constexpr std::int32_t kFrameOffset = 11;
    static constexpr std::int32_t kFramePress = 4;
    /// An unanswered offer: the partner's frame flashes golden at this pace.
    static constexpr std::uint64_t kOfferBlinkMs = 500;

    /// TRIBUT.GAD with the treaties moved down and the strip's coats of arms and frames above them.
    static assets::GadgetLayout strip_layout(assets::GadgetLayout layout) {
        const std::int32_t frame_gfx = layout.constant("GFXPIRAT").value_or(543) + kFrameOffset;
        const assets::Gadget* shield = layout.find(kTributeShield);
        const std::int32_t arms_gfx = shield != nullptr ? shield->gfx : 548;
        for (assets::Gadget& g : layout.gadgets) {
            if (g.id >= kTributeBoard && g.id <= kTributeHand) {
                g.pos[1] += diplomacy::kTreatiesShift;
            }
        }
        for (std::size_t k = 0; k < diplomacy::kMaxPartners; ++k) {
            const auto [x, y] = diplomacy::strip_slot(k, diplomacy::kMaxPartners);
            assets::Gadget arms;
            arms.id = kStripArms + static_cast<std::int32_t>(k);
            arms.kind = assets::GadgetKind::Graphic;
            arms.no_select = true;
            arms.gfx = arms_gfx;
            arms.pos = {x + diplomacy::kArmsInset, y + diplomacy::kArmsInset};
            arms.size = {32, 40};
            layout.gadgets.push_back(arms);
            // The frame after the coat of arms: it is drawn over it and takes the clicks.
            assets::Gadget frame;
            frame.id = kStripFrame + static_cast<std::int32_t>(k);
            frame.kind = assets::GadgetKind::Graphic;
            frame.gfx = frame_gfx;
            frame.press_offset = kFramePress;
            frame.pos = {x, y};
            frame.size = {diplomacy::kFrameWidth, diplomacy::kFrameHeight};
            layout.gadgets.push_back(frame);
        }
        return layout;
    }

    /// The other humans and computers, by player number (0x00433C1C).
    static std::vector<sim::PlayerId> partners_of(const PanelContext& c) {
        return diplomacy::partners(c.world(), c.local);
    }

    /// The strip: the coats of arms of all partners, the chosen one's treaties and tribute below.
    void refresh_strip(const Selection& selection, const std::vector<sim::PlayerId>& partners, const PanelContext& c) {
        m_tribute = false;
        // The window opened from an offer's icon starts with that partner.
        if (selection.serial != m_serial) {
            m_serial = selection.serial;
            if (std::find(partners.begin(), partners.end(), selection.partner) != partners.end()) {
                m_partner = selection.partner;
                m_amount = 0;
            }
        }
        if (std::find(partners.begin(), partners.end(), m_partner) == partners.end()) {
            m_partner = partners.front();
            m_amount = 0;
        }
        const std::vector<diplomacy::Offer> offers = diplomacy::open_offers(c.world(), c.local);
        const bool flash = (c.now_ms / kOfferBlinkMs) % 2 == 0;
        const render::BannerArt arms = render::BannerArt::at(m_shield);
        for (std::size_t k = 0; k < diplomacy::kMaxPartners; ++k) {
            const auto slot = static_cast<std::int32_t>(k);
            const bool shown = k < partners.size();
            m_strip.set_visible(kStripArms + slot, shown);
            m_strip.set_visible(kStripFrame + slot, shown);
            if (!shown) {
                continue;
            }
            const sim::PlayerId b = partners[k];
            const auto [x, y] = diplomacy::strip_slot(k, partners.size());
            m_strip.set_position(kStripFrame + slot, x, y);
            m_strip.set_position(kStripArms + slot, x + diplomacy::kArmsInset, y + diplomacy::kArmsInset);
            set_banner_graphic(m_strip, kStripArms + slot, arms, c.banner(b));
            const bool offer =
                std::any_of(offers.begin(), offers.end(), [&](const diplomacy::Offer& o) { return o.from == b; });
            m_strip.set_selected(kStripFrame + slot, b == m_partner || (offer && flash));
        }
        refresh_tribute(m_strip, c);
    }

    /// How partner b feels about the local player, with `gold` of tribute being offered.
    static std::int32_t pose(const PanelContext& c, sim::PlayerId b, std::uint32_t gold) {
        const sim::Diplomacy& d = c.world().diplomacy();
        if (c.local >= d.size() || b >= d.size()) {
            return 3;
        }
        return mood_pose(d.mood(c.world(), b, c.local, gold * 32 / 17, c.world().time_ms()));
    }

    /// Name, shield and the two treaties of partner b.
    void fill(ui::Panel& p, sim::PlayerId b, std::int32_t row, const PanelContext& c, std::int32_t name,
              std::int32_t shield, std::int32_t shadow, std::int32_t trade, std::int32_t peace) const {
        const sim::Player* player = c.world().player(b);
        if (player == nullptr) {
            return;
        }
        p.set_text(name + row, player_name(*player));
        const render::BannerArt arms = render::BannerArt::at(m_shield);
        set_banner_graphic(p, shield + row, arms, c.banner(b));
        set_banner_graphic(p, shadow + row, arms, c.banner(b));
        const sim::Diplomacy& d = c.world().diplomacy();
        if (c.local < d.size() && b < d.size()) {
            const sim::Relation& rel = d.relation(c.local, b);
            p.set_graphic(trade + row, m_trade + static_cast<std::int32_t>(rel.trade), 4);
            p.set_graphic(peace + row, m_peace + static_cast<std::int32_t>(rel.peace), 4);
        }
    }

    sim::DiplomacyOrder treaty_order(const PanelContext& c, sim::PlayerId b, bool trade) const {
        sim::DiplomacyOrder o;
        o.player = c.local;
        o.partner = b;
        o.kind = trade ? sim::DiplomacyOrder::Kind::Trade : sim::DiplomacyOrder::Kind::Peace;
        const sim::Diplomacy& d = c.world().diplomacy();
        const sim::Treaty now = c.local < d.size() && b < d.size()
                                    ? (trade ? d.relation(c.local, b).trade : d.relation(c.local, b).peace)
                                    : sim::Treaty::None;
        o.value = next_step(now);
        return o;
    }

    /// The tribute page, or the same gadgets below the strip.
    void refresh_tribute(ui::Panel& page, const PanelContext& c) {
        fill(page, m_partner, 0, c, kTributeName, kTributeShield, kTributeShieldShadow, kTributeTrade, kTributePeace);
        const sim::Player* p = c.world().player(m_partner);
        const bool computer = p != nullptr && p->role == sim::PlayerRole::Computer;
        page.set_visible(kTributeHand, computer);
        if (computer) {
            // The hand shows the mood after paying (the same scaling as the payment).
            page.set_graphic(kTributeHand, m_hand + 2 * pose(c, m_partner, m_amount), 0);
        }
        page.set_text(kPayLabel, c.text("GAME", 78));
        page.set_text(kAmount, std::to_string(m_amount));
        if (page.held() != kSlider) {
            page.set_slider(kSlider, 0, static_cast<std::int32_t>(m_amount), static_cast<std::int32_t>(kMaxTribute));
        }
        // Open1602: nothing to pay, or not enough money - the button says so instead of doing
        // nothing (U-034).
        const sim::Player* local = c.world().player(c.local);
        page.set_disabled(kPay,
                          m_amount == 0 || local == nullptr || local->money < static_cast<std::int64_t>(m_amount));
    }

    void clicked_tribute(const ui::PanelEvent& e, const PanelContext& c, std::vector<sim::Command>& out) {
        if (e.kind == ui::PanelEvent::Kind::Slid && e.id == kSlider) {
            const auto v = static_cast<std::uint32_t>(std::max(0, panel().slider(kSlider, 0, kMaxTribute)));
            m_amount = std::min(kMaxTribute, (v + kTributeStep / 2) / kTributeStep * kTributeStep);
            return;
        }
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        switch (e.id) {
            case kTributeTrade:
            case kTributePeace:
                out.emplace_back(treaty_order(c, m_partner, e.id == kTributeTrade));
                break;
            case kLess:
                m_amount -= std::min(m_amount, kTributeStep);
                break;
            case kMore:
                m_amount = std::min(kMaxTribute, m_amount + kTributeStep);
                break;
            case kPay: {
                const sim::Player* local = c.world().player(c.local);
                if (m_amount > 0 && local != nullptr && local->money >= static_cast<std::int64_t>(m_amount)) {
                    sim::DiplomacyOrder o;
                    o.player = c.local;
                    o.partner = m_partner;
                    o.kind = sim::DiplomacyOrder::Kind::Tribute;
                    o.gold = m_amount;
                    out.emplace_back(o);
                    m_amount = 0;
                }
                break;
            }
            default:
                break;
        }
    }

    ui::Panel m_vertrag;
    ui::Panel m_tribut;
    ui::Panel m_strip;
    bool m_strip_shown = false;  ///< more partners than VERTRAG has rows
    std::uint32_t m_serial = 0;  ///< the selection last seen (a new one may name a partner)
    std::size_t m_rows = diplomacy::kOriginalRows;
    std::int32_t m_shield = 548;
    std::int32_t m_trade = 563;
    std::int32_t m_peace = 571;
    std::int32_t m_hand = 579;
    bool m_tribute = false;
    sim::PlayerId m_partner = sim::kNoPlayer;
    std::uint32_t m_amount = 0;
};

}  // namespace

std::unique_ptr<SelectionPanel> make_diplomacy_panel(assets::GadgetLayout vertrag, assets::GadgetLayout tribut) {
    return std::make_unique<DiplomacyPanel>(std::move(vertrag), std::move(tribut));
}

}  // namespace o1602::app
