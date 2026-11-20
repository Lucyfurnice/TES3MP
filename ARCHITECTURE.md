# Architecture Overview: TES3MP on OpenMW 0.51.0

This document describes how TES3MP's multiplayer stack integrates into OpenMW `0.51.0` while keeping networking state and packet serialization isolated from core single-player subsystems.

---

## 1. Repository & Module Boundaries

```text
+-------------------------------------------------------------------+
|                        Applications (apps/)                       |
|                                                                   |
|  apps/openmw/mwmp/                  apps/openmw-mp/               |
|  - Client lifecycle (Main,          - Standalone tes3mp-server    |
|    Networking, LocalPlayer,         - Server Script runtime       |
|    DedicatedPlayer, LocalActor,       (LangLua / LuaBridge,       |
|    DedicatedActor, CellController,     Script/Functions/*)        |
|    GUIController, RecordHelper)     - Cell / Player / Networking  |
+------------------+-------------------------------+----------------+
                   |                               |
                   v                               v
+-------------------------------------------------------------------+
|               Shared Multiplayer Core (components/openmw-mp/)     |
|                                                                   |
|  Base/        Controllers/   Master/   Packets/{Actor,Object,     |
|                                        Player,System,Worldstate}  |
|  TimedLog     Utils          Version   NetworkMessages            |
+------------------+-------------------------------+----------------+
                   |                               |
                   v                               v
+-------------------------------------------------------------------+
|        OpenMW 0.51.0 Components & External Libraries              |
|                                                                   |
|  components/{esm,esm3,files,debug,version,...}                    |
|  CrabNet (FindRakNet.cmake) | extern/{Base64,LuaBridge,PicoSHA2}  |
+-------------------------------------------------------------------+
```

### Key Directories

- **`components/openmw-mp/`** — Compiled into the `components` static library via [components/CMakeLists.txt](components/CMakeLists.txt). Contains shared protocol constants (`NetworkMessages.hpp`, `Version.hpp`), data models (`Base/*`), packet serializers (`Packets/*`), and controllers (`Controllers/*`).
- **`apps/openmw-mp/`** — Dedicated `tes3mp-server` executable (`BUILD_OPENMW_MP=ON`). Exposes the C/LuaJIT server scripting API (`Script/Functions/*`) via `extern/LuaBridge` and validates connected clients against `tes3mp-server-default.cfg` and CoreScripts.
- **`apps/openmw/mwmp/`** — Client multiplayer bridge. Coordinates RakNet/CrabNet packet processing (`Networking`, `processors/*`) with OpenMW 0.51.0 managers (`mwworld`, `mwmechanics`, `mwgui`, `mwstate`, `mwlua`).
- **`extern/`** — Vendored multiplayer helpers wired into `CMakeLists.txt` and `apps/openmw-mp/CMakeLists.txt`:
  - `extern/Base64` (`add_subdirectory(extern/Base64)`)
  - `extern/LuaBridge` (header-only C++/LuaJIT bindings for `tes3mp-server`)
  - `extern/PicoSHA2` (SHA-256 content/password hashing)
- **`cmake/FindRakNet.cmake`** — Locates CrabNet's `RakNetLibStatic` / `RakNetLibStaticd` library and `raknet/RakPeer.h` headers via `RAKNET_ROOT` (or system library paths).

---

## 2. Startup & Wire Handshake Flow

1. **Pre-Engine Connection (`OMW::Engine::go` -> `mwmp::Main::init`)**
   - Reads `--connect` or server config, connects via CrabNet (`Networking::connect`), and executes `PacketPreInit` / `ID_GAME_BASE_INFO`.
   - Validates `Version::getCommitHash()`, TES3MP protocol version (`TES3MP_PROTO_VERSION = 10`), and plugin/ESM checksums (`mContentFiles` / `mFileCollections`, excluding client built-ins such as `builtin.omwscripts` from server ESM checksum mismatch checks).
2. **Engine Initialization (`OMW::Engine::prepareEngine`)**
   - Initializes OSG viewer, SDL window, VFS, `ResourceSystem`, `LuaManager`, and GUI.
   - `mwmp::Main::postInit()` sets up multiplayer chat GUI, creates the session (`MWState::StateManager::newGame(true)`), toggles multiplayer AI handling, and creates the placeholder interior cell (`RecordHelper::createPlaceholderInteriorCell()`).
3. **First Gameplay Frame (`mwmp::Main::frame` -> `updateWorld`)**
   - Sends `ID_PLAYER_BASEINFO` and `ID_LOADED` once local initialization completes, then pumps RakNet/CrabNet packets (`Networking::update`) every frame ahead of `LuaManager::synchronizedUpdate()` and `StateManager::update()`.

---

## 3. OpenMW 0.51.0 Integration Points

- **World & Cell State (`apps/openmw/mwworld/`)**
  - `World::startNewGame(bool bypass)` skips the blocking 5-tick synchronous `GlobalScripts::run()` loop when `bypass` is true, delegating global script execution cleanly to the normal frame loop.
  - `World::clearCellStore` / `WorldModel::clearCell` resets cell stores when dynamic cells (`ID_RECORD_DYNAMIC`) or cell state resets arrive from the server without shrinking `mIdCache`.
- **State & Lua Runtime (`apps/openmw/mwstate/`, `apps/openmw/mwlua/`)**
  - `StateManager::newGame` transitions `mState` to `State_Running` prior to `World::startNewGame` (matching `loadGame`) so OpenMW 0.51.0 global Lua `onInit()` hooks can inspect/create records cleanly.
  - `mwlua/uibindings.cpp` maps TES3MP's custom `GM_TES3MP_InputBox` and `GM_TES3MP_ListBox` modes so Lua UI scripts calling `_getUiModeStack()` do not abort on unknown `GuiMode` keys.
- **Mechanics & Physics (`apps/openmw/mwmechanics/`, `apps/openmw/mwphysics/`)**
  - Remote players/actors (`DedicatedPlayer`, `DedicatedActor`) synchronize stats, animations, spells, and equipment via `MechanicsHelper` and `mwmp::ActorProcessor`/`PlayerProcessor` without local client AI/magicka overrides.
  - Raycast LOS checks (`PhysicsTaskScheduler::hasLineOfSight`, `PhysicsSystem::getLineOfSight`, `MovementSolver::traceDown`) guard degenerate zero-length rays caused by overlapping actor spawns.

---

## 4. Build Switches & Optional Targets

| CMake Option | Default | Purpose |
| :--- | :--- | :--- |
| `BUILD_OPENMW_MP` | `ON` | Enables `apps/openmw-mp` (`tes3mp-server`) and multiplayer components |
| `BUILD_OPENMW` | `ON` | Builds the OpenMW `0.51.0` + `mwmp` client (`openmw`) |
| `USE_LUAJIT` | `ON` | Required by OpenMW Lua & `tes3mp-server` `LuaBridge` runtime |
| `BUILD_BROWSER` | `OFF` | Legacy Qt server browser (pending 0.51.0/Qt6 migration) |
| `BUILD_MASTER` | `OFF` | Legacy master server binary (disabled by default during client/server stabilization) |
