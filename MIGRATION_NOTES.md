# Migration Notes: Porting TES3MP 0.8.1 onto OpenMW 0.51.0

This log documents the concrete breaking API changes between OpenMW `0.47` (TES3MP `0.8.1` baseline) and OpenMW `0.51.0` resolved in this fork, along with their commit references and remaining known gaps.

---

## 1. Key API & Engine Migrations Resolved

### 1.1 `ESM::RefId` Serialization & Empty Sentinels
OpenMW `0.51.0` replaced raw lowercase `std::string` record/cell identifiers across `ESM`, `MWWorld`, and `MWMechanics` with strongly typed `ESM::RefId`.

- **Packet Layer (`components/openmw-mp/Packets/BasePacket.hpp`):**
  - Added `BasePacket::RW(ESM::RefId&, ...)` converting between wire strings and `ESM::RefId::getRefIdString()` / `ESM::RefId::stringRefId(...)`.
- **Server Scripting (`apps/openmw-mp/Script/Functions/RecordsDynamic.cpp`, `Spells.cpp`, `Stats.cpp`, `Actors.cpp`):**
  - Migrated dynamic record creation fields (`mId`, `mScript`, `mHair`, `mHead`, `mRace`, `mClass`, `mFaction`, `mSound`, etc.) to `ESM::RefId::stringRefId(...)`.
- **Client `"Empty{}"` Sentinel Fix (`apps/openmw/mwmp/LocalPlayer.cpp`):**
  - In OpenMW 0.51.0, an unset `ESM::RefId` formats/debug-prints as `"Empty{}"` rather than `""`. Guarded `selectedSpell` reception so empty or `"Empty{}"` IDs clear the selected spell instead of throwing `Spell 'Empty{}' not found`. (`ae4ef692f4`)
- **Plugin Handshake (`apps/openmw/mwmp/Networking.cpp`):**
  - Replaced legacy extension parsing with `Misc::getFileExtension` and excluded `builtin.omwscripts` from TES3MP `PacketPreInit` server plugin checksum enforcement.

### 1.2 Container & Iterator Semantics in OpenMW 0.51.0
- **`MWMechanics::Spells` (`apps/openmw/mwmp/LocalPlayer.cpp`):**
  - OpenMW 0.51.0 stores `MWMechanics::Spells::Collection` as `std::vector<const ESM::Spell*>` instead of a `std::map`. Calling `ptrSpells.remove(spell->mId)` inside `LocalPlayer::setSpellbook()` invalidated loop iterators before `if (iter == ptrSpells.end())`, triggering MSVC Debug STL `vector iterators incompatible` (`include\vector:203`). Replaced with a two-pass collect-and-remove loop. (`51686469f8`)
- **`MWWorld::WorldModel::clearCell` & `mIdCache` (`apps/openmw/mwworld/worldmodel.cpp`):**
  - `clearCell()` called `mIdCache.clear()`, shrinking the ring buffer from `Settings::cells().mPointersCacheSize` to `0` so the next `getPtrAndCache()` indexed out of bounds (`include\vector`). Changed `clearCell()` to fill `mIdCache` with `{ ESM::RefId(), nullptr }` (matching `WorldModel::clear()`) and guarded `getPtrAndCache()` against empty/out-of-range indices. (`6263f9029f`)
- **`GUIChat` History Iterator (`apps/openmw/mwmp/GUI/GUIChat.cpp`):**
  - Initialized `mCurrent = mCommandHistory.end()` in the constructor to prevent value-initialized `std::list` iterator dereference assertions (`include\list:146`). (`8a4b1ed3c1`)

### 1.3 Startup Lifecycle, Lua `onInit()`, & Freeze Prevention
- **State Transition Before `newGameStarted()` (`apps/openmw/mwstate/statemanagerimp.cpp`):**
  - `StateManager::newGame()` left `mState == State_NoGame` until after `World::startNewGame()` returned, while `startNewGame()` invoked `LuaManager::newGameStarted()` -> `addAutoStartedScripts()` -> `onInit()`. Mod scripts calling `world.createRecord(...)` in `onInit()` threw `"This function cannot be used until the game is fully initialized."`. Moved `mState = State_Running;` ahead of `startNewGame()`, matching `loadGame()`. (`00da168aef`)
- **Duplicate `newGame` Reload (`apps/openmw/engine.cpp`):**
  - Guarded `Engine::go()`'s fallback `newGame(!mNewGame)` with `mStateManager->getState() != State_Running` so `mwmp::Main::postInit()`'s session start is not wiped and restarted a second time. (`a6ca533fff`)
