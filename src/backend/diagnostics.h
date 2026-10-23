// Lightweight diagnostics written to dcogl.log, so that problems players report come with useful information:
// C++ exceptions thrown by the game (with the error text the game puts in them), faults, and how the process ended.
// Nothing here changes how the game handles its own errors.
#pragma once

#include <windows.h>

void InstallDiagnostics(HMODULE self);
void LogProcessExit(bool processTerminating);
