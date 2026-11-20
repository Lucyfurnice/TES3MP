# TES3MP on OpenMW 0.51.0

An experimental port bringing **TES3MP 0.8.1** multiplayer networking, server scripting, and state synchronization onto the **OpenMW 0.51.0** (C++20) engine codebase.

> **Experimental Pre-Release Notice (`v0.51.0-alpha.1` milestone):**
> This fork pairs TES3MP `0.8.1` (protocol `10`) with OpenMW `0.51.0` commit-hash verification (`Version::getCommitHash()`). **Clients and servers from legacy TES3MP 0.8.1 releases are not wire-compatible with this fork.** Expect breaking changes while remaining client/mod runtime edge cases are stabilized.

---

## Compatibility & Status Matrix

| Component | Target | Build Status | Runtime / Wire Verification |
| :--- | :--- | :--- | :--- |
| **`tes3mp-server`** (`apps/openmw-mp`) | OpenMW 0.51.0 | Compiles & links cleanly | Boots to `OnServerPostInit`; verified CrabNet/RakNet handshakes, `PacketPreInit` checksum validation, and `ESM::RefId` serialization |
| **`openmw-mp` Core** (`components/openmw-mp`) | OpenMW 0.51.0 | Compiles cleanly | Packet structures (`BasePacket`, `PacketPreInit`, `Player`/`Actor`/`Object`/`Worldstate`) updated for `ESM::RefId` and OpenMW 0.51.0 data types |
| **`tes3mp` Client** (`apps/openmw/mwmp`) | OpenMW 0.51.0 | Compiles & links cleanly | Connects to `tes3mp-server`, exchanges `ID_GAME_BASE_INFO` / `ID_PLAYER_BASEINFO` / `ID_LOADED`, loads cells/actors/UI; active soak testing with modded setups |
| **Master / Browser** (`BUILD_MASTER`, `BUILD_BROWSER`) | OpenMW 0.51.0 | Off by default | Not yet enabled or verified against Qt6 / OpenMW 0.51.0 |

---

## Documentation Map

- **[ARCHITECTURE.md](ARCHITECTURE.md)** — Subsystem boundaries (`components/openmw-mp`, `apps/openmw-mp`, `apps/openmw/mwmp`), vendored dependencies (`CrabNet`, `LuaBridge`, `PicoSHA2`, `Base64`), CMake wiring, and startup/wire handshake flow.
- **[MIGRATION_NOTES.md](MIGRATION_NOTES.md)** — Engineering notes on breaking API shifts resolved between OpenMW 0.47 (TES3MP 0.8.1) and OpenMW 0.51.0 (`ESM::RefId`, `MWMechanics::Spells` vector migration, `WorldModel` cell cache lifetime, `MWLua` init ordering, Windows Debug ABI, crash diagnostics).
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — Milestone-based commit conventions, review and verification standards, and AI-assisted development disclosure.

---

## Reproducible Build Instructions

### Core Requirements

