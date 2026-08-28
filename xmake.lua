add_rules("mode.debug", "mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")

set_xmakever("3.0.0")

local is_server = is_config("target_type", "server")

option("shared")
    set_default(true)
    set_showmenu(true)
    set_description("Build shared library")
option_end()

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

add_requires("sqlitecpp 3.3.3", {configs = {shared = get_config("shared")}})
add_requires("levilamina 26.20.7", {configs = {target_type = get_config("target_type")}})
add_requires(
    "levibuildscript",
    "preloader 1.15.7",
    "nlohmann_json 3.12.0",
    "gtest v1.17.0"
)

set_toolchains("clang-cl")
 
if not has_config("vs_runtime") then
    set_runtimes("MD")
end

set_version("1.16.0")

includes("scripts/modpacker.lua")

target("LOICollectionA")
    add_rules("c++.unity_build", { batchsize = 8 })
    add_rules(
        "@levibuildscript/linkrule",
        "modpacker"
    )
    add_cxflags(
        "/EHa",
        "/utf-8",
        "/permissive-",
        "/Ob3",
        "/W4",
        "/w44265",
        "/w44289",
        "/w44296",
        "/w45263",
        "/w44738",
        "/w45204"
    )
    add_cxflags(
        "/EHs",
        "-Wno-microsoft-cast",
        "-Wno-invalid-offsetof",
        "-Wno-c++2b-extensions",
        "-Wno-microsoft-include",
        "-Wno-overloaded-virtual",
        "-Wno-ignored-qualifiers",
        "-Wno-missing-field-initializers",
        "-Wno-potentially-evaluated-expression",
        "-Wno-pragma-system-header-outside-header",
        {tools = {"clang_cl"}}
    )
    add_defines(
        "NOMINMAX",
        "UNICODE",
        "_HAS_CXX23=1",
        "LOICOLLECTION_A_EXPORTS"
    )
    set_configdir("$(builddir)/config")
    add_configfiles("assets/resources/Version.h.in")

    if is_plat("windows") then
        add_files("assets/resources/**.rc")
    end

    add_includedirs("src", "$(builddir)/config")

    if is_mode("debug") then
        add_defines("DEBUG")

        add_packages("gtest")
        
        add_files("tests/**.cpp")
        add_includedirs("tests")
        add_defines('LOICOLLECTION_A_PERMISSION_JSON="' .. os.projectdir():gsub("\\", "/") .. '/assets/common/gui/permission.json"')
    elseif is_mode("release") then
        add_defines("NDEBUG")
    end

    if is_server then
        add_files("src/LOICollectionA/**.cpp|modules/client/**.cpp|frontend/builtin/mc/client/**.cpp")
        remove_headerfiles("src/LOICollectionA/include/(client/**.h)")
        remove_headerfiles("src/LOICollectionA/frontend/builtin/mc/(client/**.h)")
        add_defines("LL_PLAT_S")

        if is_mode("debug") then
            remove_files("tests/client/**.cpp")
        end
    else
        add_files("src/LOICollectionA/**.cpp|modules/server/**.cpp|utils/*-server/**.cpp|frontend/builtin/mc/server/**.cpp")
        remove_headerfiles("src/LOICollectionA/include/(server/**.h)")
        remove_headerfiles("src/LOICollectionA/frontend/builtin/mc/(server/**.h)")
        add_defines("LL_PLAT_C")

        if is_mode("debug") then
            remove_files("tests/server/**.cpp")
        end
    end

    set_pcxxheader("src/LOICollectionA/include/Global.h")

    add_headerfiles("src/(LOICollectionA/**.h)")
    remove_headerfiles(
        "src/LOICollectionA/frontend/(builtin/**.h)",
        "src/LOICollectionA/(utils/**.h)",
        "src/LOICollectionA/*.h"
    )

    add_packages(
        "levilamina",
        "preloader",
        "nlohmann_json",
        "sqlitecpp"
    )

    set_exceptions("none")
    set_kind("shared")
    set_languages("cxx20")
    set_symbols("debug")

    on_load(function (target)
        import("core.base.json")

        local data = json.loadfile("tooth.json")
        local major, minor, patch = target:version():match("(%d+)%.(%d+)%.(%d+)")

        target:set("configvar", "VERSION_MAJOR", major)
        target:set("configvar", "VERSION_MINOR", minor)
        target:set("configvar", "VERSION_PATCH", patch)
        target:set("configvar", "VERSION_BUILD", os.time())
        target:set("configvar", "COMPANY_NAME", data["tooth"])
        target:set("configvar", "FILE_DESCRIPTION", data["info"]["description"])
    end)
