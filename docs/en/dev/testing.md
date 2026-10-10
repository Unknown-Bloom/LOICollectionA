# Testing Guide

This article explains how the LOICollectionA test suite is wired up, how to run it, how to add tests, and the naming convention you must follow under `c++.unity_build`. For build-related topics see [Build and Test](./build.md).

## Design stance

The tests are based on [GoogleTest](https://github.com/google/googletest), but they are **not run through a separate test executable**. Instead the whole suite is compiled into the plugin and executed inside a real game server process:

- Only `debug` mode compiles and links gtest plus `tests/**` (see the `is_mode("debug")` branch in `xmake.lua`)
- The entry point is triggered automatically by `tests/server/TestMain.cpp` on the server thread, so test code can touch runtime objects such as `Level`, `Player` and `BlockSource` directly
- This is also why sanitizer/process-based fuzzers such as libFuzzer are not an option here; see [Fuzzing](#fuzzing)

## Running the tests

### 1. Build the server target in debug mode

```bash
xmake f -m debug --target_type=server -y
xmake -y
```

`--target_type=server` compiles `tests/common/**` and `tests/server/**` and excludes `tests/client/**`; `--target_type=client` is the mirror image.

### 2. Deploy to a LeviLamina server

Put the build artifacts (the gtest-linked `LOICollectionA.dll` plus its resource directories) into the server's `plugins/LOICollectionA/` as described in [Build and Test](./build.md), then start the server.

### 3. Start the server and the tests run automatically

Once the server has loaded the level and fired `ServerStartedEvent`, the suite starts **automatically** on the server thread with no manual step:

```log
[TestMain] server started, running LOICollectionA test suite
[==========] Running N tests from M test suites.
...
[  PASSED  ] N tests.
[TestMain] test suite finished
[TestMain] requesting server shutdown
```

`TestMain.cpp` listens for `ServerStartedEvent` through `ll::event::EventBus`: on receipt it calls `testing::InitGoogleTest()` on the `ServerThreadExecutor`, creates a simulated player, runs `RUN_ALL_TESTS()`, destroys the simulated player, and finally calls `ll::service::getDedicatedServer()->stop()` so the server shuts down through its normal path (the same one `main_win.cpp` uses for Ctrl+C, which makes sure the level is saved and the logs are flushed).

The reason a console command such as `/test all` is *not* registered is that under CI the server's stdin is redirected to a file, so console input is never consumed by the command parser — anything written there is silently swallowed (not even an `Unknown command` reply). Driving the suite from an event removes stdin from the loop entirely.

> [!WARNING]
> If the simulated player fails to be created, `simulated player creation failed, tests will not run` is logged and the server **shuts down immediately**, with no gtest output at all. If you see that line, first confirm that the server can spawn simulated players (it needs a world that accepts them and enough memory).

> [!NOTE]
> The tests run on the server thread and some of them touch real world data (for example the wallet and market tables). Always run them on a **dedicated test server**, never on a server you actually use.

## Test directory structure

```txt
tests/
├─ common/                       # cross-platform tests: compiled for both the server and client targets
│   ├─ base/                     # base components: Cache, ScopeGuard, ServiceContainer, Throttle, Wrapper
│   ├─ coro/                     # coroutines: TimerManager tests, MockExecutor (an Executor stub with controllable time)
│   ├─ data/                     # data layer: BlockStore, ConnectionPool, JsonStorage, Payload, WriteBatch
│   ├─ frontend/                 # LCUI frontend: lexer / parser / semantics / optimizer / VM / serializer / LSP / sandbox budgets
│   │                            #   plus the support headers CommonTest.h, MirTestSupport.h and FuzzTest.cpp
│   └─ utils/
│       ├─ I18nUtilsTest.cpp     # internationalisation utilities
│       └─ core/                 # MathUtils, SystemUtils, Sha256
├─ server/                       # server tests: excluded for the client target
│   ├─ mc/                       # vanilla object wrappers: BlockUtils, CommandUtils, InventoryUtils, ScoreboardUtils
│   ├─ modules/
│   │   ├─ CallbackUtilsTest.cpp # LOICollectionAPI variable registration
│   │   └─ Plugins/              # per-plugin integration tests (Blacklist, Cdk, Chat, Language, Market, Menu,
│   │                            #   Mute, Notice, Pvp, Shop, Statistics, Tpa, Wallet, BehaviorEvent, ...)
│   ├─ TestMain.cpp              # test entry point: listens for ServerStartedEvent and runs automatically
│   ├─ TestSimulatedPlayer.h     # simulated-player wrapper
│   └─ TestSimulatedPlayer.cpp
└─ client/                       # client tests: currently empty, reserved only
```

Responsibilities per directory:

| Directory | Responsibility | Build scope |
| --- | --- | --- |
| `tests/common/base` | Platform-independent base components | server + client |
| `tests/common/coro` | Coroutines and timers (including `MockExecutor`) | server + client |
| `tests/common/data` | Data layer: connection pool, block store, JSON storage, write batches | server + client |
| `tests/common/frontend` | The whole LCUI language frontend | server + client |
| `tests/common/utils` | General utilities and hashing | server + client |
| `tests/server/mc` | Vanilla object wrappers | server only |
| `tests/server/modules` | Module and plugin integration tests | server only |
| `tests/client` | Client tests (reserved) | client only |

> [!WARNING]
> The `tests/client/` directory currently contains **no files at all**. The `remove_files("tests/client/**.cpp")` clause in `xmake.lua` is a switch reserved for future client tests. Do not assume in documentation or scripts that this directory holds any tests.

> [!NOTE]
> `tests/**` is only pulled in for debug builds (`xmake.lua`: `if is_mode("debug") then add_packages("gtest") add_files("tests/**.cpp") add_includedirs("tests")`). Release builds use `remove_files("tests/**.cpp")` to guarantee that no test code reaches a published artifact.

## Adding a test

### 1. Pick the file location by layer

- Platform-independent logic → `tests/common/<layer>/`
- Code that depends on vanilla objects or the server runtime → `tests/server/<layer>/`
- Integration tests for one plugin → `tests/server/modules/Plugins/`

### 2. Naming conventions

- File names end in `Test.cpp` and match the source file, e.g. `src/LOICollectionA/base/Throttle.h` → `tests/common/base/ThrottleTest.cpp`
- The suite name matches the file name and the case name describes the behaviour:

    ```cpp
    #include <gtest/gtest.h>

    #include "LOICollectionA/base/Throttle.h"

    TEST(ThrottleTest, AllowInitiallyTrue) {
        // ...
    }

    TEST(ThrottleTest, ConsecutiveCallsWithinInterval) {
        // ...
    }
    ```

- Use `TEST_F` when you need a fixture (shared `SetUp` / `TearDown`):

    ```cpp
    class WalletPluginTest : public testing::Test {
    protected:
        void SetUp() override {
            if (!WalletPlugin::getShared()->isValid())
                GTEST_SKIP() << "WalletPlugin is not valid";
        }
    };

    TEST_F(WalletPluginTest, SomeBehaviour) {
        // ...
    }
    ```

- Skip with `GTEST_SKIP()` when a plugin is unavailable instead of failing the case — a plugin may not have run `registry()` yet when the server has not fully started

### 3. Reuse the existing test support

| Header | Provides | Purpose |
| --- | --- | --- |
| `tests/common/frontend/CommonTest.h` | `compile(input, diagnostics)`, `eval(input, ctx)` | Compiles LCUI source all the way to MIR, or evaluates it directly, for language behaviour tests |
| `tests/common/frontend/MirTestSupport.h` | `makeCountdownLoopChunk()`, `makeNestedLoopChunk()`, `makeStraightLineChunk()`, `makeConditionalFallthroughChunk()`, `makeUnconditionalJumpChunk()`, `makeSelfJumpChunk()` | Builds MIR chunks for optimizer and control-flow analysis tests |
| `tests/common/coro/MockExecutor.h` | `MockExecutor` (with `advanceTime(delta)` to move time by hand) | Lets code that depends on `ll::coro::Executor` (such as `TimerManager`) run deterministically in tests |
| `tests/server/TestSimulatedPlayer.h` | `TestSimulatedPlayer` (`create()` / `destroy()` / `getPlayer()`) | Gives a server-side test a real player |

Include paths are rooted at `tests/` (`xmake.lua` supplies this through `add_includedirs("tests")`):

```cpp
#include "common/frontend/CommonTest.h"
#include "common/coro/MockExecutor.h"
#include "server/TestSimulatedPlayer.h"
```

Plugin tests often pair with a plugin's own test hook, such as `WalletPlugin::setOptionsForTest(const Config::C_Wallet&)`: it lets a case temporarily override the plugin configuration so different configurations can be exercised inside one process, and the test restores the value from `GetWalletConfig()` afterwards.

## Key convention: globally unique names under unity builds

`xmake.lua` enables `add_rules("c++.unity_build", { batchsize = 8 })`, so xmake merges **up to eight source files into a single translation unit**. What gets merged is not just the code but also their anonymous namespaces — meaning that two test files each defining a same-named, same-signature function inside `namespace { }` collide outright as soon as they land in the same batch.

The project therefore has one hard rule:

> **Helper functions and helper types in test files must have globally unique names.** Name them after the owning test file plus their purpose (for example `WriteBatchTestMakeTempDb`) rather than relying on an anonymous namespace for isolation.

This is not a theoretical risk but a bug that already happened: commit `ba5278e` (*fix(tests): give each unity-build test file unique helper names*) was needed because `tempDb` / `dropDb` were defined in `ConnectionPoolTest.cpp`, `WriteBatchTest.cpp` and `BlockStoreTest.cpp` at once, which broke the whole `data` batch, while `CreateBlacklistEntry` appeared with an identical signature in four plugin tests and was about to blow up the next batch.

> [!TIP]
> A practical habit when writing a new test: start with the helper in an anonymous namespace, then confirm before committing that the name is unique across all of `tests/`. You can also write the helper as a `static` member or as a free function carrying the file prefix.

> [!NOTE]
> Fixture classes (`class XxxPluginTest : public testing::Test`) are already uniquely named per file and need no extra treatment; `TEST_F` only uses the name as part of a generated class name.

## Fuzzing

`tests/common/frontend/FuzzTest.cpp` folds a **deterministic** fuzz harness into the ordinary gtest suite, with no extra tooling:

- Mutation tests: random byte insertions, deletions, overwrites and splices of corpus sources are fed through the lexer, parser, component expander and semantic analyzer
- Token soup: random token strings are fed straight into the frontend
- Serializer tests: serialized `.lcp` packages are corrupted byte by byte and must be rejected cleanly on deserialization

Every input must terminate, never crash, and stay inside a per-input time budget of `250ms` — so parser DoS regressions fail the suite instead of reaching a live server.

The generators are seeded (for example `0x5DEECE66D`), so a failure is always reproducible. The default round count comes from `constexpr auto kFuzzRounds = std::size_t{2000};` in `FuzzTest.cpp`; to raise the iteration count temporarily, edit that constant and rebuild.

> [!NOTE]
> The harness is intentionally deterministic rather than coverage-guided: the whole suite is compiled into the plugin and driven by `/test all`, which rules out sanitizer/process-based fuzzers such as libFuzzer.

> [!WARNING]
> The existing fuzz harness covers only the single file `tests/common/frontend/FuzzTest.cpp` and its round count is fixed. It does **not** read any environment variable — changing the iteration count requires editing the source constant.

## CI status

> [!WARNING]
> **No CI job executes the test suite.** `.github/workflows/build.yml` only runs `xmake f` and `xmake -y` across the four `server` / `client` × `debug` / `release` combinations, i.e. it **only builds** (the debug combinations do compile `tests/**`, so they catch compilation errors, but they run no test case). The suite currently depends entirely on a developer running `/test all` by hand on a local test server.

So when you submit a test-related change, run a debug build and `/test all` locally yourself and paste the result into the pull request description.
