<!-- markdownlint-disable MD033 -->
<!-- markdownlint-disable MD041 -->

<div align="center">

# LOICollectionA

> **开箱即用的 LeviLamina 多功能插件集。**

![Release](https://img.shields.io/github/v/release/loitietu/LOICollectionA?style=flat-square)
![Stars](https://img.shields.io/github/stars/loitietu/LOICollectionA?style=social)
![Downloads](https://img.shields.io/github/downloads/loitietu/LOICollectionA/total?style=flat-square)
[![License](https://img.shields.io/github/license/loitietu/LOICollectionA)](LICENSE)

[![English](https://img.shields.io/badge/English-inactive?style=for-the-badge)](README.md)
[![简体中文](https://img.shields.io/badge/简体中文-informational?style=for-the-badge)](README.zh.md)
[![656669024](https://img.shields.io/badge/1018233878-red?style=for-the-badge&logo=qq)](https://qm.qq.com/cgi-bin/qm/qr?k=l7XBaItHiNLnFKX7YiI7uqsEIZHaxjq3&jump_from=webapi&authKey=G3/2El1RPyAVYP4NYTJ2ytKRL6hSYfDNQXbrOlKBy/P0FEUjQSnXF8c7TWNkGbCC)

[快速开始](#快速开始) · [功能总览](#功能总览) · [开发者](#开发者) · [本地编译](#本地编译) · [社区与贡献](#社区与贡献)

</div>

## 这是什么？

LOICollectionA 是运行在 [LeviLamina](https://github.com/LiteLDev/LeviLamina) 上的 Minecraft 基岩版服务端多功能插件集。它由 LOICollection 完整重构而来，并针对 LeviLamina 重新适配；采用微内核架构组织功能模块，每个功能都可以在配置文件中独立开关。

为什么值得装？

- **一个插件，常用功能全都有。** 管理、经济、商店、传送、公告、统计等常用功能开箱即用，不用再维护一大堆插件。
- **模块化，按需开关。** 不需要的功能可以直接在配置文件里关掉，模块之间互不干扰。
- **原生 UI 体验。** 功能基于基岩版原生表单（`.lcui`）实现，玩家在游戏内就能获得直观的界面操作。

> 项目正在持续开发中，未来会提供更多 API 接口。

## 功能总览

以下功能模块均可在配置文件中开启或关闭。

### 基础模块

| 模块 | 作用 | 可配置开关 |
| --- | --- | --- |
| Blacklist | 禁止指定玩家进入服务器 | ✅ |
| Mute | 对玩家进行禁言 | ✅ |
| Cdk | 创建、管理并兑换 CDK 兑换码 | ✅ |
| Menu | 自定义游戏内菜单及操作 | ✅ |
| Tpa | 传送请求系统，支持邀请控制与黑名单 | ✅ |
| Shop | 服务器商店，支持购买与出售物品 | ✅ |
| Monitor | 实时服务器信息与玩家侧边栏 | ✅ |
| Pvp | 玩家自行开关 PVP | ✅ |
| Wallet | 玩家经济，支持转账、红包与财富排行 | ✅ |
| Chat | 聊天称号与玩家聊天黑名单 | ✅ |
| Language | 玩家语言偏好，配套语言选择 GUI | ➖ |
| Notice | 创建带优先级的公告与通知 | ✅ |
| Market | 玩家之间的交易市场 | ✅ |
| BehaviorEvent | 将玩家与世界的行为事件记录到数据库 | ✅ |
| Statistics | 统计在线时长、击杀、死亡、破坏方块等数据 | ✅ |

> `Language` 没有配置开关：它始终启用，用于记录每位玩家的语言偏好。

### 附加模块

| 模块 | 作用 | 可配置开关 |
| --- | --- | --- |
| BasicHook | 底层事件 Hook，内置 FakeSeed（向客户端伪造世界种子） | ✅ |
| RedStone | 红石相关事件 Hook | ✅ |
| OrderedUI | 让多个 UI 表单按顺序弹出，避免界面冲突 | ✅ |

## 快速开始

> 前置要求：已安装 [lip](https://github.com/LiteLDev/lip) 的 LeviLamina 服务端（26.50.x）。

1. 在服务端根目录执行以下命令：

    ```cmd
    lip install github.com/loitietu/LOICollectionA
    ```

2. 启动服务器（`bedrock_server_mod.exe`）。
3. 等待控制台输出加载成功提示。

需要更多？请阅读[快速开始文档](docs/md/start.md)（手动安装、更新、常见问题）、[适配版本](docs/md/version.md)或[数据迁移指南](docs/course/migrate.md)。

## 开发者

<details>
<summary><strong>开发者文档</strong></summary>

`.lcui` 是插件的原生 UI 层。它封装了 LeviLamina 的表单与界面能力，包括 `CustomForm`、`MessageBox`、`PaginatedForm` 等，开发者无需重新编译即可用脚本构建游戏内界面。

- [LCUI 脚本语法](docs/md/lcui.md)
- [LOICollectionAPI 参考](docs/md/api.md)
- [开发环境配置](docs/dev/config.md)

未来将提供更多 API 接口，方便插件开发者扩展更丰富的功能。

</details>

## 本地编译

前置要求：[xmake](https://github.com/xmake-io/xmake) 与 `clang-cl` 工具链。

打开命令提示符（`cmd`）并执行：

```cmd
git clone https://github.com/loitietu/LOICollectionA.git
cd LOICollectionA
xmake repo -u
xmake
```

详细说明请查看[开发环境配置](docs/dev/config.md)。

## 社区与贡献

- 加入 QQ 群：[1018233878](https://qm.qq.com/cgi-bin/qm/qr?k=l7XBaItHiNLnFKX7YiI7uqsEIZHaxjq3&jump_from=webapi&authKey=G3/2El1RPyAVYP4NYTJ2ytKRL6hSYfDNQXbrOlKBy/P0FEUjQSnXF8c7TWNkGbCC)
- 完整文档：[GitHub Pages](https://loitietu.github.io/LOICollectionA/)
- 遇到问题或有新想法？欢迎提交 [Issue](https://github.com/loitietu/LOICollectionA/issues) 或 [PR](https://github.com/loitietu/LOICollectionA/pulls)。

## LICENSE

该插件根据 [GPL-3.0](LICENSE) 许可证进行许可。
