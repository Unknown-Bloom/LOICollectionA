# 模块开发指南

> [!NOTE]
> 以下内容以 `BlacklistPlugin`（黑名单模块）为示例，取自 LOICollectionA 1.17.0 的代码结构，对于后续版本可能会有所不同。

本文介绍如何为 LOICollectionA 开发一个 C++ 模块。在开始之前，请先阅读 [架构概览](./architecture.md) 了解模块框架与服务容器。

## 创建模块

一个模块由**公开头文件**（`include/server/Plugins/XxxPlugin.h`）与**实现文件**（`modules/server/Plugins/XxxPlugin.cpp`）组成。

### 1. 定义公开类

```cpp
// include/server/Plugins/XxxPlugin.h
#pragma once

#include <memory>
#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

namespace LOICollection::server::Plugins {
    class XxxPlugin : public std::enable_shared_from_this<XxxPlugin>,
                      public modules::ModuleBase,
                      public modules::AutoRegister<XxxPlugin> {
    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<XxxPlugin> getShared(); // 单例

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;                 // 模块名
        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override; // 优先级

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        XxxPlugin();

        struct Impl;                                    // Pimpl 惯用法
        std::unique_ptr<Impl> mImpl;
    };
}
```

> [!TIP]
> `LOICOLLECTION_A_API` / `LOICOLLECTION_A_NDAPI` 是导出宏（Windows 上为 `__declspec(dllexport/dllimport)`），公开方法必须标注，否则外部链接时找不到符号。

### 2. 实现单例与生命周期

```cpp
// modules/server/Plugins/XxxPlugin.cpp
namespace LOICollection::server::Plugins {
    struct XxxPlugin::Impl {
        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<ll::io::Logger> logger;
        ReadOnlyWrapper<Config::C_Xxx> options;   // 模块配置（只读）
        std::filesystem::path dataPath;
        std::atomic<bool> mRegistered{ false };   // 注册状态标记
    };

    std::shared_ptr<XxxPlugin> XxxPlugin::getShared() {
        static std::shared_ptr<XxxPlugin> instance(new XxxPlugin());
        return instance;
    }

    std::string XxxPlugin::getName() { return "Xxx"; }

    modules::ModulePriority XxxPlugin::getPriority() { return modules::ModulePriority::Normal; }

    ll::Expected<bool> XxxPlugin::load() {
        // 模块未启用时不初始化（load 仍会被调用，但只做最少的资源准备）
        auto& config = ServiceProvider::getInstance()
            .getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get();
        if (!config.ServerConfig.Plugins.Xxx.ModuleEnabled)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance()
            .getService<std::string>("DataPath")->data());
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = config.ServerConfig.Plugins.Xxx;  // 拷贝模块配置（只读）

        // 初始化模块自己的数据库
        this->mImpl->db = std::make_shared<SQLiteStorage>((mDataPath / "xxx.db").string());
        return true;
    }

    ll::Expected<bool> XxxPlugin::registry() {
        // 模块未启用时直接跳过（不注册命令/事件/UI）
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        // 建表 -> 注册 UI -> 注册命令与事件
        return this->mImpl->db->create("Xxx", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("id");
            ctor("name");
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();
            this->mImpl->mRegistered.store(true, std::memory_order_release);
            return true;
        });
    }

    ll::Expected<bool> XxxPlugin::unregistry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->unlistenEvent();
        return this->mImpl->db->exec("VACUUM;")
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);
                return true;
            });
    }

    ll::Expected<bool> XxxPlugin::unload() {
        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};
        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();
        return true;
    }
}
```

### 3. 生命周期各阶段职责

| 阶段 | 职责 | 注意事项 |
| --- | --- | --- |
| `load` | 获取服务、初始化数据库与日志、缓存路径 | 只做资源准备，不注册任何运行时行为 |
| `registry` | 检查 `ModuleEnabled` → 建表 → 注册 UI/命令/事件 | 插件未启用时返回 `false` 跳过 |
| `unregistry` | 注销事件、清理数据库（如 `VACUUM`） | 与 registry 严格对应 |
| `unload` | 释放所有资源 | 若仍处于注册状态需先注销 |

> [!WARNING]
> `load` 在每次插件加载时都会执行，`registry` 只在插件 `enable` 时执行。如果模块未启用（`ModuleEnabled: false`），`registry` 返回 `false`，但 `load` 仍然会执行——不要在 `load` 中注册命令或事件。

## 读取配置

配置通过 `ReadOnlyWrapper<Config::C_Config>` 服务获取，只能读取：

```cpp
auto& config = ServiceProvider::getInstance()
    .getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get();
bool enabled = config.ServerConfig.Plugins.Mute;  // 读取 Mute 模块开关

// 在 load 中通常把模块自己的配置子结构拷贝出来
this->mImpl->options = config.ServerConfig.Plugins.Blacklist;
```

### 新增配置项

在 `src/LOICollectionA/ConfigPlugin.h` 对应的结构体中添加成员即可，**必须提供默认值**：

```cpp
struct C_Xxx {
    bool ModuleEnabled = false;       // 模块开关（约定俗成第一个字段）
    std::string TargetScoreboard = "money";
    int Limit = 10;
};
```

插件下次启动时，`MergePatch` 会自动把新字段合并进玩家的 `config.json`，已有字段保留。

