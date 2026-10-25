#include "diagnostics.h"

#include "backend_log.h"
#include "memory_compat.h"
#include "surface.h"

#include <dbghelp.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr DWORD kCppExceptionCode = 0xE06D7363;  // raised by MSVC's throw
constexpr DWORD kCppExceptionMagic = 0x19930520;
constexpr LONG kMaxCppExceptionsLogged = 40;     // the game also throws exceptions as normal control flow
constexpr LONG kMaxFaultsLogged = 20;
constexpr DWORD kDumpTimeoutMs = 2 * 60 * 1000;

volatile LONG g_cppExceptions;
volatile LONG g_faults;
__declspec(thread) bool t_inHandler;

HMODULE g_self;
char g_folder[MAX_PATH];  // this DLL's folder, the game's, with a trailing backslash
// [diagnostics] dump_on_fault = N: the first N faults in the game's own code each write a full memory dump. The game
// catches its faults and shows "La aplicación no puede continuar", so Windows never writes one.
LONG g_dumpsAllowed;
volatile LONG g_dumpsWritten;
// [diagnostics] memory_map = N: every N memory reports (one every 30 s of play), a list of every region of the address
// space in dcogl-memory-NNN.tsv, to see what grows over a long session.
LONG g_memoryMapEvery;
LONG g_memoryReports;

struct DumpRequest
{
    EXCEPTION_POINTERS* pointers;
    DWORD threadId;
    char path[MAX_PATH];
    bool written;
};
DumpRequest g_dumpRequest;  // static: it must outlive a dump that times out

void Describe(uintptr_t address, char* out, size_t size)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) && module)
    {
        char path[MAX_PATH];
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* slash = strrchr(path, '\\');
        _snprintf_s(out, size, _TRUNCATE, "%s+0x%x", slash ? slash + 1 : path, static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(module)));
    }
    else
    {
        _snprintf_s(out, size, _TRUNCATE, "0x%08x", static_cast<unsigned>(address));
    }
}

// Name of the thrown C++ type, from the ThrowInfo that MSVC passes to RaiseException.
void CppTypeName(const EXCEPTION_RECORD* record, char* out, size_t size)
{
    _snprintf_s(out, size, _TRUNCATE, "?");
    if (record->NumberParameters < 3 || record->ExceptionInformation[0] != kCppExceptionMagic)
        return;
    __try
    {
        const auto* throwInfo = reinterpret_cast<const uint32_t*>(record->ExceptionInformation[2]);
        const auto* catchable = reinterpret_cast<const uint32_t*>(throwInfo[3]);
        if (catchable[0] >= 1)
            _snprintf_s(out, size, _TRUNCATE, "%s", reinterpret_cast<const char*>(reinterpret_cast<const uint32_t*>(catchable[1])[1]) + 8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// The game's exception objects are an error code followed by a message.
void CppExceptionText(const EXCEPTION_RECORD* record, uint32_t& code, char* text, size_t size)
{
    code = 0;
    text[0] = 0;
    if (record->NumberParameters < 2)
        return;
    __try
    {
        const auto* object = reinterpret_cast<const unsigned char*>(record->ExceptionInformation[1]);
        code = *reinterpret_cast<const uint32_t*>(object);
        size_t i = 0;
        for (; i + 1 < size; ++i)
        {
            const unsigned char c = object[4 + i];
            if (c == 0)
                break;
            if (c < 0x20)
            {
                i = 0;
                break;
            }
            text[i] = static_cast<char>(c);
        }
        text[i] = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        text[0] = 0;
    }
}

// Code in the game's folder (manager.exe, gfxwin.dll, ...) other than this DLL, or in no module at all (a jump through
// a bad pointer). Faults in Windows' or the drivers' code are often handled on purpose and aren't dumped.
bool IsGameCode(uintptr_t address)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &module) ||
        !module)
        return true;
    if (module == g_self)
        return false;
    char path[MAX_PATH];
    GetModuleFileNameA(module, path, MAX_PATH);
    return _strnicmp(path, g_folder, strlen(g_folder)) == 0;
}

