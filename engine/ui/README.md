# engine/ui

The user interface without platform dependencies. Depends on `sim`, `assets` and `core`; the
game draws its draw lists through `o1602::render_sdl`.

| Header | Contents |
|---|---|
| `font.hpp` | Fonts (`.ZEI`) and text rules of the original |
| `panel.hpp` | The original's gadget layouts in operation: screen areas, drawing per state, mouse with toggles, groups, sliders |
| `resources.hpp` | The interface's sprite sheets and fonts per screen size |
| `build_menu.hpp` | The build menu: categories, pop-ups, availability and costs |
| `form.hpp` | Open1602's own windows without a layout in the original (the multiplayer menu) |
| `unit_selection.hpp` | Selected ships and units, groups, the orders a click gives |
| `key_bindings.hpp` | Freely assignable keys (`keys.cfg`, [ADR 0010](../../docs/architecture/0010-controls-and-interface.md)) |
| `localization.hpp` | Open1602's own texts in the installation's language (`data/locale/*.lang`, embedded) |
| `text_format.hpp` | Safe filling of the printf patterns of `Text.cod` |

The layouts are read in `engine/assets` (`gad.hpp`). Description:
[docs/formats/gad.md](../../docs/formats/gad.md).
