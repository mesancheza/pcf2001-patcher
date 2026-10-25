// Lightweight diagnostics written to dcogl.log, so that problems players report come with useful information:
// C++ exceptions thrown by the game (with the error text the game puts in them), faults, and how the process ended.
// Nothing here changes how the game handles its own errors.
#pragma once

#include <windows.h>

void InstallDiagnostics(HMODULE self);
void LogProcessExit(bool processTerminating);

// Writes the process's memory use to dcogl.log: its address space (the game is a 32-bit program: 2 GB at most, and a
// large surface needs one free stretch), private memory, the surfaces alive, and GDI and USER objects.
void LogProcessMemory();
