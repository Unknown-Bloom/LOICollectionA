rule("modpacker")
after_build(function(target)
    local mod_define = target:extraconf("rules", "modpacker")

    mod_define = mod_define or {}
    mod_define.modName = mod_define.modName or target:name()
    mod_define.modFile = mod_define.modFile or path.filename(target:targetfile())

    if mod_define.modVersion == nil then
        local major, minor, patch = target:version():match("(%d+)%.(%d+)%.(%d+)")
        if not major then
            print("Failed to get version, using 0.0.0")
            major, minor, patch = 0, 0, 0
        end
        mod_define.modVersion = mod_define.modVersion or (major .. "." .. minor .. "." .. patch)
    end

    import("core.project.config")
    mod_define.modPlatform = mod_define.modPlatform or config.get("target_type")

    function string_formatter(str, variables)
        return str:gsub("%${(.-)}", function(var)
            return variables[var] or "${" .. var .. "}"
        end)
    end

    function pack_mod(target, mod_define)
        import("lib.detect.find_file")

        local manifest_path = find_file("scripts/manifest.json", os.projectdir())
        if manifest_path then
            local binname = path.basename("bin-" .. (mod_define.modPlatform == "server" and "server" or "client"))

            local manifest = io.readfile(manifest_path)
            local bindir = path.join(os.projectdir(), "build/" .. binname)
            local outputdir = path.join(bindir, mod_define.modName)
            local targetfile = path.join(outputdir, mod_define.modFile)
            local pdbfile = path.join(outputdir, path.basename(mod_define.modFile) .. ".pdb")
            local manifestfile = path.join(outputdir, "manifest.json")
            local oritargetfile = target:targetfile()
            local oripdbfile = path.join(path.directory(oritargetfile), path.basename(oritargetfile) .. ".pdb")

            os.mkdir(outputdir)
            os.cp(oritargetfile, targetfile)
            if os.isfile(oripdbfile) then
                os.cp(oripdbfile, pdbfile)
            end

            local assetsdir = path.join(os.projectdir(), "assets")
            local commondir = path.join(assetsdir, "common")
            if os.isdir(commondir) then
                os.cp(path.join(commondir, "*"), outputdir)
            end

            formattedmanifest = string_formatter(manifest, mod_define)
            io.writefile(manifestfile, formattedmanifest)
            cprint("${bright green}[Mod Packer]: ${reset}mod already generated to " .. outputdir)
        else
            cprint("${bright yellow}warn: ${reset}not found manifest.json in root dir!")
        end
    end

    pack_mod(target, mod_define)
end)
