# TES3MP Headless on OpenMW 0.51.0 (`tes3mp-modern-headless`)

This branch is specifically dedicated to building and developing **`tes3mp-headless`** (`apps/tes3mp-headless/`) on top of the **TES3MP / OpenMW 0.51.0** (C++20) engine codebase.

> **Branch Scope — `tes3mp-headless` vs. `tes3mp-server`:**
> This branch is specifically focused on **`tes3mp-headless`**, not `tes3mp-server` (`apps/openmw-mp/`).
> - **`tes3mp-server`** is TES3MP's packet-forwarding and Lua server host without full OpenMW world/physics/AI simulation.
> - **`tes3mp-headless`** is a standalone C++20 headless OpenMW `0.51.0` **full simulation host** running `MWWorld`, `MWMechanics`, `MWPhysics`, `MWScript`, `MWLua`, `MWDialogue`, `MWState`, and `MWClass` natively without a window, OpenGL context, `osgViewer`, `MyGUI` runtime, or `OpenAL` audio hardware—while retaining `osg`, `osgDB`, `osgUtil`, and `Bullet` in memory so `.esm`/`.esp`/`.omwscripts` content files, `.bsa` archives, and `.nif` collision meshes load natively on a deterministic fixed-timestep loop (`pollNetwork()` / `broadcastNetworkState()` hooks ready for RakNet packet injection).

> **Pre-Release Notice (`v0.51.0-rc2` base):**
> This fork pairs TES3MP `0.8.1` (protocol `10`) with OpenMW `0.51.0` commit-hash verification (`Version::getCommitHash()`). **Clients and servers from legacy TES3MP 0.8.1 releases are not wire-compatible with this fork.**

---

## Compatibility & Status Matrix

| Component | Target | Build Status | Runtime / Verification Notes |
| :--- | :--- | :--- | :--- |
| **`tes3mp-headless`** (`apps/tes3mp-headless`, `BUILD_TES3MP_HEADLESS`) | OpenMW 0.51.0 | Compiles & links cleanly (C++20) | Headless `MWBase` (`WindowManager`, `SoundManager`, `InputManager`), `MWRender` (`RenderingManager`, `Animation`/`NpcAnimation` text-key timing), `MWLua` UI/postprocessing stubs, and `mwmp` simulation stubs; bypasses main menu directly into world/cell fixed-timestep ticks (`--version` and `--help` verified; live world tick requires Morrowind data files) |
| **`openmw-mp` Core** (`components/openmw-mp`) | OpenMW 0.51.0 | Compiles cleanly | Packet structures (`BasePacket`, `PacketPreInit`, `Player`/`Actor`/`Object`/`Worldstate`) updated for `ESM::RefId` and OpenMW 0.51.0 data types |
| **`tes3mp-server`** (`apps/openmw-mp`, `BUILD_OPENMW_MP`) | OpenMW 0.51.0 | Present in tree (not the focus of this branch) | Legacy/relay TES3MP server target |
| **`tes3mp` Client** (`apps/openmw`, `BUILD_OPENMW`) | OpenMW 0.51.0 | Compiles & links cleanly | Verified regression-free alongside `TES3MP_HEADLESS` guards |
| **`tes3mp-browser`** (`BUILD_BROWSER`) | OpenMW 0.51.0 | Compiles & links cleanly (Qt6) | Optional Qt6 server browser |
| **`tes3mp-master`** (`BUILD_MASTER`) | OpenMW 0.51.0 | Compiles & links cleanly | Optional C++20 master server |
| **Launcher, Wizard & `openmw-cs`** (`BUILD_LAUNCHER`, `BUILD_WIZARD`, `BUILD_OPENCS`) | OpenMW 0.51.0 | Compiles & links cleanly (Qt6) | Optional desktop GUI tools |

---

## Headless Architecture Overview (`apps/tes3mp-headless/`)

- **CMake Integration:** Controlled by `BUILD_TES3MP_HEADLESS` (default `ON`) in root `CMakeLists.txt`, `components/CMakeLists.txt`, and `extern/CMakeLists.txt`, compiling [apps/tes3mp-headless/CMakeLists.txt](apps/tes3mp-headless/CMakeLists.txt) with `TES3MP_HEADLESS` defined.
- **No Rendering / Audio / GUI Runtime:**
  - SDL2 is initialized strictly with `SDL_INIT_TIMER | SDL_INIT_EVENTS` (no `SDL_INIT_VIDEO`).
  - `apps/tes3mp-headless` explicitly omits `MyGUI`, `OPENGL_LIBRARIES`, `OSGVIEWER`, `OpenAL`, `FFmpeg`, `oics`, and `osg-ffmpeg-videoplayer` from its target link list.
  - `MWBase` virtual interfaces are satisfied by `HeadlessWindowManager`, `HeadlessSoundManager` (non-null dummy `MWSound::Sound` / `MWSound::Stream` handles), and `HeadlessInputManager`.
  - `MWRender` adapters keep `Resource::KeyframeManager` + `SceneUtil::TextKeyMap` timing for combat/spell animation text keys (`headlessanimation.cpp`) and `MWRender::LandManager` (`mwrender/landmanager.cpp`) for ESM terrain heights.
