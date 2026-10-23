#include "backend_log.h"

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
HMODULE g_self;
FILE* g_log;
SRWLOCK g_lock = SRWLOCK_INIT;
}  // namespace

void InitBackendLog(HMODULE self)
{
    g_self = self;
}

void BackendLog(const char* format, ...)
{
    AcquireSRWLockExclusive(&g_lock);
    if (!g_log)
    {
        char path[MAX_PATH];
        GetModuleFileNameA(g_self, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        strcpy_s(slash ? slash + 1 : path, MAX_PATH - (slash ? slash + 1 - path : 0), "dcogl.log");
        g_log = _fsopen(path, "w", _SH_DENYWR);
    }
    if (g_log)
    {
        SYSTEMTIME now;
        GetLocalTime(&now);
        fprintf(g_log, "[%02u:%02u:%02u.%03u] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        va_list args;
        va_start(args, format);
        vfprintf(g_log, format, args);
        va_end(args);
        fflush(g_log);
    }
    ReleaseSRWLockExclusive(&g_lock);
}
