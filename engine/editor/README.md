# engine/editor

The logic of Open1602's scenario editor ([ADR 0013](../../docs/architecture/0013-editor.md)), without
any window: the editor's screen in the game and `o1602-tool scenario` both build on it, and every
operation is tested headless.

| Header | Contents |
|---|---|
| `document.hpp` | `Document`: the world being edited, opened from an Open1602 file or one of the original's, saved as an `.o1602` scenario; whether it changed since it was saved |
| `operation.hpp`, `history.hpp` | `Operation` (an edit that can be done and taken back) and `History` (undo and redo, bounded by memory) |
| `operations.hpp` | The operations: tiles of the islands, the scenario's description, its markers and its script |
| `original_check.hpp` | What keeps a scenario from the original's limits (`ScenarioInfo::original_limits`) |

Every change of a document goes through an operation, never straight into the world, so that it
can be taken back. The simulation never runs on a document.
