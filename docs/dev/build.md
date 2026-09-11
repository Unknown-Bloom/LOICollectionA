# 构建与测试

本文介绍如何从源码构建 LOICollectionA、运行测试与打包发布。

## 环境要求

- [xmake](https://github.com/xmake-io/xmake) 3.0+
- `clang-cl` 工具链（LLVM + MSVC 环境）
- Git

首次构建前先更新 xmake 仓库：

```bash
xmake repo -u
```

项目依赖（由 xmake 自动下载）：

| 依赖 | 版本 |
| --- | --- |
| levilamina | 26.20.7 |
| sqlitecpp | 3.3.3 |
| nlohmann_json | 3.12.0 |
| gtest | v1.17.0（仅 debug 模式） |
| preloader | 1.15.7 |
| levibuildscript | 最新 |

## 构建

### 基础构建

```bash
# 克隆并进入仓库
git clone https://github.com/loitietu/LOICollectionA.git
cd LOICollectionA

# 默认配置构建（release，服务端）
xmake

# 构建 debug（会一并编译测试代码）
xmake f -m debug
xmake
```

### 构建选项

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `-m debug\|release` | `release` | 构建模式；debug 模式编译 gtest 测试 |
| `--target_type=server\|client` | `server` | 目标平台；client 用于客户端侧插件 |
| `--shared=y\|n` | `y` | 是否构建共享库（影响 SQLite 等依赖） |

```bash
# 清理后重新配置
xmake f -c
xmake f -m debug --target_type=server
xmake
```

### 构建产物

产物为 `shared` 库（Windows 下为 `LOICollectionA.dll`），配合 modpacker 规则打包（见下文）。编译宏 `LL_PLAT_S`（服务端）/`LL_PLAT_C`（客户端）由 `target_type` 自动定义，源码中的 `modules/server/*` 与 `modules/client/*` 按目标互斥编译。

> [!NOTE]
> 项目使用 `c++.unity_build`（批大小 8）加速编译，并启用了 `Global.h` 预编译头。修改头文件后重编译可能较慢，属正常现象。

## 测试

测试基于 gtest，**在游戏服务器内运行**：

1. debug 模式构建（`xmake f -m debug`），`tests/**` 自动编入
2. 将构建出的插件部署到 LeviLamina 服务端并启动
3. 在服务器控制台执行 `/test all`，gtest 将自动运行并输出结果

> [!TIP]
> `/test all` 命令由 `tests/server/TestCommand.cpp` 在服务器线程启动后通过 hook 注册，测试会在模拟玩家（`TestSimulatedPlayer`）创建后执行。

### 测试目录结构

```txt
tests/
├─ common/          # 跨平台测试：base（Cache/ServiceContainer/Wrapper 等）、
│                   #   coro、data（Json/SQLite 存储）、frontend（LCUI 词法/语法/语义/VM）
├─ server/          # 服务端测试：mc（方块/计分板等工具）、modules（插件回调）、
│                   #   TestCommand.cpp（入口）、TestSimulatedPlayer（模拟玩家）
└─ client/          # 客户端测试（仅 client 目标编译）
```

### 模糊测试

`tests/common/frontend/FuzzTest.cpp` 把一套**确定性**的模糊测试并入常规 gtest，无需额外工具链。它会把变异后的源码与随机 token 串喂给词法/语法分析器、组件展开器与语义分析器，并把序列化后的 `.lcp` 包按字节破坏后重新反序列化。每个输入都必须终止、不崩溃，且不超过单个输入 250ms 的时间预算——因此解析器 DoS 类回归会直接让测试失败，而不是带到线上服务器。

生成器带固定种子，失败必然可复现。本地想加大迭代次数：

```bash
LOICOLLECTION_A_FUZZ_ROUNDS=20000
```

> [!NOTE]
> 这里刻意使用确定性生成而非覆盖率引导：整套测试被编译进插件并由 `/test all` 驱动，无法使用 libFuzzer 这类依赖 sanitizer 与独立进程的方案。

## 打包发布

### modpacker

`xmake.lua` 通过 `scripts/modpacker.lua` 注册了 `modpacker` 规则，构建时自动整理插件目录（插件文件、`gui`、`lang` 等资源）。

### tooth.json

发布清单 `tooth.json` 定义插件元数据与安装方式：

- `tooth`：发布地址（`github.com/loitietu/LOICollectionA`）
- `version`：插件版本（需与 `xmake.lua` 的 `set_version` 一致）
- `variants`：平台变体与依赖（如 `win-x64` + LeviLamina 26.20.*）
- `assets`：发布资源（Release 中的 zip 包）

### Release 流程

`.github/workflows/release.yml` 在打 tag（如 `v1.15.0`）时自动执行：构建 → 打包 zip → 创建 GitHub Release → 上传 `lip` 安装资源。用户通过 `lip install github.com/loitietu/LOICollectionA` 安装。

## 代码规范

仓库自带以下配置，提交前请保持：

| 文件 | 作用 |
| --- | --- |
| `.clang-format` | 代码格式化（clang-format 15+） |
| `.clang-tidy` | 静态检查规则 |
| `.clangd` | clangd 语言服务器配置 |

其他约定：

- `Global.h` 为预编译头，集中包含基础库（ll/api、base/ 工具），新增公共头文件可考虑加入
- 编译选项 `set_exceptions("none")` + `/EHa`、`/utf-8`、`/permissive-`，请勿依赖 C++ 异常
- 平台相关代码放入 `modules/server/*` 或 `modules/client/*`，公开头文件放入 `include/server/*` 或 `include/client/*`
- 对外 API 使用 `LOICOLLECTION_A_API` / `LOICOLLECTION_A_NDAPI` 导出宏
