# Environment Configuration

This section introduces the environment configuration for two development scenarios: integrating LOICollectionA as a dependency library into your own mod project (dependency integration), and developing and debugging for this repository itself (repository development).

## Dependency Integration: Using LOICollectionA in Your Own mod

Before you begin, please make sure you have installed `xmake` and the `C++` compilation environment.

> [!TIP]
> The following assumes that you have already installed `xmake` and the `C++` compilation environment and created an empty `mod` project

1. Open the `xmake.lua` file in the project root directory and add the following content:

    ```lua
    package("LOICollectionA")
        add_urls("https://github.com/Unknown-Bloom/LOICollectionA.git")
        add_versions("1.18.0", "bd2237938eb1f9e5b3e6fd9c8da483fa6f9a20a9")

        on_install(function (package)
            import("package.tools.xmake").install(package)
        end)
    package_end()
    ```

    > [!WARNING]
    > The commit hash in `add_versions` must match the actual commit in the `add_urls` repository. Please use the commit hash corresponding to the tag (such as `v1.18.0`) of the [GitHub repository](https://github.com/Unknown-Bloom/LOICollectionA) as the reference; the hash in the example comes from the commit that v1.18.0 points to.

2. Save and close the `xmake.lua` file.

- At this point, the project is fully configured. You can use `add_requires` and `add_packages` to install the dependency library.

> [!TIP]
> The above content is only an example; some parts may differ, so please modify it according to your actual situation.

## Repository Development

If you want to develop modules or extend the API for LOICollectionA itself, please refer to [Build and Test](./build.md) to set up the environment and build, and read the following documents:

- [Architecture Overview](./architecture.md) — module framework, service container, and plugin lifecycle
- [Module Development Guide](./module.md) — how to write a new module
- [LOICollectionAPI Extension Guide](./api-extension.md) — how to register script variables, functions, and native classes
