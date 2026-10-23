#include "backend_log.h"

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
// Normal play writes a few kilobytes per hour; the limit only guards against something repeating without end.
constexpr long kMaxLogBytes = 4 * 1024 * 1024;

HMODULE g_self;
FILE* g_log;
bool g_full;
SRWLOCK g_lock = SRWLOCK_INIT;

void PathNextToDll(const char* name, char* path)
{
    GetModuleFileNameA(g_self, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    strcpy_s(slash ? slash + 1 : path, MAX_PATH - (slash ? slash + 1 - path : 0), name);
}

void Open()
{
    // The previous session's log is kept: after a crash, players often start the game again before reporting it.
    char path[MAX_PATH];
    char old[MAX_PATH];
    PathNextToDll("dcogl.log", path);
    PathNextToDll("dcogl.old.log", old);
    MoveFileExA(path, old, MOVEFILE_REPLACE_EXISTING);
    g_log = _fsopen(path, "w", _SH_DENYWR);
}
}  // namespace

void InitBackendLog(HMODULE self)
{
    g_self = self;
}

void BackendLog(const char* format, ...)
{
    AcquireSRWLockExclusive(&g_lock);
    if (!g_log && !g_full)
        Open();
    if (g_log && ftell(g_log) > kMaxLogBytes)
    {
        fputs("log size limit reached; nothing more is written in this session\n", g_log);
        fclose(g_log);
        g_log = nullptr;
        g_full = true;
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
