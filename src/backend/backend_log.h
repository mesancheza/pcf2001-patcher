// Minimal log for the backend: dcogl.log next to the DLL. Only notable events are written (setup, errors, first
// use of a method that isn't implemented), so the file stays small during normal play.
#pragma once

#include <windows.h>

void InitBackendLog(HMODULE self);
void BackendLog(const char* format, ...);
