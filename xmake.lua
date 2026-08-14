-- Enable section-level dead-code stripping (--gc-sections etc.) for the final
-- binary. Only affects targets that call this function; platform-conditional
-- (idiomatic xmake):
--   Linux/mingw (GCC/Clang) : -ffunction-sections -fdata-sections + -Wl,--gc-sections
--   macOS (ld64)            : -ffunction-sections -fdata-sections + -dead_strip
--   Windows (MSVC)          : /Gy (function-level linking, prerequisite for /OPT:REF) + /OPT:REF
function enable_section_gc()
    if is_plat("linux", "mingw") then
        add_cxflags("-ffunction-sections", "-fdata-sections")
        add_ldflags("-Wl,--gc-sections")
    elseif is_plat("macosx") then
        add_cxflags("-ffunction-sections", "-fdata-sections")
        add_ldflags("-dead_strip")
    elseif is_plat("windows") then
        add_cxflags("/Gy")
        add_ldflags("/OPT:REF")
    end
end

target("ftxui")
    set_kind("static")
    set_default(false)
    set_languages("cxx17")
    enable_section_gc()
    add_includedirs("include", {public = true})
    add_includedirs("src", {public = true})

    for _, file in ipairs(os.files(path.join(
        os.scriptdir(), "src/ftxui/**.cpp"))) do
        local name = path.basename(file)
        if not name:find("_test", 1, true) and
            not name:find("fuzzer", 1, true) then
            add_files(file)
        end
    end

    if is_plat("windows", "mingw") then
        add_defines("FTXUI_MICROSOFT_TERMINAL_FALLBACK", {public = true})
        add_defines("UNICODE", "_UNICODE", {public = true})
    elseif is_plat("linux", "bsd") then
        add_syslinks("pthread", {public = true})
    end
