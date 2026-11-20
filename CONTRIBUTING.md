# Contributing to TES3MP (OpenMW 0.51.0 Port)

This fork pairs OpenMW `0.51.0` with TES3MP multiplayer capabilities. Clean commit hygiene, reproducible builds, and explicit verification notes keep the port bisect-friendly and reviewable.

---

## Commit & Release Hygiene

### Milestone-Based Commits
Keep commits focused on a single logical pass or bugfix so regressions can be bisected cleanly:
- **Good:** *"Pass 1: Wire CMake/CrabNet and port tes3mp-server + openmw-mp to OpenMW 0.51.0"*, *"Fix invalidated vector iterator comparison in LocalPlayer::setSpellbook"*.
- **Avoid:** Dumping unrelated fixes, CMake tweaks, and gameplay changes into a single mega-commit, or committing non-compiling intermediate states.

### Pre-Release Tags & Binary Checksums
Experimental milestone releases (e.g. `v0.51.0-alpha.1`) should:
1. Tag a commit where both `openmw` and `tes3mp-server` compile cleanly.
2. Clearly note protocol boundaries (including incompatibility with legacy TES3MP `0.8.1` clients/servers).
3. Include `SHA-256` checksums (`Get-FileHash -Algorithm SHA256` / `sha256sum`) for any distributed binaries alongside the exact `CMAKE_BUILD_TYPE` and compiler toolchain used.

---

## Merge Request Checklist

To speed up review, include in every PR / MR description:
- **Summary & Motivation:** What subsystem changed (`components/openmw-mp`, `apps/openmw-mp`, `apps/openmw/mwmp`, or core OpenMW hooks) and why.
- **Verification Scope:** Exact CMake/build targets compiled (`openmw`, `tes3mp-server`, tests) and whether runtime client/server connection or wire serialization was exercised.
- **OpenMW Upstream Boundaries:** Keep multiplayer changes clearly marked (`Start of tes3mp addition` / `Start of tes3mp change`) inside shared OpenMW engine files to simplify future upstream rebases.
- **Formatting & Hygiene:** Match `.clang-format` / `.editorconfig`; preserve unrelated comments and docstrings; avoid sweeping formatting diffs on untouched code.

---

## Upstream OpenMW Engineering Guidelines

We inherit upstream OpenMW's core engineering policies:
- Avoid altering core singleplayer gameplay mechanics or breaking existing mod compatibility unless specifically required for multiplayer synchronization.
- Keep new multiplayer UI strings compatible with OpenMW's YAML localization system where applicable.
- Never silently mask crashes or assertion failures; prefer root-cause fixes backed by reproducible diagnostics (`openmw.log` / `tes3mp-client-*.log`).
- For general upstream OpenMW reference material, see the [OpenMW Developer Reference](https://wiki.openmw.org/index.php?title=Developer_Reference).

---

## AI-Assisted Development Disclosure

LLM coding agents were used during this fork's migration to accelerate repetitive API refactoring across the codebase (for example, `ESM::RefId` conversions, packet boilerplate updates, and engine hook alignment). All generated diffs are manually reviewed, compiled, and verified against OpenMW `0.51.0` headers and wire protocols before being accepted into the repository.
