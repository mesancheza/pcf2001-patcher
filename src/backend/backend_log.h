// The patch's log: dcogl.log next to the DLL, so players can attach it when they report a problem. Only notable
// events are written (setup, errors, faults, periodic statistics), so the file stays small during normal play. The
// previous session is kept as dcogl.old.log. Never write anything that identifies the player, such as full paths
// (they usually contain the Windows user name).
#pragma once

#include <windows.h>

void InitBackendLog(HMODULE self);
void BackendLog(const char* format, ...);