- **C++ Standard:** C++20 (`CMAKE_CXX_STANDARD=20` required)
- **CMake:** 3.16+ (tested with CMake `4.5.0-rc3` + `Ninja`)
- **Networking Library:** [CrabNet](https://github.com/TES3MP/CrabNet) built as `RakNetLibStatic` / `RakNetLibStaticd` (discovered via `cmake/FindRakNet.cmake` using `RAKNET_ROOT`)
- **OpenMW 0.51.0 Dependencies:**
  - OpenSceneGraph `3.6.5` (`.nif`/image plugins; `.dae` Collada plugin is optional)
  - Boost `>= 1.70.0`, SDL2 `>= 2.0.20`, MyGUI `3.4.3`, Bullet `>= 286`, LuaJIT (`USE_LUAJIT=ON`), OpenAL, FFmpeg `>= 3.2`, ICU, yaml-cpp, RecastNavigation, LZ4, SQLite3, zlib
  - **Vendored in-tree (`extern/`):** `Base64`, `LuaBridge`, `PicoSHA2`, `oics`, `smhasher`, `sol3`, `osg-ffmpeg-videoplayer`

### Windows (MSVC / Visual Studio 2026 + Ninja + vcpkg)

Tested toolchain:
- **Compiler:** MSVC toolset `14.51.36231` (Visual Studio 18 / 2026, x64)
- **Dependencies:** `vcpkg` (`x64-windows` triplet) + prebuilt CrabNet (`RAKNET_ROOT=D:/Lib/__staging__/CrabNet`)

From an **x64 Native Tools Command Prompt** (or after calling `vcvars64.bat`):

```powershell
$env:RAKNET_ROOT = "D:/Lib/__staging__/CrabNet"

cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_OPENMW=ON `
  -DBUILD_OPENMW_MP=ON `
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

cmake --build build --target openmw tes3mp-server -j 8
```

> **Windows Debug ABI note:** In `Debug` builds, `CMakeLists.txt` prioritizes `<vcpkg>/x64-windows/debug` ahead of `<vcpkg>/x64-windows` in `CMAKE_PREFIX_PATH` so OpenSceneGraph links against debug CRT libraries (`_ITERATOR_DEBUG_LEVEL=2`). Mixing Release OSG DLLs into a Debug build will fail at runtime.

### Linux / Unix (GCC or Clang)

Requirements: GCC 11+ or Clang 15+ (full C++20 support), CMake 3.16+, Ninja/Make, development packages for OpenMW 0.51.0 dependencies, and CrabNet installed or pointed to via `RAKNET_ROOT`.

```bash
export RAKNET_ROOT=/path/to/CrabNet

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_OPENMW=ON \
  -DBUILD_OPENMW_MP=ON \
  -DBUILD_LAUNCHER=OFF \
  -DBUILD_OPENCS=OFF \
  -DBUILD_BROWSER=OFF \
  -DBUILD_MASTER=OFF \
  -DUSE_LUAJIT=ON

cmake --build build --target openmw tes3mp-server -j "$(nproc)"
```

---

## Pre-Release Milestones & Binary Verification (`v0.51.0-alpha.1`)

When publishing experimental milestone tags (such as `v0.51.0-alpha.1`):

1. **Both `openmw` and `tes3mp-server` must be built from the same Git commit** (`components/version/version.cpp` embeds `Version::getCommitHash()`, which the server and client validate during `PacketPreInit`).
2. **Legacy 0.8.1 incompatibility:** Protocol/script payloads containing OpenMW 0.51.0 `ESM::RefId` formats and `builtin.omwscripts` load orders will not interoperate with unpatched TES3MP 0.8.1 binaries.
3. **Reference local Windows Debug build checksums** (commit `6263f9029f`, `CMAKE_BUILD_TYPE=Debug`):
   - `openmw.exe`: `1F2177C8B269A1B0F447DE6B1D95C386EE731685714E888513A5D2088A665812`
   - `tes3mp-server.exe`: `A6931B0891A6938E3B370F348A769F9B8290B5FCD59DE7C6A4937CAA4502F0DC`

---

## Running Locally (Developer Smoke Test)

1. Ensure `resources/vfs-mw` and `builtin.omwscripts` are staged alongside your server/client configuration, and point `openmw.cfg` at your Morrowind `Data Files` installation (you must own Morrowind to use TES3MP/OpenMW).
2. Start `tes3mp-server.exe` from the build directory (or with `--config` pointing to `tes3mp-server-default.cfg`).
3. Launch the client against localhost:

```powershell
.\openmw.exe --connect=127.0.0.1:25565
```

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

LLM coding agents were used during this port to accelerate repetitive API refactoring across the codebase (such as `ESM::RefId` conversions, packet boilerplate updates, and subsystem hook alignment). All generated diffs are manually reviewed, compiled, and verified against OpenMW 0.51.0 headers, build targets, and wire protocols before acceptance.
