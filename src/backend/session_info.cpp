#include "session_info.h"

#include "backend_log.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef PATCH_VERSION
#define PATCH_VERSION "unknown"
#endif

namespace
{
uint32_t g_crcTable[256];

void InitCrcTable()
{
    for (uint32_t i = 0; i < 256; ++i)
    {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crcTable[i] = c;
    }
}

// CRC-32 and size of a file, so modified or unusual game files stand out. Returns false if it can't be read.
bool FileCrc(const char* path, uint32_t& crc, uint64_t& size)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    static unsigned char buffer[64 * 1024];
    crc = 0xFFFFFFFFu;
    size = 0;
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof buffer, &read, nullptr) && read > 0)
    {
        for (DWORD i = 0; i < read; ++i)
            crc = g_crcTable[(crc ^ buffer[i]) & 0xFF] ^ (crc >> 8);
        size += read;
    }
    CloseHandle(file);
    crc ^= 0xFFFFFFFFu;
    return true;
}

// The game folder: dcogl.dll is installed next to manager.exe. Ends with a backslash.
void GameFolder(char* folder)
{
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&GameFolder), &self);
    GetModuleFileNameA(self, folder, MAX_PATH);
    char* slash = strrchr(folder, '\\');
    if (slash)
        slash[1] = 0;
    else
        folder[0] = 0;
}

void LogFile(const char* folder, const char* name)
{
    char path[MAX_PATH];
    _snprintf_s(path, sizeof path, _TRUNCATE, "%s%s", folder, name);
    uint32_t crc;
    uint64_t size;
    if (FileCrc(path, crc, size))
        BackendLog("  %s: %llu bytes, crc32 %08x\n", name, size, crc);
    else
        BackendLog("  %s: missing\n", name);
}

void LogWindows()
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version{sizeof version};
    // GetVersionEx reports what the compatibility settings pretend; ntdll tells the truth.
    if (auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion")))
        rtlGetVersion(&version);
    DWORD revision = 0;
    DWORD bytes = sizeof revision;
    RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "UBR", RRF_RT_REG_DWORD, nullptr,
                 &revision, &bytes);
    char display[32] = "";
    bytes = sizeof display;
    RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion", RRF_RT_REG_SZ,
                 nullptr, display, &bytes);
    MEMORYSTATUSEX memory{sizeof memory};
    GlobalMemoryStatusEx(&memory);
    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    BackendLog("Windows %lu.%lu.%lu.%lu %s, %lu processors, %llu MB memory\n", version.dwMajorVersion, version.dwMinorVersion,
               version.dwBuildNumber, revision, display, system.dwNumberOfProcessors, memory.ullTotalPhys >> 20);

    // Compatibility modes (e.g. Windows XP) change how the game behaves; worth knowing when reading a report.
    char layer[256];
    if (GetEnvironmentVariableA("__COMPAT_LAYER", layer, sizeof layer))
        BackendLog("compatibility mode: %s\n", layer);
    BackendLog("screen: %dx%d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}
}  // namespace

void LogSessionInfo()
{
    InitCrcTable();
    BackendLog("PC FUTBOL 2001 patch %s (dcogl.dll)\n", PATCH_VERSION);
    LogWindows();

    // Only the arguments: the program path may contain the Windows user name.
    const char* arguments = GetCommandLineA();
    const bool quoted = *arguments == '"';
    arguments = quoted ? strchr(arguments + 1, '"') : strchr(arguments, ' ');
    arguments = arguments ? arguments + 1 : "";
    while (*arguments == ' ')
        ++arguments;
    BackendLog("arguments: %s\n", arguments);

    char folder[MAX_PATH];
    GameFolder(folder);
    char updatePath[MAX_PATH];
    _snprintf_s(updatePath, sizeof updatePath, _TRUNCATE, "%supdate.ini", folder);
    char gameVersion[32] = "unknown";
    if (FILE* update = nullptr; fopen_s(&update, updatePath, "r") == 0 && update)
    {
        if (fgets(gameVersion, sizeof gameVersion, update))
            gameVersion[strcspn(gameVersion, "\r\n")] = 0;
        fclose(update);
    }
    BackendLog("game %s, files:\n", gameVersion);
    static const char* const kGameFiles[] = {"manager.exe", "gfxwin.dll", "Utils.dll", "d2s2.dll", "dcd3d.dll", "dcsfx.dll"};
    for (const char* name : kGameFiles)
        LogFile(folder, name);

    // The database: the .fdi files (teams, players, coaches, stadiums) identify which update or community database is
    // installed.
    BackendLog("database:\n");
    char pattern[MAX_PATH];
    _snprintf_s(pattern, sizeof pattern, _TRUNCATE, "%sDBDAT\\*.fdi", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
    {
        BackendLog("  no DBDAT\\*.fdi files\n");
        return;
    }
    do
    {
        char name[MAX_PATH];
        _snprintf_s(name, sizeof name, _TRUNCATE, "DBDAT\\%s", found.cFileName);
        LogFile(folder, name);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}
