# 架构概览

> [!NOTE]
> 以下内容取自 LOICollectionA 1.17.0 的代码结构，对于后续版本可能会有所不同。

LOICollectionA 是一个基于 [LeviLamina](https://github.com/LiteLDev/LeviLamina) 的 C++ 插件（NativeMod），整体采用**微内核架构**：核心只负责配置加载、服务注册与模块调度，所有功能以 **模块（Module）** 为单位独立实现，可在配置文件中按需开关。

## 插件入口与生命周期

插件入口位于 `src/LOICollectionA/LOICollectionA.cpp`，通过 `LL_REGISTER_MOD` 注册：

```cpp
LL_REGISTER_MOD(LOICollection::A, LOICollection::A::getInstance());
```

`A` 继承 LeviLamina 的 `ll::mod::NativeMod`，四个阶段依次驱动所有模块：

| 阶段 | 核心动作 | 对模块的调用 |
| --- | --- | --- |
| `load` | 计算配置版本 → 生成/合并/加载 `config.json` → 注册全局服务 → 初始化数据库目录与语言目录 | 逐个调用模块 `load()` |
| `enable` | 根据 `ConsoleLanguage` 设置默认语言 → 编译 LOICollectionAPI 脚本 → | 逐个调用模块 `registry()` |
| `disable` | — | 逐个调用模块 `unregistry()` |
| `unload` | — | 逐个调用模块 `unload()` |

> [!TIP]
> 模块的四个阶段并非"开关"关系：`load`/`unload` 负责**资源生命周期**（数据库、日志、路径），`registry`/`unregistry` 负责**运行时注册**（命令、事件、UI）。详见 [模块开发指南](./module.md)。

## 模块系统

模块框架位于 `src/LOICollectionA/include/`，包含三个核心类：

### ModuleBase（模块基类）

所有模块继承 `LOICollection::modules::ModuleBase`，需要实现四个纯虚方法：

```cpp
class ModuleBase {
public:
    virtual std::string getName() = 0;                 // 模块名（注册名）
    virtual ModulePriority getPriority() = 0;          // 模块优先级
    virtual ll::Expected<bool> load() = 0;             // 资源加载
    virtual ll::Expected<bool> unload() = 0;           // 资源释放
    virtual ll::Expected<bool> registry() = 0;         // 运行时注册
    virtual ll::Expected<bool> unregistry() = 0;       // 运行时注销
};
```

### AutoRegister（自动注册）

模块只需继承 `modules::AutoRegister<Derived>`，其静态初始化会在程序启动时自动调用 `ModManager::registry(getShared(), name, priority)`，无需手动注册：

```cpp
class BlacklistPlugin : public std::enable_shared_from_this<BlacklistPlugin>,
                        public modules::ModuleBase,
                        public modules::AutoRegister<BlacklistPlugin> {
    // ...
};
```

### ModManager（模块管理器）

| 方法 | 说明 |
| --- | --- |
| `registry(shared_ptr, name, priority)` | 注册模块，`priority` 默认 `Normal` |
| `unregistry(name)` | 注销模块 |
| `getModule(name)` | 按名字获取模块实例 |
| `mods()` | 获取全部模块名列表（按优先级排序） |

`ModulePriority` 枚举：`Highest`（0）、`High`（1）、`Normal`（2）、`Low`（3）、`Lowest`（4）。优先级影响模块的加载/注册顺序。

## 服务容器与依赖注入

模块之间不直接依赖，而是通过 `ServiceProvider` / `ServiceContainer` 按 **类型 + 名字** 注册与获取服务：

```cpp
// 注册
ServiceProvider::getInstance().registerInstance<TService>(instance, name);

// 获取
auto svc = ServiceProvider::getInstance().getService<TService>(name);
```

插件启动时注册的全局服务：

| 类型 | 名字 | 说明 |
| --- | --- | --- |
| `ReadOnlyWrapper<Config::C_Config>` | `"Config"` | 只读配置（见下） |
| `std::string` | `"DataPath"` | 插件数据目录（`plugins/LOICollectionA/data`） |
| `std::string` | `"GuiPath"` | GUI 目录（`plugins/LOICollectionA/gui`） |
| `std::string` | `"ConfigPath"` | 配置目录（`plugins/LOICollectionA/config`） |
| `SQLiteStorage` | `"SettingsDB"` | 全局设置数据库（`data/settings.db`） |

> [!NOTE]
> 配置以 `ReadOnlyWrapper<Config::C_Config>` 注册，模块只能**读取**配置，无法修改。这是有意设计：配置只在启动时读取一次，运行期修改需要重启服务器。

## 配置系统

- 配置结构定义在 `src/LOICollectionA/ConfigPlugin.h`，共 105 个配置键（嵌套结构 `C_Config` → `C_ServerConfig` → `C_ServerPlugins` / `C_ServerProtableTool`）
- 配置版本由插件版本号哈希生成（`SynchronousPluginConfigVersion`）
- 升级插件时，代码中的默认配置通过 `MergePatch` **递归合并**进现有 `config.json`：新增键自动补入，已有键保留用户取值
- 详细配置项说明见 [数据文件](../md/data.md)

## 目录结构

```txt
src/LOICollectionA/
├─ LOICollectionA.cpp / .h   # 插件入口（A 类）
├─ ConfigPlugin.h / .cpp     # 配置结构定义与加载
├─ base/                     # 基础设施：ServiceContainer、ServiceProvider、
│                            #   ReadOnlyWrapper、LRUKCache、Throttle、ScopeGuard
├─ data/                     # 数据层：SQLiteStorage（SQLite 连接池）、JsonStorage
├─ frontend/                 # LCUI 脚本引擎：Lexer、Parser、SemanticAnalyzer、
│   │                        #   AST、Callback（原生绑定注册表）、ir/（编译器、VM）
│   ├─ sandbox/              # 脚本沙箱：ScriptPermission（权限与执行预算配置）、
│   │                        #   SandboxBudget（燃料计量）、CapabilityGate、SandboxReport
│   ├─ lsp/                  # LCUI 语言服务核心：Protocol（JSON-RPC 分帧）、
│   │                        #   Analysis（诊断与符号）、LanguageServer（分发）
│   └─ builtin/              # 脚本内置实现：Math/Format/String 函数、
│                            #   mc/server 命令、ui/（表单与 Observable 原生类）
├─ include/                  # 对外公开头文件（安装后随插件分发）
│   ├─ ModuleBase.h / ModManager.h / ModulePriority.h
│   ├─ CallbackUtils.h       # LOICollectionAPI 变量注册入口
│   ├─ form/GUIManager.h     # GUI 管理器
│   ├─ server/Events/        # 自定义事件类型（网络包、玩家计分板变更等）
│   └─ server/Plugins/       # 各模块公开接口（BlacklistPlugin.h 等）
├─ modules/                  # 模块实现（client/ 与 server/ 按平台隔离）
├─ utils/                    # 工具：I18nUtils、MathUtils、mc-server 工具集
tests/                       # gtest 测试（common/ 跨平台、server/、client/）
```

## 双平台支持

插件同时支持 `server`（服务端）与 `client`（客户端）两种目标，通过 xmake 的 `target_type` 选项区分：

- 编译宏：`LL_PLAT_S`（服务端）/ `LL_PLAT_C`（客户端）
- 源码隔离：`modules/server/*` 与 `modules/client/*` 按目标互斥编译
- 头文件隔离：`include/server/*` 与 `include/client/*` 按目标从分发清单中移除
- 构建方式详见 [构建与测试](./build.md)

## 数据层

所有持久化数据通过 `SQLiteStorage`（默认，支持读写连接池与事务）或 `JsonStorage`（简单 JSON 文件）访问，二者都返回 `ll::Expected<T>` 以支持链式错误处理。用法详见 [模块开发指南](./module.md) 的"数据层"章节。
