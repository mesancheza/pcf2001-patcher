cmake_minimum_required(VERSION 3.25)
project(pcf2001_patcher VERSION 0.1.0 LANGUAGES CXX)

# PC FÚTBOL 2001 is a 32-bit program, and everything we build is loaded into its process.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure with a 32-bit (x86) toolchain, e.g. by running build.bat.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static C runtime, so players don't need to install any Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/W4 /permissive- /utf-8)

# Reproducible builds: no timestamps in the output, so anyone with the same toolchain can rebuild a release
# and get byte-identical files (see docs/BUILDING.md).
add_compile_options(/Brepro)
add_link_options(/Brepro)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

# The version the built files report (e.g. in dcogl.log). Releases set it from the release branch's name
# (build.bat Release <version>, see docs/RELEASING.md); other builds use the project version above.
set(PCF_PATCH_VERSION "" CACHE STRING "Version reported by the built files; empty for the project version")
if(PCF_PATCH_VERSION STREQUAL "")
    set(PATCH_VERSION_STRING "${PROJECT_VERSION}")
else()
    set(PATCH_VERSION_STRING "${PCF_PATCH_VERSION}")
endif()

# What players get. What gets installed (cmake --install) is exactly what a release ships to them.
add_subdirectory(src/installer)  # pcf2001-setup.exe: installs the game from the CD
add_subdirectory(src/backend)    # dcogl.dll: the new graphics component, carried inside the patcher
add_subdirectory(src/patcher)    # pcf2001-patch.exe: applies the fixes
add_subdirectory(src/mods)       # pcf2001-mods.exe: installs optional mods on top of the fixes
install(FILES README.md LEEME.md LICENSE DESTINATION .)

# Development tools (not part of the shipped patch)
add_subdirectory(tools/backend-trace)
add_subdirectory(tools/is6-cab)
