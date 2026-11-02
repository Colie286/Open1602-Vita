# 0005 – Module structure and dependency rules

Status: accepted · 2026-10-16

```
            core            Files, errors (Result), bytes, text, compression
              ▲
            assets          Loaders for original formats (no platform/graphics dependency)
              ▲
             sim            Deterministic simulation (world, islands, economy, AI, random numbers …)
              ▲
   net, render, audio, ui   Lockstep multiplayer, draw lists, sound, interface (each on sim)
              ▲
   video (on audio)         Smacker films
   editor (on sim, assets)  Scenario documents: editing steps with undo and redo, checks
   menus (on audio, ui,     The game's parts that need no platform: menus, settings files, build
          net, render)      tool, command sink, event presenter, video queue, map gestures
                            (sources in engine/app/src, tested without SDL)
   platform (on core, SDL3) Window, renderer, audio output
   render_sdl (on render,   Draws sprites and panels with SDL3
          ui, platform)
              ▲
             app            The game executable, links all of them
```

The exact links of every target are listed at the top of `engine/CMakeLists.txt`. Only the game build
(`O1602_BUILD_GAME`) adds `platform`, `render_sdl` and `app`.

Rules:
- Arrows point to dependencies; cycles are forbidden.
- `sim` **never** depends on `platform`, `render`, `audio`, `ui` (headless capability, determinism).
- A module includes only headers of modules it links (directly or through them), and only
  `platform`, `render_sdl` and `app` include SDL. The ctest `layering` (`cmake/CheckLayering.cmake`)
  checks this on the sources (for `menus` on the sources `engine/app/menus/CMakeLists.txt` lists), since the include directories of all modules are visible to the tests
  and tools through `PUBLIC` links and a stray include would otherwise compile.
- Every module is its own CMake library `o1602::<module>` with `include/o1602/<module>/` as its
  public interface; internals live in `src/`.
- Namespace `o1602::<module>`.

All modules exist except `script` (Lua, M8). Each is described in `engine/<module>/README.md`.