- **Synchronous Startup Script & Cell-Scan Hang (`apps/openmw/mwworld/worldimp.cpp`, `worldmodel.cpp`):**
  - Guarded the 5x pre-frame `GlobalScripts::run()` loop in `World::startNewGame()` with `if (!bypass)`.
  - Added periodic `SDL_PumpEvents()` (every 64 scanned unloaded cells when `SDL_INIT_VIDEO` is active) inside `WorldModel::getPtrByRefId()` so scanning large modded load orders from MWScript `OpDisable` does not trip Windows' 5-second `IsHungAppWindow` freeze detector (`tes3mp-freeze.dmp`). (`00da168aef`)

### 1.4 Lua UI Bindings & Physics Guards
- **Custom TES3MP `GuiMode` Entries (`apps/openmw/mwlua/uibindings.cpp`):**
  - Registered `GM_TES3MP_InputBox` and `GM_TES3MP_ListBox` in `sUiModes` so OpenMW 0.51.0 `scripts/omw/ui.lua` (`_getUiModeStack()`) does not abort with `invalid unordered_map<K, T> key`. (`ae4ef692f4`)
- **Zero-Length Raycasts on Actor Spawn (`apps/openmw/mwphysics/{mtphysics.cpp,physicssystem.cpp,movementsolver.cpp}`):**
  - Guarded `btCollisionWorld::rayTest` calls when `(to - from).length2() <= 1e-6f`, avoiding Bullet's `btVector3::normalize()` assert (`0x80000003`) when synced actors briefly share coordinates.

### 1.5 Build, Server Startup, & Crash Diagnostics
- **Windows `Debug` ABI Matching (`CMakeLists.txt`):**
  - Prioritized `vcpkg/installed/x64-windows/debug` in `CMAKE_PREFIX_PATH` for `Debug` builds to prevent `_ITERATOR_DEBUG_LEVEL` mismatches against OSG DLLs. (`7f1e393f20`)
- **Optional Collada Plugin (`CMakeLists.txt`, `components/misc/osgpluginchecker.cpp`):**
  - Dropped hard requirement for `osgdb_dae` (`osgdb_daed.dll`) since vcpkg's OSG package omits it by default. (`1dda83951c`)
- **Server Startup & Windows `io2` (`apps/openmw-mp/main.cpp`, `Script/LangLua/LangLua.cpp`):**
  - Wired `Files::ConfigurationManager::addCommonOptions(desc)` and added a Windows `package.loaders` Lua fallback where `io2` C module is absent. (`97d4e24491`)
- **Symbolicated Crash & CRT Assert Reporting (`apps/openmw/main.cpp`, `components/debug/debugging.cpp`):**
  - Added SEH `SetUnhandledExceptionFilter`, `std::set_terminate`, `SIGABRT`, and `_CrtSetReportHook2` handlers using `DbgHelp` (`SymFromAddr` / `SymGetLineFromAddr64`) mirrored to `stderr` and `tes3mp-client-*.log`. (`ae4ef692f4`, `8a4b1ed3c1`)

---

## 2. Milestone History Highlights

- `8d00894946` — Import standalone TES3MP 0.8.1 targets and externals
- `863482cdf3` — Pass 1: Wire CMake/CrabNet and port `tes3mp-server` + `openmw-mp` to OpenMW 0.51.0
- `a734e14dd4` — Pass 2: Port `mwmp` and client engine hooks to OpenMW 0.51.0
- `6ca054fa1f` — Pass 2B: Port client core, `mwworld`, `mwmechanics`, and `mwgui`/`mwstate` hooks
- `af37f6417c` -> `6263f9029f` — Config/resource staging, crash diagnostics, and runtime bugfix milestones

---

## 3. Known Caveats & Open Work (`v0.51.0-alpha.1` Baseline)

- **Protocol Version:** Incompatible with TES3MP `0.8.1` binaries due to OpenMW `0.51.0` commit hash check and `ESM::RefId` serialization.
- **Legacy Sub-Targets:** `BUILD_BROWSER` and `BUILD_MASTER` are disabled by default and have not been ported/tested on Qt6 / OpenMW 0.51.0.
- **OpenMW Lua Mod API:** Mods referencing post-0.51.0 or unreleased Lua engine handlers (such as `onDropped` / `onPlaced` in `GlobalScripts`) log non-fatal `Not supported handler` errors.
- **Soak Testing:** Long-duration multi-client gameplay sessions and Linux GCC/Clang Release CI parity still need automated/manual verification.
