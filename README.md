<!-- markdownlint-disable MD033 -->
<!-- markdownlint-disable MD041 -->

<div align="center">

# LOICollectionA

> **A ready-to-use, multifunctional plugin set for LeviLamina.**

![Release](https://img.shields.io/github/v/release/loitietu/LOICollectionA?style=flat-square)
![Stars](https://img.shields.io/github/stars/loitietu/LOICollectionA?style=social)
![Downloads](https://img.shields.io/github/downloads/loitietu/LOICollectionA/total?style=flat-square)
[![License](https://img.shields.io/github/license/loitietu/LOICollectionA)](LICENSE)

[![English](https://img.shields.io/badge/English-inactive?style=for-the-badge)](README.md)
[![简体中文](https://img.shields.io/badge/简体中文-informational?style=for-the-badge)](README.zh.md)
[![656669024](https://img.shields.io/badge/1018233878-red?style=for-the-badge&logo=qq)](https://qm.qq.com/cgi-bin/qm/qr?k=l7XBaItHiNLnFKX7YiI7uqsEIZHaxjq3&jump_from=webapi&authKey=G3/2El1RPyAVYP4NYTJ2ytKRL6hSYfDNQXbrOlKBy/P0FEUjQSnXF8c7TWNkGbCC)

[Quick Start](#quick-start) · [Features](#features) · [Developers](#developers) · [Build](#build-from-source) · [Contributing](#community--contributing)

</div>

## What is it?

LOICollectionA is a multifunctional plugin set for Minecraft Bedrock servers running [LeviLamina](https://github.com/LiteLDev/LeviLamina). It is a complete refactor of LOICollection, rebuilt for LeviLamina with a micro-kernel architecture: every feature lives in its own module and can be enabled or disabled from the configuration file.

Why choose it?

- **One install, many features.** Cover moderation, economy, shops, teleportation, announcements, statistics, and more without juggling a dozen plugins.
- **Modular by design.** Turn off what you don't need — modules run independently and don't interfere with each other.
- **Native UI everywhere.** Features use Bedrock's native forms through the built-in `.lcui` layer, so players get a proper in-game interface instead of chat-command-only experiences.

> This project is under active development. More API interfaces for plugin developers are planned.

## Features

All modules below can be enabled or disabled in the configuration file.

### Basic Modules

| Module | What it does | Config |
| --- | --- | --- |
| Blacklist | Block specified players from joining the server | ✅ |
| Mute | Mute players for a set duration | ✅ |
| Cdk | Create, manage, and redeem CDK codes | ✅ |
| Menu | Custom in-game menus with actions | ✅ |
| Tpa | Teleport request system with invite control and blacklist | ✅ |
| Shop | Server shops to buy and sell items | ✅ |
| Monitor | Real-time server info and player sidebars | ✅ |
| Pvp | Per-player PvP toggle | ✅ |
| Wallet | Player economy with transfers, red envelopes, and wealth rankings | ✅ |
| Chat | Chat titles and per-player chat blacklist | ✅ |
| Language | Per-player language preference with a selection GUI | ➖ |
| Notice | Create and schedule announcements with priorities | ✅ |
| Market | Player-to-player marketplace | ✅ |
| BehaviorEvent | Record player and world behavior events to a database | ✅ |
| Statistics | Track online time, kills, deaths, blocks, and more | ✅ |

> `Language` has no configuration switch: it is always enabled and persists each player's language choice.

### Additional Modules

| Module | What it does | Config |
| --- | --- | --- |
| BasicHook | Low-level event hooks, including FakeSeed (spoof the world seed reported to clients) | ✅ |
| RedStone | Redstone-related event hooks | ✅ |
| OrderedUI | Queue multiple UI forms so they open one by one without conflicts | ✅ |

## Quick Start

> Requirements: a LeviLamina server (26.50.x) with [lip](https://github.com/LiteLDev/lip) installed.

1. Run the following command in your server directory:

    ```cmd
    lip install github.com/loitietu/LOICollectionA
    ```

2. Start the server (`bedrock_server_mod.exe`).
3. Wait for the loading confirmation message in the console.

Need more? Read the [quick start guide](docs/en/md/start.md) (manual installation, upgrades, common issues), the [version compatibility list](docs/en/md/version.md), or the [data migration guide](docs/en/course/migrate.md).

## Developers

<details>
<summary><strong>Developer docs</strong></summary>

`.lcui` is the plugin's native UI layer. It wraps LeviLamina's form and UI capabilities — including `CustomForm`, `MessageBox`, and `PaginatedForm` — so you can build in-game interfaces with scripts instead of recompiling the plugin.

- [LCUI scripting guide](docs/en/md/lcui.md)
- [LOICollectionAPI reference](docs/en/md/api.md)
- [Development environment setup](docs/en/dev/config.md)

More API interfaces for plugin developers are planned.

</details>

## Build from Source

Requirements: [xmake](https://github.com/xmake-io/xmake) and a `clang-cl` toolchain.

Open Command Prompt (`cmd`) and execute:

```cmd
git clone https://github.com/loitietu/LOICollectionA.git
cd LOICollectionA
xmake repo -u
xmake
```

See the [development environment guide](docs/en/dev/config.md) for details.

## Community & Contributing

- Join the QQ group: [1018233878](https://qm.qq.com/cgi-bin/qm/qr?k=l7XBaItHiNLnFKX7YiI7uqsEIZHaxjq3&jump_from=webapi&authKey=G3/2El1RPyAVYP4NYTJ2ytKRL6hSYfDNQXbrOlKBy/P0FEUjQSnXF8c7TWNkGbCC)
- Full documentation: [GitHub Pages](https://loitietu.github.io/LOICollectionA/)
- Found a bug or want a feature? Open an [issue](https://github.com/loitietu/LOICollectionA/issues) or submit a [pull request](https://github.com/loitietu/LOICollectionA/pulls).

## License

Licensed under [GPL-3.0](LICENSE).
