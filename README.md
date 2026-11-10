# Open1602 Master Server

A public server list and UDP relay for Open1602 Vita. Players can host and join public games without forwarding ports on their own routers. The server does not require original game files.

## Requirements

- CMake 3.25 or newer
- A C compiler and a C++20 compiler
- Make or Ninja

## Build

```sh
git clone --branch masterserver --single-branch https://github.com/Colie286/Open1602-Vita.git
cd Open1602-Vita
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Run

```sh
./build/open1602-master --port 16022
```

The server must be reachable on **UDP port 16022**. Run with `--help` for additional options. An example systemd service is provided in `tools/master-server/open1602-master.service`.

## Sources and licenses

The server is based on [Open1602](https://github.com/x4rd4s/open1602) and the Vita port's lobby and relay extensions. It uses [ENet 1.3.18](https://github.com/lsalzman/enet) for reliable UDP connections. ENet sources are included.

- Open1602 Master Server: **GPL-3.0-or-later**, see [LICENSE](LICENSE).
- ENet: **MIT**, see [third_party/enet/LICENSE](third_party/enet/LICENSE).
- Source provenance and modifications: see [NOTICE](NOTICE).
