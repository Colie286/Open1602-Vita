# Development environment on Arch Linux

## 1. Build tools (engine)

```sh
sudo pacman -Syu --needed base-devel git cmake ninja gcc clang gdb sdl3 python python-pillow
```

zlib is deliberately not installed explicitly: on systems with `zlib-ng-compat` (e.g. CachyOS,
or when Heroic brings it along) that package provides `zlib.h` and `libz.so` and conflicts with `zlib`.
CMake finds both variants. Check: `pacman -Qo /usr/include/zlib.h`.

Build and test:

```sh
cd open1602            # the clone of https://github.com/x4rd4s/open1602
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# Tests additionally against your own installation:
export ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602"
ctest --preset debug
./build/debug/bin/o1602-tool islands "$ANNO1602_DIR/NORD/big.scp"
```

More presets: `release` (puts the programs into `bin/` of the clone), `asan` (AddressSanitizer + UBSan),
`tsan` (ThreadSanitizer), `ci` (warnings as errors), `coverage` (GCC line and branch coverage). Every
preset but `release` and `vcpkg` puts the programs into `build/<preset>/bin/`.

### The game `open1602` (SDL3)

The `release` preset builds it (other presets with `-DO1602_BUILD_GAME=ON`); it needs the `sdl3` package
(see above):

```sh
cmake --preset release
cmake --build --preset release
export ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602"
./bin/open1602                                  # the main menu
./bin/open1602 --scenario "Szenes/Endlosspiel00.szs" --zoom 1
./bin/open1602 --savegame Merlin01           # name from the game (Game.dat)
./bin/open1602 --load ~/.local/share/Open1602/saves/quicksave.o1602  # own savegame
./bin/o1602-tool savegames               # all savegames with names
```

Every option and the default keys: `open1602 --help` (any unknown option prints them). In the game, F1 shows the keys over the map;
they can be changed in `keys.cfg` in the user's data folder. The decisions behind the controls are in
[ADR 0010](architecture/0010-controls-and-interface.md). Note that Q and E rotate the view; Ctrl+Q quits.

The window title shows the game, the game clock, the speed or pause and, in a multiplayer game, the
number of players and the state of the session, in the player's language. With the environment
variable `O1602_DEBUG_TITLE` set, it also shows a developer readout: zoom and rotation, the tile under
the mouse with its island, city and owner, the selection, the frame rate and the number of sprites
drawn. With `--frames N --screenshot image.png` the program runs for N frames, saves the last one and
reports the frame rate (for tests without user input). Every frame of such a run advances the game by
the same 16 ms, however fast the computer draws, so two runs with the same game and `--seed` give
pixel-identical screenshots (not in a multiplayer game, which goes by real time).

## 2. The original under Wine (for side-by-side comparisons)

```sh
sudo pacman -S --needed wine winetricks
# If pacman does not find 'wine': enable [multilib] in /etc/pacman.conf, then pacman -Syu
```

A separate prefix just for the comparisons (the Heroic installation stays untouched):

```sh
export ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602"
export WINEPREFIX="$HOME/.wine-anno1602"
WINEARCH=win32 wineboot -i 2>/dev/null || wineboot -i
cd "$ANNO1602_DIR" && wine 1602.exe
```

If DirectDraw acts up under Wine: put [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) as `ddraw.dll`
into a *copy* of the game folder.
