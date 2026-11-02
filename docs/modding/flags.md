# Player flags and colours

All player flags are **one** flag that the engine tints in the player's colour. A new
colour therefore needs no graphics: in the flag menu you choose one of the twelve preset colours
(`render::BannerTable::kDefaultColours`) or any RGB colour. This way the number of possible
players grows without new images ([ADR 0007](../architecture/0007-extensibility-and-modding.md), section 4).

**Status:** Done are the tinting (`render/flag_tint.hpp`), the twelve presets and the template from
the white original flag (`render::BannerTable`), custom images per banner (`FlagStyle::Sprites`, used for
the skull), units in player colour (`render/unit_tint.hpp`) and the interface's coats of arms and message
icons in player colour (`render/variant_tint.hpp`, [below](#interface-art-in-player-colour)) and the flag menu of the
endless game with the twelve presets ([below](#the-flag-menu)). Still open: the free RGB colour (interface) and custom
flags as PNG (content packages, milestone M8).

## How tinting works

Every pixel of the flag template with **R = G = B** (pure grey) belongs to the cloth and takes on the
player colour. Its grey value is the shading:

| Grey value | Result |
|---|---|
| 128 | exactly the player colour |
| 0 … 127 | darker, linearly down to black: `colour · grey / 128` |
| 129 … 255 | lighter, linearly up to white: `colour + (255 − colour) · (grey − 128) / 127` |

All other pixels – coloured or transparent – stay unchanged. This lets you draw the mast, border,
coat of arms or lettering in fixed colours.

## Drawing your own flag

- **Images:** 8 phases of waving, one set per zoom level (full size like `gfx/ship.bsh` 216–223,
  89 × 30 pixels; half and quarter size for the other levels). The phases run at 90 ms per frame.
- **Anchor point:** The engine centres the image horizontally above the mast. On buildings the
  bottom edge of the image lies at the height `Fahnoffs[2]` of the flag figure; on ships the top edge hangs
  down from the masthead §6).
  In the original images the cloth starts just right of centre (column 46 of 89, row 6); a
  custom flag should have the same attachment point, otherwise it floats next to the mast.
- **Cloth in greys:** base tone 128, shadows below, highlights above. Draw with a
  grey gradient instead of a fixed colour, and the flag will look right in every player colour.
- **Fixed parts in colour:** A coat of arms that should not be tinted must not contain pure grey.
  For grey fixed parts (e.g. a silver border) a minimal deviation is enough, such as
  (120, 120, 121).
- **Format:** PNG with an alpha channel; pixels with alpha 0 are transparent. Semi-transparency is not
  supported, so that the flag matches the pixel style of the game.

A mod can also supply its own finished, pre-coloured images for individual banners (`FlagStyle::Sprites`)
if a motif should not be tintable – this is how the pirates keep their skull.

## Graphics of packages in player colour

Every graphic of a package that shows a player's colour follows the same rule as the flag: it is a
**template**, drawn once, whose pure grey pixels take on the colour of the owner's banner with the
shading of the table above, and whose other pixels stay as drawn. One template is enough for any
number of players, up to 12, and for colours chosen freely in the flag menu.

| Graphic | How | State |
|---|---|---|
| flags | `FlagTemplate` of a banner table, see above | done |
| soldiers of a package | `tint = true` in `own_figures.toml` ([own-figures.md](own-figures.md#soldiers)) | done |
| ships of a package | `tint = true` for a ship of a package's own ([own-figures.md](own-figures.md#ships)), in the owner's colour | done |
| coats of arms and message icons of a package | `tint = true` in `icons.toml` ([graphics.md](graphics.md#icons)): a message's icon in the colour of the player it is about (`game.message(..., about)`), a soldier's icon in a castle in the owner's | done |

For the original's units Open1602 makes the template itself from the four colours drawn
(`render/unit_tint.hpp`), and does the same for a soldier of a package whose image holds its
sprites four times, red, blue, yellow and white: banners after the fourth wear a template made from
them. A soldier whose image holds them once without `tint` looks the same for every player.

## Interface art in player colour

The original draws some interface pictures once per player colour (red, blue, yellow, white): the
coats of arms in the info panel, the diplomacy window, the ship and city lists, the trade route, the
pirates' order and the end screen, the small coats of arms of the city list, and the message icons
in the status bar. Banners with one of these colours show the drawn picture
(`render::Banner::unit_variant`); every other banner (players five to twelve, mods' banners) gets
the white picture recoloured at runtime, with the pattern of one of the four original coats of
arms (`render::Banner::art_pattern`: red's cross, blue's quarters, yellow's stripes or white's
plain field). `add_colour` picks the pattern from the banner's id (FNV-1a, like the riders' horse),
so the further players do not all look alike and every computer of a multiplayer game shows the
same.

Which pixels take the colour is found by comparing the four variants: a pixel is cloth where all
other variants have another colour, the white one is grey (channels at most 47 apart) and not
darker than luminance 64, and at least three of its eight neighbours are cloth too (the variants'
hand-dithered shadows differ in single pixels). Cloth gets the same shading as the flag template
(0.48 · brightness + 5), so a banner's coat of arms and its flag have the same colour. The
brightness of each cloth pixel comes from the pattern variant, scaled so that its cloth is as
bright on average as the white one's (the yellow arms' black stripes stay dark). The motif of a
message icon (lips, sword, scroll, exclamation mark), outlines and the selection frame stay as
drawn.

A family of pictures is described by `render::BannerArt`: its first sprite, the distance between the
variants, how many there are and which one is recoloured, plus sprites of a banner's own (the
pirates' skull among the military message icons). The windows set a gadget's picture with
`set_banner_graphic` (`engine/app/src/banner_graphic.hpp`); the interface renderer recolours and
caches it per colour.

## The flag menu

The single player's banner screen (`FARBWAHL.GAD`) keeps the original's four hall banners and adds, for the endless
game only, eight coats of arms for the further presets: four left and four right of the name board, drawn from the
end screen's coat of arms (`GFXFARB + 25`, gold frame `+ 4` while the mouse is over it) and tinted like every other
interface art. Over a coat of arms the screen writes the colour's name below it (`net.colour.*`) and repaints the hall
banner with the same pattern ([`art_pattern`](#interface-art-in-player-colour)) in that colour, so the hall shows the
banner one would sail under. A click chooses it as a click on a hall banner does. Each time the screen is entered from
the mission screen, the eight coats of arms draw their patterns anew (`BannerTable::shuffle_art_patterns`), each side
of four showing all four patterns; the game's banners keep the patterns `add_colour` gave them.

The hall banners are painted, not flat flags, so they are recoloured by `render::tint_shape`
(`render/shape_tint.hpp`): the cloth is every opaque pixel of the banner between the left and right edge of its
clickable outline on the same row (`ColourTint::shape` names the outline's sprite; gaps of up to two rows in the
outline take what the rows around them have in common), and its shading is kept as the
brightness relative to the cloth's upper quartile. Pole, rope and the hall around it stay as they are.

A further colour the level's file does not have is taken by swapping banners with the player who had it
(`sim::take_banner`); that computer is renamed after its new colour. Messages that name one of the original's four
colours (the „… Spieler konnte seinen Auftrag erfüllen!“ family, MELDUNG 0x6B–0x6E) are spoken only for those four. One's
own victory with a further colour plays the colourless „Ihr konntet den Euch gestellten Auftrag erfüllen!“ (0x54); a
rival's shows the same words with its colour in the status bar, under the winner's icon and without a voice: the
catalogue's `message.won.<colour>` (so each language declines the colour itself), for a banner without one
`message.won` with the player's name. Such a text of Open1602's own travels with the message (`Announcement::text`)
and replaces the original's MELDUNG text in the status bar.

## The original flags

The original has separate, hand-dithered images for red, blue, yellow and white (`ship.bsh`
192–223), plus the pirates' skull (224–231). Open1602 makes the template from the white flag:
every pixel gets the grey value 0.48 · brightness + 5. The factors and the base colours of the four
banners (red 216/51/8, blue 0/0/241, yellow 255/219/57, white 255/255/255) are fitted to the original
images; the mean deviation is about 15 of 255 per colour channel, because the originals are dithered with
256 colours. Anyone who wants the original images switches them on with `--original-flags`
(`render::BannerOptions::original_player_flags`).

## Choosing colours

The twelve presets are easy to tell apart on land, on water and on the minimap: red, blue, yellow,
white (as in the original), green, orange, purple, turquoise, pink, magenta, brown, dark grey. Black is missing
because the pirates fly black and very dark flags are hard to see on the roofs. Later, the
menu is meant to warn about free RGB colours that are too similar or too dark.
