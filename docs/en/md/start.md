# Quick Start

## Installing LOICollectionA

This section is divided into two parts: `manual installation` and `lip installation`  
In most cases, it is usually recommended to use `lip installation`  
However, in some special cases, `manual installation` is quicker than `lip installation`

### lip Installation

> [!WARNING]
> Before installation, please make sure `lip` is properly installed and the server is configured

- Run the following command in the server root directory to install LOICollectionA

```bash
lip install github.com/loitietu/LOICollectionA
```

- To install a specific version, run the following command

```bash
lip install github.com/loitietu/LOICollectionA@v1.17.0
```

- To update the version, run the following command

```bash
lip install --upgrade github.com/loitietu/LOICollectionA
```

> [!TIP]
> When updating the version, please make sure whether the target version requires data migration.  
> If data migration is required, please back up the plugin data first, then find the `migrate.py` file for the corresponding version in the `scripts/migrate` folder of the `Github` repository, and perform the data migration according to the [instructions](../course/migrate.md).

- After installation, you can find the LOICollectionA installation files in the `plugins` folder
- Double-click the `bedrock_server_mod.exe` file in the server root directory to start the server
- In the server terminal, you should be able to see the LOICollection startup log

---

### Manual Installation

> **Currently LOICollectionA depends on the following prerequisite libraries**

| Name | Version |
| --- | --- |
| LeviLamina | 26.20.x |

> Please manually download the version-compatible prerequisite components. If you are unsure about version compatibility, it is recommended to use lip installation instead.

1. Go to Minebbs or Github Release to download the latest version of LOICollectionA
2. Extract the downloaded `LOICollectionA-windows-x64.zip`
3. Move the extracted `LOICollectionA` folder to the `plugins` folder
4. Double-click the `bedrock_server_mod.exe` file in the server root directory to start the server
5. In the server terminal, you should be able to see the LOICollection startup log

---

### Common Issues

- **Unable to enable: unknown error when make exception string**
  - For this issue, you can try going to Github Release to manually install the `LOICollectionA-ES-windows-x64.zip` version

## Installing LOICollectionA-Expand

> [!NOTE]
> The installation steps below are similar to those for LOICollectionA; only the lip installation method is provided here for reference.

### Installation Instructions

> [!WARNING]
> Before installation, please make sure `lip` is properly installed and the server is configured

- Run the following command in the server root directory to install LOICollectionA

```bash
lip install github.com/loitietu/LOICollectionA-Expand
```

- To install a specific version, run the following command

```bash
lip install github.com/loitietu/LOICollectionA-Expand@v1.0.1
```

- To update the version, run the following command

```bash
lip install --upgrade github.com/loitietu/LOICollectionA-Expand
```

> [!TIP]
> Honestly, does anyone even read this? qwq
