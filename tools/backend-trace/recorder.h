// "Black box" crash recorder for the backend tracer: logs exceptions as they are raised (before the game's own
// error handling runs), keeps a ring buffer of the most recent device calls, and records the code path that led
// to a device being destroyed. Development tool, see docs/TOOLS.md.
#pragma once

#include <cstdint>

void InstallRecorder();
void RemoveRecorder();

// Cheap enough to call on every device method call.
void RecordDeviceCall(int slot, uintptr_t caller, const void* self);

// Logs the likely return addresses found on the stack, starting at `stack`, under the given title.
void LogStackScan(const char* title, const uint32_t* stack);