- **Direct World Boot & Fixed-Timestep Loop:**
  - `HeadlessEngine` (`headlessengine.hpp` / `headlessengine.cpp`) boots VFS archives, `ResourceSystem`, `World`, `MechanicsManager`, `ScriptManager`, `DialogueManager`, `Journal`, `StateManager`, and `MWLua::LuaManager` / `Worker`, invokes `StateManager::newGame(true)` plus optional `--start-cell` teleport, and executes a fixed `1.0 / tickRate` simulation loop with `pollNetwork()` before and `broadcastNetworkState()` after every tick.

---

## Documentation Map

- **[ARCHITECTURE.md](ARCHITECTURE.md)** — Subsystem boundaries (`components/openmw-mp`, `apps/openmw-mp`, `apps/openmw/mwmp`), vendored dependencies (`CrabNet`, `LuaBridge`, `PicoSHA2`, `Base64`), and wire handshake flow.
- **[MIGRATION_NOTES.md](MIGRATION_NOTES.md)** — Engineering notes on OpenMW 0.47 to OpenMW 0.51.0 API migrations (`ESM::RefId`, `MWMechanics::Spells`, `WorldModel`, `MWLua`, Windows Debug ABI).
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — Commit conventions, verification standards, and AI-assisted development disclosure.

---

## Reproducible Build Instructions

### Core Requirements

