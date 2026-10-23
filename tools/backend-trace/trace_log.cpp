#include "trace_log.h"

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
HMODULE g_self;
CRITICAL_SECTION g_logLock;
FILE* g_log;
}  // namespace

void InitTraceLog(HMODULE self)
{
    g_self = self;
    InitializeCriticalSection(&g_logLock);
}

void CloseTraceLog()
{
    EnterCriticalSection(&g_logLock);
    if (g_log)
    {
        fclose(g_log);
        g_log = nullptr;
    }
    LeaveCriticalSection(&g_logLock);
}

void Log(const char* format, ...)
{
    EnterCriticalSection(&g_logLock);
    if (!g_log)
    {
        char path[MAX_PATH];
        SiblingPath("dcogl-trace.log", path, sizeof path);
        g_log = _fsopen(path, "w", _SH_DENYWR);
    }
    if (g_log)
    {
        va_list args;
        va_start(args, format);
        vfprintf(g_log, format, args);
        va_end(args);
        fflush(g_log);
    }
    LeaveCriticalSection(&g_logLock);
}

void FormatTimestamp(char* out, size_t size)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    _snprintf_s(out, size, _TRUNCATE, "%02u:%02u:%02u.%03u", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
}

void SiblingPath(const char* fileName, char* out, size_t size)
{
    GetModuleFileNameA(g_self, out, static_cast<DWORD>(size));
    char* slash = strrchr(out, '\\');
    char* name = slash ? slash + 1 : out;
    strcpy_s(name, size - (name - out), fileName);
}

void DescribeAddress(uintptr_t address, char* out, size_t size)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) && module)
    {
        char path[MAX_PATH];
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* slash = strrchr(path, '\\');
        _snprintf_s(out, size, _TRUNCATE, "%s+0x%x", slash ? slash + 1 : path,
                    static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(module)));
    }
    else
    {
        _snprintf_s(out, size, _TRUNCATE, "0x%08x", static_cast<unsigned>(address));
    }
}
