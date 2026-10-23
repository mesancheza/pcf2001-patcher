# The mods tool: installs and removes optional changes to how the game plays, one by one, on top of the patch (see
# docs/MODS.md). The mods themselves are listed in src/patcher/mods.cpp, which the patcher shares.

# Version numbers for the program's version information: "0.1.0" -> 0,1,0,0.
string(REPLACE "." "," MODS_VERSION_COMMAS "${PATCH_VERSION_STRING}")
string(REGEX MATCHALL "," version_commas "${MODS_VERSION_COMMAS}")
list(LENGTH version_commas version_comma_count)
if(version_comma_count LESS 3)
    string(APPEND MODS_VERSION_COMMAS ",0")
endif()

add_executable(mods WIN32
    main.cpp
    mods.rc
    mods.manifest)
set_target_properties(mods PROPERTIES OUTPUT_NAME pcf2001-mods)
target_compile_definitions(mods PRIVATE
    MODS_VERSION_COMMAS=${MODS_VERSION_COMMAS}
    MODS_VERSION_STRING="${PATCH_VERSION_STRING}")
target_link_libraries(mods PRIVATE patch_core comctl32 shell32 ole32)
# The UAC level comes from mods.manifest, not from the linker's default.
target_link_options(mods PRIVATE /MANIFESTUAC:NO)
install(FILES $<TARGET_FILE:mods> DESTINATION .)
