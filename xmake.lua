-- NorsePayload - UE4SS v4.0.0 C++ mod for NORSE: Oath of Blood (timing tables on animations).
-- Uses NorsePatcher's shared headers and UE4SS import library (../NorsePatcher), the Norse Address Library runtime header
-- (../NorseAddressLibrary/include) and MinHook (BSD-2, third_party/minhook, copied from NorseRealtimeNative).
-- Build:  xmake f -p windows -a x64 -m release --toolchain=msvc -y && xmake
-- Output: build\windows\x64\release\main.dll + main.pdb

set_project("NorsePayload")
local version = "1.0.0"
set_version(version)
set_xmakever("2.8.0")

add_rules("mode.release")
set_defaultmode("release")
set_languages("c++20")
set_runtimes("MD")

target("main")
    set_kind("shared")
    add_files("src/*.cpp")
    add_files("third_party/minhook/src/*.c", "third_party/minhook/src/hde/hde64.c")
    add_includedirs("third_party/minhook/include", "../NorsePatcher/include", "../NorseManifest/include", "../NorseEvents/include")
    add_defines("NPL_VERSION=\"" .. version .. "\"")
    -- VERSIONINFO (rule 6): src/version.rc reads these; the numbers come from `version`, which only version-gate writes
    add_files("src/version.rc")
    add_defines("NORSE_VER_STR=\"" .. version .. "\"")
    add_defines("NORSE_VER_MAJOR=" .. version:match("^(%d+)"), "NORSE_VER_MINOR=" .. version:match("^%d+%.(%d+)"),
                "NORSE_VER_PATCH=" .. version:match("^%d+%.%d+%.(%d+)"))
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", "UNICODE", "_UNICODE")
    add_cxflags("/EHsc", "/W4", "/permissive-", "/utf-8", {force = true})
    add_linkdirs("../NorsePatcher/build/ue4ss")
    add_links("UE4SS", "user32")
    set_symbols("debug")
    set_optimize("faster")
    -- rule 45: no build-machine paths in the binary (TABLE form, gate row xmake-d1trimfile-reaches-cl-intact)
    add_cxflags({"/d1trimfile:$(projectdir)"}, {force = true, expand = false})
    add_shflags("/PDBALTPATH:%_PDB%", {force = true})
    add_shflags("/DEBUG", "/OPT:REF", "/OPT:ICF", {force = true})