DWORD WINAPI WriteDumpThread(void*)
{
    using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                              PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    HMODULE dbghelp = LoadLibraryExA("dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto writeDump = dbghelp ? reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (!writeDump)
        return 0;
    HANDLE file = CreateFileA(g_dumpRequest.path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    MINIDUMP_EXCEPTION_INFORMATION exception{g_dumpRequest.threadId, g_dumpRequest.pointers, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo | MiniDumpWithHandleData |
                                                 MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    g_dumpRequest.written = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &exception, nullptr, nullptr) != FALSE;
    CloseHandle(file);
    return 0;
}

void WriteDump(EXCEPTION_POINTERS* pointers)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    g_dumpRequest.pointers = pointers;
    g_dumpRequest.threadId = GetCurrentThreadId();
    g_dumpRequest.written = false;
    _snprintf_s(g_dumpRequest.path, sizeof g_dumpRequest.path, _TRUNCATE, "%sdcogl-%04u%02u%02u-%02u%02u%02u.dmp", g_folder,
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    // From another thread: the faulting one may be short of stack. The game waits meanwhile, frozen as it was.
    if (HANDLE thread = CreateThread(nullptr, 0, WriteDumpThread, nullptr, 0, nullptr))
    {
        WaitForSingleObject(thread, kDumpTimeoutMs);
        CloseHandle(thread);
    }
    BackendLog("diagnostics: %s %s\n", g_dumpRequest.written ? "dump written to" : "could not write the dump",
               g_dumpRequest.path + strlen(g_folder));
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* pointers)
{
    const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
    const DWORD code = record->ExceptionCode;
    if (t_inHandler)
        return EXCEPTION_CONTINUE_SEARCH;
    t_inHandler = true;
    const DWORD lastError = GetLastError();
    char where[128];
    Describe(reinterpret_cast<uintptr_t>(record->ExceptionAddress), where, sizeof where);

    if (code == kCppExceptionCode)
    {
        if (InterlockedIncrement(&g_cppExceptions) <= kMaxCppExceptionsLogged)
        {
            char type[96];
            char text[160];
            uint32_t errorCode;
            CppTypeName(record, type, sizeof type);
            CppExceptionText(record, errorCode, text, sizeof text);
            // The return address on the stack identifies the code that threw.
            char thrower[128] = "?";
            __try
            {
                Describe(*reinterpret_cast<const uintptr_t*>(pointers->ContextRecord->Ebp + 4), thrower, sizeof thrower);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
            BackendLog("exception: C++ %s, code 0x%08x%s%s%s, near %s\n", type, errorCode, text[0] ? ", \"" : "", text,
                       text[0] ? "\"" : "", thrower);
        }
    }
    else if ((code & 0xF0000000) == 0xC0000000)
    {
        if (InterlockedIncrement(&g_faults) <= kMaxFaultsLogged)
        {
            if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
            {
                BackendLog("exception: access violation at %s, %s address 0x%08x\n", where,
                           record->ExceptionInformation[0] == 0 ? "reading" : record->ExceptionInformation[0] == 1 ? "writing" : "executing",
                           static_cast<unsigned>(record->ExceptionInformation[1]));
            }
            else
            {
                BackendLog("exception: 0x%08lx at %s\n", code, where);
            }
        }
        // Not on a stack overflow: there is no stack left to wait on another thread.
        if (g_dumpsAllowed > 0 && code != EXCEPTION_STACK_OVERFLOW && IsGameCode(reinterpret_cast<uintptr_t>(record->ExceptionAddress)) &&
            InterlockedIncrement(&g_dumpsWritten) <= g_dumpsAllowed)
            WriteDump(pointers);
    }
    SetLastError(lastError);
    t_inHandler = false;
    return EXCEPTION_CONTINUE_SEARCH;  // only observe; the game handles its exceptions as it always did
}
}  // namespace

void InstallDiagnostics(HMODULE self)
{
    g_self = self;
    GetModuleFileNameA(self, g_folder, MAX_PATH);
    if (char* slash = strrchr(g_folder, '\\'))
        slash[1] = 0;
    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof ini, _TRUNCATE, "%sdcogl.ini", g_folder);
    g_dumpsAllowed = static_cast<LONG>(GetPrivateProfileIntA("diagnostics", "dump_on_fault", 0, ini));
    if (g_dumpsAllowed > 0)
        BackendLog("diagnostics: a full memory dump for each of the first %ld faults in the game's code\n", g_dumpsAllowed);
    g_memoryMapEvery = static_cast<LONG>(GetPrivateProfileIntA("diagnostics", "memory_map", 0, ini));
    if (g_memoryMapEvery > 0)
        BackendLog("diagnostics: a map of the address space every %ld memory reports\n", g_memoryMapEvery);
    AddVectoredExceptionHandler(1, OnException);
}

void LogProcessExit(bool processTerminating)
{
    BackendLog("%s\n", processTerminating ? "process exiting" : "backend unloaded");
}

void LogProcessMemory()
{
    // A walk over the address space: a few thousand regions, a few milliseconds. Allocations (regions sharing an
    // allocation base) are counted by kind: program images, mapped sections (files, or anonymous like the surfaces'
    // pixels), and private memory (heaps, and what Direct3D and the graphics driver reserve).
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    uintptr_t address = reinterpret_cast<uintptr_t>(system.lpMinimumApplicationAddress);
    const uintptr_t end = reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress);
    uint64_t free = 0, largestFree = 0, image = 0, mapped = 0, privateReserved = 0, privateCommitted = 0;
    unsigned mappedCount = 0, privateCount = 0;
    const void* allocation = nullptr;
    MEMORY_BASIC_INFORMATION region;
    FILE* map = nullptr;
    if (g_memoryMapEvery && ++g_memoryReports % g_memoryMapEvery == 0)
    {
        char path[MAX_PATH];
        _snprintf_s(path, sizeof path, _TRUNCATE, "%sdcogl-memory-%03ld.tsv", g_folder, g_memoryReports / g_memoryMapEvery);
        if (fopen_s(&map, path, "w") == 0 && map)
            fprintf(map, "allocation\tregion\tsize_kb\tstate\ttype\tname\n");
    }
    while (address < end && VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof region))
    {
        const bool first = region.State != MEM_FREE && region.AllocationBase != allocation;
        if (region.State == MEM_FREE)
        {
            free += region.RegionSize;
            largestFree = region.RegionSize > largestFree ? region.RegionSize : largestFree;
        }
        else if (region.Type == MEM_IMAGE)
            image += region.RegionSize;
        else if (region.Type == MEM_MAPPED)
        {
            mapped += region.RegionSize;
            mappedCount += first;
        }
        else
        {
            privateReserved += region.RegionSize;
            privateCommitted += region.State == MEM_COMMIT ? region.RegionSize : 0;
            privateCount += first;
        }
        if (map && region.State != MEM_FREE)
        {
            char name[MAX_PATH] = "";
            if (first && region.Type != MEM_PRIVATE)
            {
                char full[MAX_PATH];
                if (GetMappedFileNameA(GetCurrentProcess(), region.AllocationBase, full, MAX_PATH))
                {
                    const char* slash = strrchr(full, '\\');
                    strcpy_s(name, slash ? slash + 1 : full);
                }
            }
            fprintf(map, "%p\t%p\t%Iu\t%s\t%s\t%s\n", region.AllocationBase, region.BaseAddress, region.RegionSize / 1024,
                    region.State == MEM_COMMIT ? "commit" : "reserve",
                    region.Type == MEM_IMAGE ? "image" : region.Type == MEM_MAPPED ? "mapped" : "private", name);
        }
        if (region.State != MEM_FREE)
            allocation = region.AllocationBase;
        const uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= address)
            break;
        address = next;
    }
    if (map)
        fclose(map);
    NoteAddressSpace(free, largestFree);
    PROCESS_MEMORY_COUNTERS_EX counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof counters);
    const SurfaceStats surfaces = GetSurfaceStats();
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    constexpr double kMB = 1024.0 * 1024.0;
    BackendLog("memory: address space %.0f MB free, largest free block %.1f MB; images %.0f MB, mapped %.0f MB (%u), private "
               "%.0f MB reserved (%u) of which %.0f MB in use; %.0f MB private bytes; %ld surfaces (%.1f MB of pixels, %.1f "
               "MB of address space; %ld with a GDI context); %lu GDI objects, %lu USER objects, %lu handles\n",
               free / kMB, largestFree / kMB, image / kMB, mapped / kMB, mappedCount, privateReserved / kMB, privateCount,
               privateCommitted / kMB, counters.PrivateUsage / kMB, surfaces.surfaces, surfaces.pixelBytes / kMB,
               surfaces.viewBytes / kMB, surfaces.withDC, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
               GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), handles);
    // With [memory] allocation_sites, the surfaces' creators too: every 10 reports (5 minutes) those that grew, every
    // 60 also those holding the most.
    static LONG siteReports;
    if (AllocationSitesEnabled() && ++siteReports % 10 == 0)
        LogSurfaceSites(siteReports % 60 == 0);
}
