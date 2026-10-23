# The game installer: installs PC FÚTBOL 2001 from the player's own CD (see docs/INSTALLER.md).

# Code shared by the installer and its development tools.
add_library(installer_core STATIC
    dinamic_updates.cpp
    game_files.cpp
    hashes.cpp
    inflate.cpp
    install_job.cpp
    is6_cabinet.cpp
    setup_log.cpp
    system_integration.cpp
    text.cpp)
target_include_directories(installer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(installer_core PUBLIC UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS SETUP_VERSION="${PATCH_VERSION_STRING}")
target_link_libraries(installer_core PUBLIC bcrypt cabinet)

# Version numbers for the program's version information: "0.1.0" -> 0,1,0,0.
string(REPLACE "." "," SETUP_VERSION_COMMAS "${PATCH_VERSION_STRING}")
string(REGEX MATCHALL "," version_commas "${SETUP_VERSION_COMMAS}")
list(LENGTH version_commas version_comma_count)
if(version_comma_count LESS 3)
    string(APPEND SETUP_VERSION_COMMAS ",0")
endif()

add_executable(setup WIN32
    main.cpp
    wizard.cpp
    installer.rc
    installer.manifest)
set_target_properties(setup PROPERTIES OUTPUT_NAME pcf2001-setup)
target_compile_definitions(setup PRIVATE
    SETUP_VERSION_COMMAS=${SETUP_VERSION_COMMAS}
    SETUP_VERSION_STRING="${PATCH_VERSION_STRING}")
target_link_libraries(setup PRIVATE installer_core comctl32 shell32 ole32 advapi32)
# The UAC level comes from installer.manifest, not from the linker's default.
target_link_options(setup PRIVATE /MANIFESTUAC:NO)
install(FILES $<TARGET_FILE:setup> DESTINATION .)
