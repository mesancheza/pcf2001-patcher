// Logging shared by the backend tracer and the crash recorder. Output goes to dcogl-trace.log next to this DLL.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

void InitTraceLog(HMODULE self);
void CloseTraceLog();

// printf-style, thread-safe, flushed after every call.
void Log(const char* format, ...);

// Current local time as "HH:MM:SS.mmm", for correlating events with what the player was doing.
void FormatTimestamp(char* out, size_t size);

// Path of a file next to this DLL.
void SiblingPath(const char* fileName, char* out, size_t size);

// "module.dll+0x1234" if the address belongs to a loaded module, otherwise the raw address.
void DescribeAddress(uintptr_t address, char* out, size_t size);
