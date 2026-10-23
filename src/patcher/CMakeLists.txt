# The patcher: applies the fixes for modern Windows to an installed PC FÚTBOL 2001 v3.6 (see docs/PATCHER.md).
# It carries the new graphics component (src/backend, dcogl.dll) inside, so players get a single program.

# Version numbers for the program's version information: "0.1.0" -> 0,1,0,0.
string(REPLACE "." "," PATCHER_VERSION_COMMAS "${PATCH_VERSION_STRING}")
string(REGEX MATCHALL "," version_commas "${PATCHER_VERSION_COMMAS}")
list(LENGTH version_commas version_comma_count)
if(version_comma_count LESS 3)
    string(APPEND PATCHER_VERSION_COMMAS ",0")
endif()

add_executable(patch WIN32
    main.cpp
    patch.cpp
    patcher.rc
    patcher.manifest)
set_target_properties(patch PROPERTIES OUTPUT_NAME pcf2001-patch)
target_compile_definitions(patch PRIVATE
    PATCH_VERSION="${PATCH_VERSION_STRING}"
    PATCHER_VERSION_COMMAS=${PATCHER_VERSION_COMMAS}
    PATCHER_VERSION_STRING="${PATCH_VERSION_STRING}"
    DCOGL_DLL_PATH="$<TARGET_FILE:backend>")
# Rebuild the resources whenever the component changes.
add_dependencies(patch backend)
set_source_files_properties(patcher.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/src/backend/dcogl.dll")
# Shared code: registry lookup of the game folder, hashes, text, log.
target_link_libraries(patch PRIVATE installer_core comctl32 shell32 ole32)
# The UAC level comes from patcher.manifest, not from the linker's default.
target_link_options(patch PRIVATE /MANIFESTUAC:NO)
install(FILES $<TARGET_FILE:patch> DESTINATION .)