- **C++ Standard:** C++20 (`CMAKE_CXX_STANDARD=20` required)
- **CMake:** 3.16+ (tested with CMake + `Ninja`)
- **Networking Library:** [CrabNet](https://github.com/TES3MP/CrabNet) built as `RakNetLibStatic` / `RakNetLibStaticd` (discovered via `cmake/FindRakNet.cmake` using `RAKNET_ROOT`)
- **Headless Simulation Dependencies (`BUILD_TES3MP_HEADLESS=ON`):**
  - OpenSceneGraph `3.6.5` (`.nif`/image DB & utils), Bullet `>= 286` (double precision), RecastNavigation, LuaJIT (`USE_LUAJIT=ON`), Boost `>= 1.70.0` (`iostreams`, `program_options`), SDL2 `>= 2.0.20`, ICU, yaml-cpp, LZ4, SQLite3, zlib
  - Qt6, OpenAL, and FFmpeg are **only** required if enabling desktop client/editor targets (`BUILD_OPENMW`, `BUILD_OPENCS`, `BUILD_LAUNCHER`, `BUILD_WIZARD`, `BUILD_BROWSER`).

### Windows (MSVC / Visual Studio 2026 + Ninja + vcpkg)

Tested toolchain:
- **Compiler:** MSVC toolset `14.51.36231` (Visual Studio 18 / 2026, x64)
- **Dependencies:** `vcpkg` (`x64-windows`) + prebuilt CrabNet (`RAKNET_ROOT=D:/Lib/__staging__/CrabNet`)

From an **x64 Native Tools Command Prompt** (or after calling `vcvars64.bat`):

#### Full Build (Headless Host + Client)
```powershell
$env:RAKNET_ROOT = "D:/Lib/__staging__/CrabNet"

cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TES3MP_HEADLESS=ON `
  -DBUILD_OPENMW=ON `
  -DBUILD_OPENMW_MP=ON `
  -DBUILD_LAUNCHER=OFF `
  -DBUILD_OPENCS=OFF `
  -DBUILD_WIZARD=OFF `
  -DBUILD_BROWSER=OFF `
  -DBUILD_MASTER=OFF `
  -DUSE_LUAJIT=ON

cmake --build build --target tes3mp-headless openmw -j 8
```

#### Headless-Only Build (No Client / Editor / OpenAL / FFmpeg / Qt6)
```powershell
$env:RAKNET_ROOT = "D:/Lib/__staging__/CrabNet"

cmake -S . -B build-headless -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TES3MP_HEADLESS=ON `
  -DBUILD_OPENMW=OFF `
  -DBUILD_OPENMW_MP=OFF `
  -DBUILD_LAUNCHER=OFF `
  -DBUILD_OPENCS=OFF `
  -DBUILD_WIZARD=OFF `
  -DBUILD_BROWSER=OFF `
  -DBUILD_MASTER=OFF `
  -DBUILD_BSATOOL=OFF `
  -DBUILD_ESMTOOL=OFF `
  -DBUILD_ESSIMPORTER=OFF `
  -DBUILD_MWINIIMPORTER=OFF `
  -DUSE_LUAJIT=ON

cmake --build build-headless --target tes3mp-headless -j 8
```

> **Windows Debug ABI note:** In `Debug` builds, `CMakeLists.txt` prioritizes `<vcpkg>/x64-windows/debug` ahead of `<vcpkg>/x64-windows` in `CMAKE_PREFIX_PATH` so dependencies link against debug CRT libraries (`_ITERATOR_DEBUG_LEVEL=2`).

### Linux / Unix (GCC or Clang)

Requirements: GCC 11+ or Clang 15+ (C++20), CMake 3.16+, Ninja/Make, OpenMW dependencies for simulation (OSG, Bullet, RecastNavigation, LuaJIT, Boost, SDL2, ICU, yaml-cpp, LZ4, SQLite3, zlib), and CrabNet (`RAKNET_ROOT`).

```bash
export RAKNET_ROOT=/path/to/CrabNet

cmake -S . -B build-headless -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TES3MP_HEADLESS=ON \
  -DBUILD_OPENMW=OFF \
  -DBUILD_OPENMW_MP=OFF \
  -DBUILD_LAUNCHER=OFF \
  -DBUILD_OPENCS=OFF \
  -DBUILD_WIZARD=OFF \
  -DBUILD_BROWSER=OFF \
  -DBUILD_MASTER=OFF \
  -DBUILD_BSATOOL=OFF \
  -DBUILD_ESMTOOL=OFF \
  -DBUILD_ESSIMPORTER=OFF \
  -DBUILD_MWINIIMPORTER=OFF \
  -DUSE_LUAJIT=ON

cmake --build build-headless --target tes3mp-headless -j "$(nproc)"
```

To build `tes3mp-headless` alongside the full client and tools, set `-DBUILD_OPENMW=ON -DBUILD_OPENMW_MP=ON` (and include Qt6/OpenAL/FFmpeg development packages).

---

## Running `tes3mp-headless`

1. Stage `resources/` (including `resources/vfs` and `resources/lua_libs`) alongside `tes3mp-headless`, or pass `--resources <path>`.
2. Point `--data` and `--content` at a valid Morrowind `Data Files` directory or `.omwgame`/`.omwscripts` content set (you must own Morrowind to use TES3MP/OpenMW).
3. Launch directly into an interior or exterior cell on a fixed simulation tick loop:

```powershell
.\build\tes3mp-headless.exe `
  --resources .\build\resources `
  --data "C:\path\to\Morrowind\Data Files" `
  --fallback-archive Morrowind.bsa `
  --content Morrowind.esm `
  --start-cell "Seyda Neen" `
  --port 25565 `
  --tick-rate 60 `
  --max-ticks 120
```

Useful CLI options (`tes3mp-headless --help`):
- `--data <dir>`: Content data directory (repeatable; later paths take priority)
- `--fallback-archive <bsa>`: BSA archive to mount in the VFS
- `--content <file>`: `.esm`, `.esp`, `.omwgame`, `.omwaddon`, or `.omwscripts` file to load
- `--start-cell <name>`: Initial interior or exterior cell to load after booting
- `--tick-rate <hz>`: Fixed simulation tick rate in Hz (default `60`)
- `--max-ticks <n>`: Exit cleanly after `n` simulation ticks (`0` runs continuously until `Ctrl+C` / `SIGTERM`)
- `--port <port>`: UDP port reserved for TES3MP RakNet integration (default `25565`)

---

## Pre-Release Versioning Note

1. **Commit Verification:** `components/version/version.cpp` embeds `Version::getCommitHash()`, validated during TES3MP `PacketPreInit` handshakes.
2. **`TES3MP_VERSION` stays `"0.8.1"`:** `components/openmw-mp/Version.hpp` retains `"0.8.1"` for TES3MP CoreScripts compatibility while `resources/version` reports OpenMW `0.51.0`.

---

## Upstream OpenMW Information & Licenses

- **Base Engine:** OpenMW `0.51.0` — GPLv3 (see [LICENSE](LICENSE))
- **OpenMW Website:** [https://www.openmw.org](https://www.openmw.org)
- **Font Licenses:**
  - `DejaVuLGCSansMono.ttf`: [files/data/fonts/DejaVuFontLicense.txt](files/data/fonts/DejaVuFontLicense.txt)
  - `DemonicLetters.ttf`: [files/data/fonts/DemonicLettersFontLicense.txt](files/data/fonts/DemonicLettersFontLicense.txt)
  - `MysticCards.ttf`: [files/data/fonts/MysticCardsFontLicense.txt](files/data/fonts/MysticCardsFontLicense.txt)

---

## AI-Assisted Development Disclosure

LLM coding agents were used on this branch to implement the `tes3mp-headless` CMake target, `MWBase`/`MWRender`/`MWLua`/`mwmp` headless adapters, and fixed-timestep simulation host alongside OpenMW `0.51.0` API integration.
