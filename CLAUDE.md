# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Qt6 / C++20 Minecraft launcher — a fork of Prism Launcher 10.0.2, licensed GPL-3.0-only. The top-level CMake project is named `Launcher` (the produced binary name is driven by `Launcher_APP_BINARY_NAME`); the fork is rebranded "FreesmLauncher". Notable divergences from upstream: offline mode without a Microsoft account, Ely.by and custom authlib-injector auth, animated cat packs, animated snow, screenshot-to-clipboard.

## Build & run

Requires CMake ≥ 3.28, Ninja Multi-Config, Qt6 (Core, Widgets, Concurrent, Network, NetworkAuth, Xml, OpenGL, Test, DBus, CoreTools) and a C++20 compiler.

Configure via CMake presets from `CMakePresets.json`:

- `linux`, `windows_mingw` — use the system compiler directly.
- `macos`, `macos_universal`, `windows_msvc` — require `VCPKG_ROOT` in the environment; they consume vcpkg's toolchain file. `macos_universal` builds a fat `x86_64;arm64` binary via the `universal-osx` triplet.

Typical flow:

```
cmake --preset <name>
cmake --build --preset <name>
```

Build dir is `build/`, install dir is `install/`. All presets set `Launcher_ENABLE_JAVA_DOWNLOADER=ON` and `ENABLE_LTO=ON` — don't drop these unintentionally.

Nix build: `nix build` (via `flake.nix` / `default.nix`); binary cache at `freesmlauncher.cachix.org`.

Packaging lives outside the main tree: `flatpak/` for Flatpak, `program_info/` for Windows/macOS metadata, `nix/` for Nix packaging glue.

## Tests

Qt `QTest`-based, registered with ECM's `ecm_add_test` and driven by CTest. Sources are in `tests/` (FileSystem, GZip, Library, Version, Task, INIFile, JavaVersion, ResourceFolder, Packwiz, XmlLogs, CatPack, and others). Test binaries link against the internal `Launcher_logic` library.

- Run all tests: `ctest --preset <name>` — test presets mirror the build presets.
- Run one test: either invoke the per-test binary under `build/<config>/tests/` directly, or `ctest --preset <name> -R <TestName>`.
- The `^example64|example$` name filter is applied to skip example tests.

## CI

GitHub Actions in `.github/workflows/`: `build.yml` orchestrates per-OS workflows (`linux.yml`, `macos.yml`, `windows.yml`), alongside `release.yml`, `publish.yml`, `flatpak.yml`, `cachix.yml`, `codeql.yml`, `commitlint.yml`, and `backport.yml`. `commitlint.yml` enforces the commit format below — a malformed message fails the PR, not just warns.

## Code style (see `docs/CONTRIBUTING.md`)

Run `clang-format` (config in `.clang-format`) on changed files before committing. `.clang-tidy` encodes the naming rules as lint.

Naming:

- `PascalCase` — types and enum constants.
- `m_camelCase` — private/protected data members; `s_camelCase` — their `static` siblings.
- `camelCase` — public data members, class methods, globals, non-`const` global variables.
- `SCREAMING_SNAKE_CASE` — `static const` members, `const` globals, macros.

Avoid `[[nodiscard]]` on plain getters. Reserve it for functions that allocate a resource the caller must release, have side effects with an error-status return, or are likely to be mistaken for having side effects.

Don't rename non-conforming identifiers in drive-by fashion — rename only when already refactoring the whole class.

## Commits

Format: `{type}({scope})!: {subject}`. Types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `chore`, `revert`, `release`, `ci`. Append `!` to mark a breaking change. Imperative mood, ≤ 50 chars (up to 72 when needed), no trailing period.

**Every commit must be signed off** — `git commit -s`, or a manual `Signed-off-by: Name <email>` trailer. The DCO check rejects unsigned commits.

## Architecture — the `launcher/` tree

Entry points:

- `launcher/main.cpp` — the launcher app (a Qt `Application` subclass).
- `launcher/updater/prismupdater/updater_main.cpp` — standalone self-update binary.
- `launcher/filelink/filelink_main.cpp` — IPC helper for file links / single-instance handoff.

UI code under `launcher/ui/` is deliberately decoupled from core logic, which lives in the `Launcher_logic` library (what the tests link against). Core building blocks:

- `launcher/tasks/` — the async `Task` base class and composites. Nearly every long operation (downloads, version resolution, instance ops) is a `Task`. UI observes tasks; tests exercise them headless.
- `launcher/net/` — HTTP, mirrors, proxies, download sinks. Networking funnels through here rather than raw `QNetworkAccessManager` in UI code.
- `launcher/meta/` — fetches and caches version/component metadata from meta servers; feeds the Minecraft component system.
- `launcher/minecraft/` — the Minecraft instance model: component lists, launch profiles, library resolution, assets, agents.
- `launcher/modplatform/` — abstraction over mod sources (CurseForge/Flame, Modrinth, FTB, ATLauncher, Technic, Packwiz). New platform integrations slot in behind the common interface.
- `launcher/resources/` — resource/texture/shader/data pack folder models surfaced to the instance UI.
- `launcher/java/` — JRE detection, version parsing, and the optional auto-downloader gated by `Launcher_ENABLE_JAVA_DOWNLOADER`.
- `launcher/launch/` — assembles the actual `java` command, environment, and launch steps.
- `launcher/settings/` — global and per-instance setting storage.
- Others: `console/`, `logs/`, `icons/`, `news/`, `screenshots/`, `discord/` (RPC), `archive/`, `updater/`, `filelink/`, `macsandbox/`, `translations/`.

Generated build-time config comes from `buildconfig/BuildConfig.cpp.in` (produces `BuildConfig.cpp` with URLs, version info, build artifact/platform fields). Edit the `.in` template, never the generated file.

Vendored third-party code in `libraries/`: `libnbtplusplus` (NBT), `LocalPeer` (single-instance guard, BSD from QtSingleApplication), `javacheck`, `launcher` (bootstrap java bits), `murmur2`, `rainbow`, `systeminfo`, `qdcss`. Prefer patching vendored copies over pulling in new dependencies.

## Gotchas

- Tests link `Launcher_logic` directly — keep new logic testable without a `QApplication`; don't leak UI types into the logic library.
- Sign-off (`-s`) and conventional-commit format are both CI-enforced; either one missing fails the PR.
- `VCPKG_ROOT` is mandatory for the `macos`, `macos_universal`, and `windows_msvc` presets; it is not used by `linux` or `windows_mingw`.
