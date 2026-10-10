# 测试指南

本文介绍 LOICollectionA 的测试套件如何接线、如何运行、如何新增测试，以及在 `c++.unity_build` 下必须遵守的命名约定。构建相关内容请参见 [构建与测试](./build.md)。

## 设计取向

测试基于 [GoogleTest](https://github.com/google/googletest)，但**不通过独立的测试可执行文件运行**，而是把整套测试编译进插件，在真实的游戏服务器进程里执行：

- 只有 `debug` 模式才会编入并链接 gtest 与 `tests/**`（见 `xmake.lua` 的 `is_mode("debug")` 分支）
- 测试入口由 `tests/server/TestMain.cpp` 在服务器线程上自动触发，测试代码因此可以直接访问 `Level`、`Player`、`BlockSource` 等运行时对象
- 这套做法也解释了为什么不能使用 libFuzzer 这类依赖独立进程与 sanitizer 的方案，见 [模糊测试](#模糊测试)

## 运行测试

### 1. 以 debug 模式构建服务端目标

```bash
xmake f -m debug --target_type=server -y
xmake -y
```

`--target_type=server` 会编入 `tests/common/**` 与 `tests/server/**`，并排除 `tests/client/**`；`--target_type=client` 则相反。

### 2. 部署到 LeviLamina 服务端

把构建产物（含 gtest 的 `LOICollectionA.dll` 与资源目录）按 [构建与测试](./build.md) 的说明放入服务端的 `plugins/LOICollectionA/`，然后启动服务器。

### 3. 启动服务器，测试会自动运行

服务器加载世界并触发 `ServerStartedEvent` 后，测试套件会**自动**在服务器线程上开跑，无需任何手动操作：

```log
[TestMain] ---------------------------------------- iteration 1
[TestMain] ---------------------------------------- suite WalletPluginTest (3 tests)
[TestMain] RUN      WalletPluginTest.TransferMovesBalance
[TestMain] OK       WalletPluginTest.TransferMovesBalance (0 ms)
[TestMain] done: 3 passed, 0 skipped, 0 disabled
```

`TestMain.cpp` 通过 `ll::event::EventBus` 监听 `ServerStartedEvent`：拿到事件后在 `ServerThreadExecutor` 上调用 `testing::InitGoogleTest()`，换上仿照官方 `GTestMain.cpp` 的 `LLTestEventListener`（因此输出走 LL Logger，而不是 gtest 默认打印器），创建模拟玩家、执行 `RUN_ALL_TESTS()` 并销毁模拟玩家，最后调用 `ll::service::getDedicatedServer()->stop()` 让服务器走正常流程关闭。

完成标记是 `done: N passed, ...`（`OnTestProgramEnd` 输出，全过时不含 `failed` 字段）。

之所以不注册 `/test all` 这样的控制台命令，是因为在 CI 里服务器的 stdin 被重定向到文件，控制台输入不会被命令解析器消费——写进去的命令会被静默吞掉（连 `Unknown command` 都不会回）。改由事件驱动后就彻底绕开了 stdin。

> [!WARNING]
> 模拟玩家创建失败时会打印 `simulated player creation failed, tests will not run`，随后**直接停服**，gtest 不会有任何输出。如果日志里出现这一行，请先确认服务端能正常生成模拟玩家（需要允许模拟玩家加入的世界与足够的内存）。

> [!NOTE]
> 测试运行在服务器线程上，且部分测试会操作真实世界数据（例如钱包、市场的数据表）。请务必在**测试专用服务端**上运行。

## 测试目录结构

```txt
tests/
├─ common/                       # 跨平台测试：server 与 client 目标都会编译
│   ├─ base/                     # 基础组件：Cache、ScopeGuard、ServiceContainer、Throttle、Wrapper
│   ├─ coro/                     # 协程：TimerManager 测试、MockExecutor（可控时间的 Executor 桩）
│   ├─ data/                     # 数据层：BlockStore、ConnectionPool、JsonStorage、Payload、WriteBatch
│   ├─ frontend/                 # LCUI 前端：词法 / 语法 / 语义 / 优化 / VM / 序列化 / LSP / 沙箱预算
│   │                            #   以及测试支撑头 CommonTest.h、MirTestSupport.h、FuzzTest.cpp
│   └─ utils/
│       ├─ I18nUtilsTest.cpp     # 国际化工具
│       └─ core/                 # MathUtils、SystemUtils、Sha256
├─ server/                       # 服务端测试：client 目标会排除
│   ├─ mc/                       # 原版工具封装：BlockUtils、CommandUtils、InventoryUtils、ScoreboardUtils
│   ├─ modules/
│   │   ├─ CallbackUtilsTest.cpp # LOICollectionAPI 变量注册
│   │   └─ Plugins/              # 逐个插件的集成测试（Blacklist、Cdk、Chat、Language、Market、Menu、
│   │                            #   Mute、Notice、Pvp、Shop、Statistics、Tpa、Wallet、BehaviorEvent……）
│   ├─ TestMain.cpp              # 测试入口：监听 ServerStartedEvent 自动运行
│   ├─ TestSimulatedPlayer.h     # 模拟玩家封装
│   └─ TestSimulatedPlayer.cpp
└─ client/                       # 客户端测试：目前为空，仅作预留
```

各目录职责对照：

| 目录 | 职责 | 编译范围 |
| --- | --- | --- |
| `tests/common/base` | 与平台无关的基础组件 | server + client |
| `tests/common/coro` | 协程与定时器（含 `MockExecutor`） | server + client |
| `tests/common/data` | 数据层：连接池、块存储、JSON 存储、写事务 | server + client |
| `tests/common/frontend` | LCUI 语言前端全流程 | server + client |
| `tests/common/utils` | 通用工具与哈希 | server + client |
| `tests/server/mc` | 原版对象工具封装 | 仅 server |
| `tests/server/modules` | 模块与插件的集成测试 | 仅 server |
| `tests/client` | 客户端测试（预留） | 仅 client |

> [!WARNING]
> `tests/client/` 目录当前**不含任何文件**，`xmake.lua` 中的 `remove_files("tests/client/**.cpp")` 是为将来的客户端测试预留的开关。请不要在文档或脚本中假设该目录下有测试。

> [!NOTE]
> `tests/**` 只在 debug 模式引入（`xmake.lua`：`if is_mode("debug") then add_packages("gtest") add_files("tests/**.cpp") add_includedirs("tests")`）。release 模式通过 `remove_files("tests/**.cpp")` 保证测试代码完全不进入发布产物。

## 新增测试

### 1. 按层次选择文件位置

- 与平台无关的逻辑 → `tests/common/<layer>/`
- 依赖原版对象或服务端运行时 → `tests/server/<layer>/`
- 针对某个插件的集成测试 → `tests/server/modules/Plugins/`

### 2. 命名约定

- 文件名以 `Test.cpp` 结尾，且与源文件同名，例如 `src/LOICollectionA/base/Throttle.h` → `tests/common/base/ThrottleTest.cpp`
- 测试套件名与文件名一致，用例名描述行为：

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

- 需要夹具（共享 `SetUp` / `TearDown`）时使用 `TEST_F`：

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

- 插件不可用时用 `GTEST_SKIP()` 跳过，而不是让用例失败——插件在服务器未完全启动时可能尚未 `registry()`

### 3. 复用现有的测试支撑

| 头文件 | 提供 | 用途 |
| --- | --- | --- |
| `tests/common/frontend/CommonTest.h` | `compile(input, diagnostics)`、`eval(input, ctx)` | 把 LCUI 源码一路编译成 MIR / 直接求值，用于语言行为测试 |
| `tests/common/frontend/MirTestSupport.h` | `makeCountdownLoopChunk()`、`makeNestedLoopChunk()`、`makeStraightLineChunk()`、`makeConditionalFallthroughChunk()`、`makeUnconditionalJumpChunk()`、`makeSelfJumpChunk()` | 构造 MIR 块，用于优化器与控制流分析测试 |
| `tests/common/coro/MockExecutor.h` | `MockExecutor`（`advanceTime(delta)` 手动推进时间） | 让依赖 `ll::coro::Executor` 的代码（如 `TimerManager`）在测试里确定性地跑 |
| `tests/server/TestSimulatedPlayer.h` | `TestSimulatedPlayer`（`create()` / `destroy()` / `getPlayer()`） | 在服务端测试里获得一个真实玩家 |

包含路径以 `tests/` 为根（`xmake.lua` 通过 `add_includedirs("tests")` 提供）：

```cpp
#include "common/frontend/CommonTest.h"
#include "common/coro/MockExecutor.h"
#include "server/TestSimulatedPlayer.h"
```

插件测试常配合各插件自己的测试钩子，例如 `WalletPlugin::setOptionsForTest(const Config::C_Wallet&)`：它允许用例临时改写插件配置，从而在同一个进程里测试不同配置下的行为，测试结束后再恢复为 `GetWalletConfig()` 的取值。

## 关键约定：unity build 下的全局唯一命名

`xmake.lua` 启用了 `add_rules("c++.unity_build", { batchsize = 8 })`，xmake 会把**最多 8 个源文件合并进同一个翻译单元**。合并的不只是代码，还有它们的匿名命名空间——也就是说，两个测试文件里各自写在 `namespace { }` 中的同名同签名函数，一旦落入同一批次就会直接冲突。

因此本项目有一条硬性约定：

> **测试文件里的辅助函数与辅助类型必须取全局唯一的名字**，推荐用「测试文件名 + 用途」命名（如 `WriteBatchTestMakeTempDb`），不要依赖匿名命名空间来隔离。

这不是理论风险，而是已经踩过的坑：commit `ba5278e`（*fix(tests): give each unity-build test file unique helper names*）就是因为 `tempDb` / `dropDb` 同时定义在 `ConnectionPoolTest.cpp`、`WriteBatchTest.cpp` 与 `BlockStoreTest.cpp` 中而让 `data` 批次整体编译失败，另有 `CreateBlacklistEntry` 以相同签名出现在四个插件测试里，即将在下个批次引爆。

> [!TIP]
> 编写新测试时的实用做法：先写匿名命名空间的辅助函数，再在提交前确认该名字在整个 `tests/` 下唯一。也可以直接把辅助逻辑写成 `static` 成员或带文件前缀的自由函数。

> [!NOTE]
> 夹具类（`class XxxPluginTest : public testing::Test`）本身就是按文件唯一命名的，不需要额外处理；`TEST_F` 只把它当作类名的一部分。

## 模糊测试

`tests/common/frontend/FuzzTest.cpp` 把一套**确定性**的模糊测试并入常规 gtest，无需额外工具链：

- 变异测试：把语料库中的源码做随机字节插入 / 删除 / 覆写 / 拼接，喂给词法、语法、组件展开与语义分析
- token 汤：把随机 token 串直接喂给前端
- 序列化测试：把序列化后的 `.lcp` 包按字节破坏后重新反序列化，必须干净地拒绝

每个输入都必须终止、不崩溃，且不超过 `250ms` 的单输入时间预算——因此解析器 DoS 类回归会直接让测试失败，而不是带到线上服务器。

生成器带固定种子（如 `0x5DEECE66D`），失败必然可复现。默认轮数由 `FuzzTest.cpp` 中的 `constexpr auto kFuzzRounds = std::size_t{2000};` 决定；想临时加大迭代次数，直接修改该常量后重新构建即可。

> [!NOTE]
> 模糊测试刻意使用确定性生成而非覆盖率引导：整套测试被编译进插件并由 `/test all` 驱动，无法使用 libFuzzer 这类依赖 sanitizer 与独立进程的方案。

> [!WARNING]
> 现有的模糊测试只覆盖 `tests/common/frontend/FuzzTest.cpp` 一个文件，且轮数固定。它**不**读取任何环境变量——修改迭代次数需要改动源码常量。

## 持续集成现状

> [!WARNING]
> **没有任何 CI 任务会执行测试套件。** `.github/workflows/build.yml` 只在 `server` / `client` × `debug` / `release` 四种组合下执行 `xmake f` 与 `xmake -y`，也就是**只做编译**（debug 组合会连带编译 `tests/**`，因此能挡住编译错误，但不会运行任何用例）。测试目前完全依赖开发者在本地的测试服务端上手动执行 `/test all`。

因此，提交与测试有关的改动时，请自行在本地完成一次 debug 构建并运行 `/test all`，并在 PR 描述中贴出结果。
