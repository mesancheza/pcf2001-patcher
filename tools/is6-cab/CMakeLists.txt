# Lists or extracts InstallShield 6 archives (development tool, not part of the patch).
add_executable(is6-cab is6_cab.cpp)
target_link_libraries(is6-cab PRIVATE installer_core)
