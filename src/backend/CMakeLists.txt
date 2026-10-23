# The new graphics backend (Direct3D 11). Built as dcogl.dll, the name of a backend the game recognizes but never
# shipped, so it replaces none of the game's files (see docs/DESIGN.md, D6).

# Shaders are compiled at build time and embedded, so nothing extra is needed at run time.
find_program(FXC fxc REQUIRED DOC "HLSL compiler from the Windows SDK")
set(SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/present.hlsl)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${GENERATED_DIR}/present_vs.h ${GENERATED_DIR}/present_ps.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${FXC} /nologo /T vs_4_0 /E VSMain /O3 /Vn g_presentVS /Fh ${GENERATED_DIR}/present_vs.h ${SHADER_SOURCE}
    COMMAND ${FXC} /nologo /T ps_4_0 /E PSMain /O3 /Vn g_presentPS /Fh ${GENERATED_DIR}/present_ps.h ${SHADER_SOURCE}
    DEPENDS ${SHADER_SOURCE}
    VERBATIM)

add_library(backend SHARED
    backend.def
    backend_log.cpp
    device.cpp
    exports.cpp
    input_mapping.cpp
    presenter.cpp
    surface.cpp
    ${GENERATED_DIR}/present_vs.h
    ${GENERATED_DIR}/present_ps.h)
set_target_properties(backend PROPERTIES OUTPUT_NAME dcogl)
target_include_directories(backend PRIVATE ${GENERATED_DIR})
target_compile_definitions(backend PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(backend PRIVATE d3d11 dxgi)
