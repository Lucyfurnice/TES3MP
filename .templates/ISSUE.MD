---
name: TES3MP Headless Bug / Crash Report
about: Report a crash, freeze, or simulation issue in tes3mp-headless (OpenMW 0.51.0)
title: "[tes3mp-headless] <short summary of crash or issue>"
labels: bug
---

## Summary
<!-- Concise description of what happened (startup crash, cell load crash, Lua/physics bug, etc.). -->

## Build & Version Information
<!-- Run `tes3mp-headless --version` or check `resources/version` and fill in the output below. -->
- **Repository / Branch:** `https://github.com/Lucyfurnice/TES3MP` (`tes3mp-modern-headless`)
- **Executable Version (`--version`):**
  ```text
  OpenMW version: 0.51.0
  Revision: <git commit hash, e.g. 6b9cb43764>
  ```
- **Operating System / Architecture:** <!-- e.g. Windows 11 x64 / Ubuntu 24.04 x86_64 -->
- **Build Type:** <!-- Debug / RelWithDebInfo / Release -->

## Launch Command & Configuration
<!-- Paste the exact command-line invocation used to launch `tes3mp-headless`. -->
```powershell
tes3mp-headless.exe --resources <path> --data "<path>" --content Morrowind.esm --start-cell "<cell>" --tick-rate 60 --port 25565
```
- **Loaded Content Files (`.esm` / `.esp` / `.omwaddon` / `.omwscripts`):**
  <!-- Mention if vanilla only or modded (e.g. Tamriel_Data, TR_Mainland, Lua physics mods). -->

## Crash Dump & Logs
Please attach **both** of the following files from your OpenMW user log/profile directory (on Windows usually `Documents\My Games\OpenMW\`):

1. **Crash / Freeze Dump:**
   - Windows crash: `tes3mp-headless-crash.dmp` (attached as `.zip` if needed)
   - Windows freeze: `tes3mp-headless-freeze.dmp`
   - Linux/macOS crash: `tes3mp-headless-crash.log`
2. **Application Log:**
   - `tes3mp-headless.log`

### Last 40 Lines of `tes3mp-headless.log`
```text
<!-- Paste the tail of tes3mp-headless.log here, including loaded content files, Lua scripts, and the final logged line before the crash. -->
```

## Steps to Reproduce
1. Configure/launch `tes3mp-headless` with the command above.
2. Observe behavior at startup / cell load / tick N.
3. Attach any deterministic `--random-seed` or `--max-ticks` reproducer if applicable.

## Expected vs. Actual Behavior
- **Expected:** `tes3mp-headless` loads the world/cell and runs fixed-timestep simulation ticks cleanly.
- **Actual:** <!-- Describe the fatal error, exception message, or unexpected simulation behavior. -->
