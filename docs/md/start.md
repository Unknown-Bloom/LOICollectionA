# 快速开始

## 安装 LOICollectionA

这一部分分为两个部分 `手动安装` 和 `lip安装`  
在大多数情况下,通常建议使用 `lip安装`  
但如果是在部分特殊情况下,那么 `手动安装` 则比 `lip安装` 来的更加快捷

### lip安装

> [!WARNING]
> 在安装之前请确保已正常安装 `lip` 并配置好服务端

- 请在服务端根目录下执行以下命令安装 LOICollectionA

```bash
lip install github.com/loitietu/LOICollectionA
```

- 对于安装指定版本的可以执行以下命令

```bash
lip install github.com/loitietu/LOICollectionA@v1.17.0
```

- 对于进行版本更新的可以执行以下命令

```bash
lip install --upgrade github.com/loitietu/LOICollectionA
```

> [!TIP]
> 在进行版本更新时，请确保目标版本是否需要数据迁移。  
> 如需进行数据迁移，请先备份插件数据，然后在 `Github` 库中的 `scripts/migrate` 文件夹中找到对应版本的 `migrate.py` 文件，按照 [说明](../course/migrate.md) 进行数据迁移。

- 安装完成后,您可以在 `plugins` 文件夹中找到 LOICollectionA 的安装文件
- 双击服务端根目录下的 `bedrock_server_mod.exe` 文件以启动服务器
- 在服务器终端中,您应该能够看到 LOICollection 的启动日志

---

### 手动安装

> **目前 LOICollectionA 依赖以下前置库**

| 名称 | 版本 |
| --- | --- |
| LeviLamina | 26.20.x |

> 请您手动下载版本兼容的前置组件。若不确定版本兼容性，建议改用 lip 安装。

1. 前往 Minebbs 或者 Github Release 下载 LOICollectionA 的最新版本
2. 解压下载的 `LOICollectionA-windows-x64.zip`
3. 将解压后的 `LOICollectionA` 文件夹移动到 `plugins` 文件夹下
4. 双击服务端根目录下的 `bedrock_server_mod.exe` 文件以启动服务器
5. 在服务器终端中，您应该能够看到 LOICollection 的启动日志

---

### 常见问题

- **无法启用: unknown error when make exception string**
  - 对于这个问题，可以尝试前往 Github Release 手动安装 `LOICollectionA-ES-windows-x64.zip` 版本

## 安装 LOICollectionA-Expand

> [!NOTE]
> 以下安装步骤与 LOICollectionA 类似，此处仅提供 lip 安装方式供参考。

### 安装说明

> [!WARNING]
> 在安装之前请确保已正常安装 `lip` 并配置好服务端

- 请在服务端根目录下执行以下命令安装 LOICollectionA

```bash
lip install github.com/loitietu/LOICollectionA-Expand
```

- 对于安装指定版本的可以执行以下命令

```bash
lip install github.com/loitietu/LOICollectionA-Expand@v1.0.1
```

- 对于进行版本更新的可以执行以下命令

```bash
lip install --upgrade github.com/loitietu/LOICollectionA-Expand
```

> [!TIP]
> 话说这真的有人看吗 qwq
