# 架构概览

> [!NOTE]
> 以下内容取自 LOICollectionA 1.17.1 的代码结构，对于后续版本可能会有所不同。

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

所有模块继承 `LOICollection::modules::ModuleBase`，需要实现六个纯虚方法：

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
| `BlockRepository` | `"SettingsDB"` | 全局设置数据库（`data/settings.db`，块存储） |

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
├─ data/                     # 数据层：sqlite/ 块存储（BlockStore + WriteBatch +
│                            #   BlockRepository）、JsonStorage
├─ frontend/                 # LCUI 脚本引擎：Lexer、Parser、SemanticAnalyzer、
│   │                        #   AST、Callback（原生绑定注册表）、ir/（编译器、VM、优化器）
│   ├─ sandbox/              # 脚本沙箱：ScriptPermission（权限与执行预算配置）、
│   │                        #   SandboxBudget（燃料计量）、SandboxReport；权限采用信任级 + 能力两层模型
│   ├─ storage/              # 脚本持久化存储：storage（按脚本隔离）与 playerdata
│   │                        #   （按玩家 + 脚本隔离），负责路径、能力校验与整文件缓存
│   ├─ stdlib/               # 原生值与函数：Math/Format、String/Array/Map、
│   │                        #   Observable 与 UIRawMessage 等
│   ├─ lsp/                  # LCUI 语言服务核心：Protocol（JSON-RPC 分帧）、
│   │                        #   Analysis（诊断与符号）、LanguageServer（分发）、
│   │                        #   Server（可选的 TCP 传输）
│   └─ builtin/              # 脚本内置命名空间：mc/server 命令、storage、
│                            #   ui/（表单与 GUIManager 原生类）
├─ include/                  # 对外公开头文件（安装后随插件分发）
│   ├─ ModuleBase.h / ModManager.h / ModulePriority.h
│   ├─ CallbackUtils.h       # LOICollectionAPI 变量注册入口
│   ├─ form/GUIManager.h     # GUI 管理器
│   ├─ server/Events/        # 自定义事件类型（详见 [自定义事件](./events.md)）
│   └─ server/Plugins/       # 各模块公开接口（BlacklistPlugin.h 等）
├─ modules/                  # 模块实现（client/ 与 server/ 按平台隔离）
├─ utils/                    # 工具：I18nUtils、MathUtils、mc-server 工具集
tests/                       # gtest 测试（common/ 跨平台、server/ 仅服务端、client/ 预留为空）
```

## 双平台支持

插件同时支持 `server`（服务端）与 `client`（客户端）两种目标，通过 xmake 的 `target_type` 选项区分：

- 编译宏：`LL_PLAT_S`（服务端）/ `LL_PLAT_C`（客户端）
- 源码隔离：`modules/server/*` 与 `modules/client/*` 按目标互斥编译
- 头文件隔离：`include/server/*` 与 `include/client/*` 按目标从分发清单中移除
- 构建方式详见 [构建与测试](./build.md)

## 数据层

数据层自下而上分四层，业务代码只会碰到最上面的 `TypedTable`：

```txt
TypedTable<E, Version>        # 编译期列名 + 版本化表结构（业务唯一入口）
        |  读写 / 事务
BlockRepository               # 门面：open / fromStore / store() / meta* / exec
        |
BlockStore                    # 块模型持久化（dict / block / prop / link / meta）
WriteBatch                    # 原子写事务
        |
ConnectionPool               # SQLite 连接池（WAL 模式）
```

| 层 | 位置 | 职责 |
| --- | --- | --- |
| `ConnectionPool` | `data/sqlite/connection/` | SQLite 连接管理：`WAL` + `synchronous=NORMAL` + `busy_timeout=5s` |
| `BlockStore` | `data/sqlite/block/` | 把数据拆成可寻址的"块"，按 key 增量读写，五张系统表：`dict` / `block` / `prop` / `link` / `meta` |
| `WriteBatch` | 同上 | 一批修改在一次 `commit()` 中原子落盘，失败 `rollback()` |
| `BlockRepository` | 同上 | 对外只剩：`open` / `fromStore` / `store()` / `metaGet\|Set\|Del` / `exec` |
| `TypedTable` | 同上 | 把列名提升到编译期（列由 `enum class` 定义）；`open` 时把 `版本 + 类型名 + 列数` 写入 `schema:<表名>`，不一致返回 `SchemaMismatch` |

所有方法统一返回 `ll::Expected<T>`，用 `and_then` / `transform` / `or_else` 链式处理，不使用异常。用法详见 [模块开发指南](./module.md) 的"数据层"章节，以及完整的 [SQLite 层使用教程](./sqlite.md)。

> [!WARNING]
> 数据层约束：**`VACUUM` 等少数语句不能在事务内执行**，必须走 `BlockRepository::exec()`（自动提交）；而 `CREATE` / `DROP TABLE` / `CREATE INDEX` 等 DDL 在 SQLite 中是事务性的，可以安全地与 DML 放在同一个 `WriteBatch` 里。**事务开着时不要调用 `repo->exec()`**——它会另取一条连接，撞上本事务持有的写锁。事务内的读取会复用事务自己的连接，不会向池子再申请。详见教程第 9 节。

### 数据库文件

| 文件 | 归属 | 说明 |
| --- | --- | --- |
| `data/settings.db` | 全局 | 以 `SettingsDB` 服务注册，Wallet、Language、PvP、Notice、Chat、Tpa、Market、Statistics 的公共表都落在这里 |
| `data/blacklist.db` | Blacklist | 违规词与封禁名单 |
| `data/mute.db` | Mute | 禁言名单 |
| `data/chat.db` | Chat | 聊天记录与拦截规则 |
| `data/market.db` | Market | 市场 / 商店 / 求购 / 拍卖 / 报价 |
| `data/statistics.db` | Statistics | 玩家统计 |
| `data/tpa.db` | Tpa | 传送请求 |
| `data/behaviorevent.db` | BehaviorEvent | 行为事件日志 |

`Config`、`Notice`（`notice.json`）与 `Cdk`（`cdk.json`）不走块存储，仍使用 `JsonStorage`，可直接编辑。

> [!WARNING]
> `BlockRepository::open` **不做任何数据迁移**：它只在目标 SQLite 文件里创建块模型的五张系统表，旧版本写入的数据表不会被读取或转换。跨版本升级见 [数据迁移](../course/migrate.md)。

## 脚本引擎 VM

LCUI 脚本在 `frontend/ir/` 编译为三地址 MIR，由 `VM` 直接解释执行。对象访问与字段缓存围绕以下两点设计：

### 对象模型与字段布局

每个 `Object` 持有一个**不可变**的 `FieldLayout`（字段名到 slot 索引的有序映射）。编译期/构造期已知的类字段在布局中占据连续 slot，`slotOf(name)` 为常数时间查找。字段动态写入先落到 `spill` 哈希表作为回退，保证未声明字段仍可存取。

### 字段内联缓存

字段名在每条指令处常量化。`VM` 持有一张按指令地址直接映射的定长表，条目记录 `(指令, layout, slot)`：同一指令在同一形状上直接命中，形状变化或槽位重排即失效重算。缓存随 VM 私有，不再经由共享的 `ChunkCache` 做哈希查找，也不再需要跨线程同步。

按名派发（`CALL_METHOD_BY_NAME`）以 `(指令, classIndex)` 缓存解析出的 ordinal，宿主函数调用以指令缓存 `ns::func` 的切分结果，避免每次调用重复线性查找与切片分配。

### Hidden Class / 形状转换

`extendShape(base, name)` 以 `(base, name)` 为键**记忆化**生成不可变扩展 `FieldLayout`，使同类对象共享同一形状；`Object::addField()` 把动态新增字段迁移到真实 slot（而非各自独立的 `spill`），令具有相同动态形状的对象收敛到同一 `layout`，从而让 PIC 在多对象之间稳定命中。已存在字段原地写入，语义不变。

> [!NOTE]
> 形状转换表用一把互斥锁保护：调度器可在多个线程上 pump 任务，同一形状可能被并发登记。字段缓存与派发缓存都是 VM 私有状态，只由持有该 VM 的线程读写。

### 环回收（`CycleCollector`）

脚本对象、数组与闭包之间用 `shared_ptr` 互指时会形成引用环，析构永远不会发生。每个 VM 持有一个收集器，只登记**接收过引用写**的容器：判定用「引用计数 − 内部入度」找出外部可达的根，其余节点断开出边后由析构释放预算槽位。节点索引内嵌在对象里，扫描不建哈希表。

`NativeHandle::trace(ValueVisitor&)` 让原生句柄把持有的脚本值（如表单的 `show` 回调、`CtxValue` 的包装值）暴露给同一次扫描；默认实现为空，不持有脚本值的句柄无需实现。`std::function` 捕获的脚本值仍不可见，经过它们的环依旧会漏——这是刻意保留的边界。

堆由 `ScriptHeapRegistry` 按脚本 id 共享：同一脚本的所有 VM（同一玩家的重复打开、不同玩家的实例、宿主持有的数组）落在同一张图上，跨 VM 的环因此可见。注册表创建的堆是共享堆，其登记、屏障与推进都走同一把互斥锁；VM 自带的堆不是共享堆，不加锁。推进（`step`/`collect`）额外要求当前只有一个执行者（`exclusive`）：并发跑同一个脚本时回收被跳过而不是冒险扫描正在被另一个线程改写的字段，少收的环会在之后的独占窗口里收走。

回收按 `Build → Index → Mark → Sweep` 分阶段推进，`step(budget)` 每次只处理固定数量的节点，由解释器每 1024 条指令推进一次，因此不会在单条 tick 上停顿整堆扫描。运行期间若有引用写入，VM 会把「被覆盖的旧值」与「写入的新值」标记为根（写屏障），保证增量期间不会误收活对象；屏障只会多留、不会误杀。

### 优化器流水线

优化器以基本块为单位反复执行 `ConstantPropagation → ConstantFold → DeadStore → DeadCode → CSE → DeadValue`，直到一轮没有任何改动；随后执行 LICM 与分支融合，再做一次收敛迭代。`ConstantPropagation` 在 CFG 上做前向数据流分析，汇合点对寄存器、slot 与变量名状态取交集；自环块单独迭代到稳定，避免把循环入口误判为常量。`ConstantFold` 在做全常量折叠之外，还处理数值恒等元（`x + 0`、`x * 1`、`x / 1`、`x ^ 1`），只在结果类型确定为 `int` / `float` 时把指令替换为 `MOVE`，保证与运行期类型提升一致。

### 字节码包（`.lcp`）

`MirSerializer` 写出的包由 `abiFingerprint`（原生导出面）、源码/导入哈希、`layoutFingerprint`（本包类表）与 MIR 主体组成。`layoutFingerprint` 在写入时从内存中的 chunk 计算、读取时从解码结果重算，两者不一致即拒绝加载——它捕获的是「写端/读端不同步」这类自洽但语义错误的包（例如某个类表字段被漏写）。版本不符或校验失败时，加载器回退到源码重新编译并覆写包。
