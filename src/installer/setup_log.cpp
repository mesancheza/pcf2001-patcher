#include "setup_log.h"

#include "text.h"

#include <windows.h>
#include <share.h>

#include <cstdio>
#include <mutex>

namespace
{
std::mutex g_lock;
FILE* g_log;
std::string g_pending;  // lines written before the log file was opened
}  // namespace

void OpenSetupLog(const std::filesystem::path& path)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_log)
        fclose(g_log);
    g_log = _wfsopen(path.c_str(), L"ab", _SH_DENYWR);
    if (g_log && !g_pending.empty())
    {
        fwrite(g_pending.data(), 1, g_pending.size(), g_log);
        fflush(g_log);
        g_pending.clear();
    }
}

void SetupLog(const std::wstring& line)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    const std::string text = ToUtf8(Format(L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n", now.wYear, now.wMonth, now.wDay, now.wHour,
                                           now.wMinute, now.wSecond, line.c_str()));
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_log)
    {
        fwrite(text.data(), 1, text.size(), g_log);
        fflush(g_log);
    }
    else
    {
        g_pending += text;
    }
}

std::wstring LoggablePath(const std::filesystem::path& path, const std::filesystem::path& gameFolder)
{
    std::error_code error;
    const auto relative = std::filesystem::relative(path, gameFolder, error);
    if (!error && !relative.empty() && relative.native().rfind(L"..", 0) != 0)
        return relative.native();
    return path.filename().native();
}
