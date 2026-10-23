cmake_minimum_required(VERSION 3.25)
project(pcf2001_patcher LANGUAGES CXX)

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

# The patch
add_subdirectory(src/backend)

# Development tools (not part of the shipped patch)
add_subdirectory(tools/backend-trace)
