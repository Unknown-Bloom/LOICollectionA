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
| levilamina | 26.51.6 |
| sqlitecpp | 3.3.3 |
| nlohmann_json | 3.12.0 |
| asio | 1.36.0 |
| gtest | v1.17.0（仅 debug 模式） |
| preloader | 1.15.7 |
| levibuildscript | 最新 |
| imgui | v1.92.9+b（仅 client 目标） |

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

测试基于 gtest 并**在游戏服务器内运行**：debug 模式构建会把 `tests/**` 一并编入插件，部署到 LeviLamina 服务端后，在控制台执行 `/test all` 即可触发整套测试。

```bash
xmake f -m debug --target_type=server -y
xmake -y
```

> [!TIP]
> 测试入口由 `tests/server/TestMain.cpp` 监听 `ServerStartedEvent` 自动触发，测试会在模拟玩家（`TestSimulatedPlayer`）创建后执行。

完整的测试指南——目录结构与各层职责、如何新增测试、必须遵守的 unity build 命名约定、模糊测试与 CI 现状——请参见 [测试指南](./testing.md)。

## 打包发布

### modpacker

`xmake.lua` 通过 `scripts/modpacker.lua` 注册了 `modpacker` 规则，构建时自动整理插件目录（插件文件、`gui`、`lang` 等资源）。

### tooth.json

发布清单 `tooth.json` 定义插件元数据与安装方式：

- `tooth`：发布地址（`github.com/loitietu/LOICollectionA`）
- `version`：插件版本（需与 `xmake.lua` 的 `set_version` 一致）
- `variants`：平台变体与依赖（如 `win-x64` + LeviLamina 26.50.*）
- `assets`：发布资源（Release 中的 zip 包）

### Release 流程

`.github/workflows/release.yml` 在打 tag（如 `v1.17.1`）时自动执行：构建 → 打包 zip → 创建 GitHub Release → 上传 `lip` 安装资源。用户通过 `lip install github.com/loitietu/LOICollectionA` 安装。

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
