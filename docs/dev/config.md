# 环境配置

该部分将介绍两种开发场景的环境配置：将 LOICollectionA 作为依赖库集成到自己的 mod 项目（依赖集成），以及为本仓库本身开发调试（本仓库开发）。

## 依赖集成：在自己的 mod 中使用 LOICollectionA

在开始之前，请确保您已经安装了 `xmake` 和 `C++` 编译环境。

> [!TIP]
> 以下内容将默认您已经安装了 `xmake` 和 `C++` 编译环境，并创建了一个空的 `mod` 项目

1. 打开项目根目录下的 `xmake.lua` 文件，并添加以下内容：

    ```lua
    package("LOICollectionA")
        add_urls("https://github.com/loitietu/LOICollectionA.git")
        add_versions("1.17.1", "250d28ab2a915ddc321952c4c981f8c20c70877e")

        on_install(function (package)
            import("package.tools.xmake").install(package)
        end)
    package_end()
    ```

    > [!WARNING]
    > `add_versions` 中的提交哈希必须与 `add_urls` 仓库中的真实提交一致。请以 [GitHub 仓库](https://github.com/loitietu/LOICollectionA) 的 tag（如 `v1.17.1`）对应的提交哈希为准，示例中的哈希来自 v1.17.1 标签。

2. 保存并关闭 `xmake.lua` 文件。

- 此时项目已经配置完毕，您可以通过 `add_requires` 和 `add_packages` 去安装依赖库。

> [!TIP]
> 以上内容仅为示例，其中部分可能会有所区别，请根据您的实际情况进行修改。

## 本仓库开发

如果您希望为 LOICollectionA 本身开发模块或扩展 API，请参考 [构建与测试](./build.md) 完成环境搭建与构建，并阅读以下文档：

- [架构概览](./architecture.md) —— 模块框架、服务容器与插件生命周期
- [模块开发指南](./module.md) —— 如何编写一个新模块
- [LOICollectionAPI 扩展指南](./api-extension.md) —— 如何注册脚本变量、函数与原生类