> [!WARNING]
> 配置键名只能使用 `英文`、`数字` 与 `下划线`，请勿使用中文。

## 数据层

### SQLiteStorage（推荐）

基于 SQLite 的连接池封装，所有方法返回 `ll::Expected<T>`：

| 方法 | 说明 |
| --- | --- |
| `create(table, callback)` | 建表，回调中逐个调用 `ctor("列名")` 声明列 |
| `set(table, key, values)` | 写入一行（key 为主键，values 为列名→值映射） |
| `get(table, key)` | 读取一行，返回 `列名→值` 映射 |
| `get(table, key, column, default)` | 读取单列 |
| `find(table, conditions, match)` | 按条件查询，返回匹配的 key 列表（`FindCondition::AND/OR`） |
| `has(table, key)` | 判断是否存在 |
| `del(table, key)` | 删除一行 |
| `list(table)` | 列出所有 key |
| `exec(sql)` | 执行原生 SQL（如 `"VACUUM;"`） |

```cpp
// 写入
this->db->set("Xxx", uuid, {
    { "name", player.getRealName() },
    { "time", std::to_string(time) }
});

// 查询（OR 条件）
auto id = this->db->find("Xxx", {
    { "data_uuid", uuid },
    { "data_ip", ip }
}, "", SQLiteStorage::FindCondition::OR);

// 链式处理错误
this->db->get("Xxx", id)
    .and_then([this](std::unordered_map<std::string, std::string> data) -> ll::Expected<void> {
        // ...
        return {};
    })
    .or_else(modules::defaultErrorHandler<XxxPlugin>);
```

> [!TIP]
> 需要事务时使用 `SQLiteStorageTransaction::create(storage)`，通过 `commit()` / `rollback()` 控制。

### JsonStorage（简单 JSON 文件）

适合小规模配置类数据：

```cpp
JsonStorage storage("path/to/file.json");
storage.load().or_else(...);          // 加载文件
storage.set("key", value);            // 写内存
storage.save();                       // 落盘
auto v = storage.get<std::string>("key"); // ll::Expected<T>
```

支持 `get_ptr` / `set_ptr`（JSON Pointer 路径）与 `remove` / `has` / `keys`。

## 注册命令

使用 LeviLamina 的 `ll::command` 体系：

```cpp
void XxxPlugin::registeryCommand() {
    ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
        .getOrCreateCommand("xxx", "LOICollection -> xxx", CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);

    command.overload<operation>().text("add").required("Target").optional("Time").execute(
        [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
            CommandSelectorResults<Player> results = param.Target.results(origin);
            if (results.empty())
                return output.error("target not found");

            for (Player*& pl : results) {
                this->addXxx(*pl, param.Time).or_else(modules::defaultErrorHandler<XxxPlugin>);
                output.success("done");
            }
        });
}
```

其中 `operation` 是模块内定义的命令参数结构体（参考 BlacklistPlugin 的 `struct operation`）。

## 监听事件

使用 LeviLamina 的 `ll::event::EventBus`，可以监听原版事件与模块自定义事件（`include/server/Events/*`）：

```cpp
void XxxPlugin::listenEvent() {
    ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
    this->mImpl->myListener = eventBus.emplaceListener<LOICollection::server::Events::PlayerScoreChangedEvent>(
        [this](LOICollection::server::Events::PlayerScoreChangedEvent& event) -> void {
            // ...
        });
}

void XxxPlugin::unlistenEvent() {
    ll::event::EventBus::getInstance().removeListener(this->mImpl->myListener);
}
```

> [!WARNING]
> 监听器句柄必须保存在 `Impl` 中，`unregistry` / `unload` 时务必 `removeListener`，否则卸载模块后事件仍会触发已释放的对象。

## 错误处理约定

模块公开方法统一返回 `ll::Expected<T>`，配合**模块错误码**：

```cpp
enum class XxxPluginErrorCode : int {
    Invalid = 1,
    NotFound = 2,
    PermissionDenied = 3
};

struct XxxPluginErrorCategory : std::error_category {
    [[nodiscard]] const char* name() const noexcept override { return "XxxPluginError"; }
    [[nodiscard]] std::string message(int ev) const override {
        switch (static_cast<XxxPluginErrorCode>(ev)) {
            case XxxPluginErrorCode::Invalid: return "Plugin is invalid";
            case XxxPluginErrorCode::NotFound: return "Data not found";
            default: return "Unknown";
        }
    }
};

// 使用
return ll::makeErrorCodeError(XxxPlugin::makeErrorCode(XxxPluginErrorCode::NotFound));
```

模块内链式调用失败时，用模板函数 `modules::defaultErrorHandler<XxxPlugin>` 统一输出到模块日志。

## 集成 GUI

模块可通过 `GUIManager` 加载并执行 `.lcui` 脚本（详见 [原生 UI（Native UI）](../md/native-ui.md) 与 [LCUI 脚本语法](../md/lcui.md)）：

```cpp
auto& gui = LOICollection::form::GUIManager::getInstance();
gui.load("xxx", (guiPath / "xxx.lcui").string());   // 加载并编译脚本
gui.execute("xxx");                                  // 执行脚本注册表单
gui.open("xxx", "formId", GUIManagerType::CustomForm, player);
```

GUIManager 还提供 `registerValue` / `registerRequest` / `registerCallback`，供 C++ 侧与脚本表单双向通信。
