# Build and Test

This article explains how to build LOICollectionA from source, run tests, and package releases.

## Environment Requirements

- [xmake](https://github.com/xmake-io/xmake) 3.0+
- `clang-cl` toolchain (LLVM + MSVC environment)
- Git

Update the xmake repository before the first build:

```bash
xmake repo -u
```

Project dependencies (automatically downloaded by xmake):

| Dependency | Version |
| --- | --- |
| levilamina | 26.20.7 |
| sqlitecpp | 3.3.3 |
| nlohmann_json | 3.12.0 |
| gtest | v1.17.0 (debug mode only) |
| preloader | 1.15.7 |
| levibuildscript | Latest |

## Building

### Basic Build

```bash
# Clone and enter the repository
git clone https://github.com/loitietu/LOICollectionA.git
cd LOICollectionA

# Build with default configuration (release, server)
xmake

# Build debug (also compiles the test code)
xmake f -m debug
xmake
```

### Build Options

| Option | Default | Description |
| --- | --- | --- |
| `-m debug|release` | `release` | Build mode; debug mode compiles gtest tests |
| `--target_type=server|client` | `server` | Target platform; client is used for client-side plugins |
| `--shared=y|n` | `y` | Whether to build a shared library (affects dependencies such as SQLite) |

```bash
# Clean and reconfigure
xmake f -c
xmake f -m debug --target_type=server
xmake
```

### Build Artifacts

The artifact is a `shared` library (`LOICollectionA.dll` on Windows), packaged with the modpacker rule (see below). The compile macros `LL_PLAT_S` (server)/`LL_PLAT_C` (client) are automatically defined by `target_type`, and `modules/server/*` and `modules/client/*` in the source are compiled mutually exclusively by target.

> [!NOTE]
> The project uses `c++.unity_build` (batch size 8) to speed up compilation and enables the `Global.h` precompiled header. Recompiling after modifying a header file may be slow, which is normal.

## Testing

Tests are based on gtest and **run inside the game server**:

1. Build in debug mode (`xmake f -m debug`); `tests/**` is automatically included
2. Deploy the built plugin to a LeviLamina server and start it
3. Run `/test all` in the server console; gtest will automatically run and output the results

> [!TIP]
> The `/test all` command is registered by `tests/server/TestCommand.cpp` via a hook after the server thread starts, and tests execute after a simulated player (`TestSimulatedPlayer`) is created.

### Test Directory Structure

```txt
tests/
├─ common/          # cross-platform tests: base (Cache/ServiceContainer/Wrapper, etc.),
│                   #   coro, data (Json/SQLite storage), frontend (LCUI lexer/parser/semantics/VM)
├─ server/          # server tests: mc (block/scoreboard tools, etc.), modules (plugin callbacks),
│                   #   TestCommand.cpp (entry), TestSimulatedPlayer (simulated player)
└─ client/          # client tests (compiled only for the client target)
```

### Fuzzing

`tests/common/frontend/FuzzTest.cpp` runs a deterministic fuzz harness as part of the normal gtest suite — no extra tooling needed. It feeds mutated sources and random token soups through the lexer, parser, component expander and semantic analyzer, and round-trips serialized `.lcp` packages through the serializer with corrupted bytes. Every input must terminate, never crash, and stay inside a per-input time budget (250 ms), so parser DoS regressions fail the suite instead of reaching a server.

The generator is seeded, so a failure is always reproducible. Raise the iteration count locally with:

```bash
LOICOLLECTION_A_FUZZ_ROUNDS=20000
```

> [!NOTE]
> The harness is intentionally deterministic rather than coverage-guided: the whole suite is compiled into the plugin and driven by `/test all`, which rules out sanitizer/process-based fuzzers such as libFuzzer.

## Packaging and Release

### modpacker

`xmake.lua` registers the `modpacker` rule via `scripts/modpacker.lua`, which automatically organizes the plugin directory during the build (plugin files, `gui`, `lang`, and other resources).

### tooth.json

The release manifest `tooth.json` defines the plugin metadata and installation method:

- `tooth`: release address (`github.com/loitietu/LOICollectionA`)
- `version`: plugin version (must match `set_version` in `xmake.lua`)
- `variants`: platform variants and dependencies (e.g. `win-x64` + LeviLamina 26.20.*)
- `assets`: release assets (zip packages in the Release)

### Release Workflow

`.github/workflows/release.yml` runs automatically when a tag (e.g. `v1.15.0`) is created: build → package zip → create GitHub Release → upload `lip` install assets. Users install via `lip install github.com/loitietu/LOICollectionA`.

## Code Standards

The repository comes with the following configurations; keep them before committing:

| File | Purpose |
| --- | --- |
| `.clang-format` | Code formatting (clang-format 15+) |
| `.clang-tidy` | Static analysis rules |
| `.clangd` | clangd language server configuration |

Other conventions:

- `Global.h` is the precompiled header, centrally including the base libraries (ll/api, base/ tools); new public headers can be considered for addition
- Compile options `set_exceptions("none")` + `/EHa`, `/utf-8`, `/permissive-`; do not rely on C++ exceptions
- Put platform-related code in `modules/server/*` or `modules/client/*`, and public headers in `include/server/*` or `include/client/*`
- Use the `LOICOLLECTION_A_API` / `LOICOLLECTION_A_NDAPI` export macros for the public API
