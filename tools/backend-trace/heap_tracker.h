// Heap tracker for the backend tracer (development tool, see docs/TOOLS.md).
//
// Intercepts HeapAlloc, HeapFree and HeapReAlloc in the game's modules and remembers every block that gets freed:
// its address, size, when, and which game code freed it. When the game faults on a stale pointer, the crash
// recorder asks the tracker whether the pointer falls inside a freed block, which identifies the code that freed
// memory still in use. It also logs how much memory the game frees per minute.
#pragma once

#include <windows.h>

#include <cstdint>

void InstallHeapTracker();

// Logs the per-minute statistics if a minute has passed since the last report. Cheap; call it often.
void HeapTrackerTick();

// Logs every freed block that contains one of the given addresses.
void ReportFreedBlocks(const uintptr_t* addresses, const char* const* labels, int count);
