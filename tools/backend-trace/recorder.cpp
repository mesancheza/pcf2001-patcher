#include "recorder.h"

#include "heap_tracker.h"
#include "trace_log.h"

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <intrin.h>

namespace
{
constexpr DWORD kCppExceptionCode = 0xE06D7363;   // raised by MSVC's throw
constexpr DWORD kCppExceptionMagic = 0x19930520;
constexpr int kMaxLoggedExceptions = 300;          // the game throws exceptions as part of normal control flow
constexpr int kMaxDumps = 2;
constexpr int kStackScanDwords = 512;
constexpr int kStackScanFrames = 24;
constexpr unsigned kRingSize = 64;                  // power of two

struct CallRecord
{
    DWORD tick;
    int slot;
    uintptr_t caller;
    const void* self;
};

CallRecord g_ring[kRingSize];
volatile LONG g_ringNext;
PVOID g_handler;
volatile LONG g_loggedExceptions;
volatile LONG g_dumpsWritten;
__declspec(thread) bool t_inHandler;

// Reads a 32-bit value, returning false instead of faulting on an invalid address.
bool SafeRead(const void* address, uint32_t& value)
{
    __try
    {
        value = *static_cast<const volatile uint32_t*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool IsExecutable(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof info) || info.State != MEM_COMMIT)
        return false;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & exec) != 0;
}

// True if the instruction just before `address` is a CALL, i.e. `address` looks like a genuine return address.
bool FollowsCall(uintptr_t address)
{
    __try
    {
        const auto* code = reinterpret_cast<const unsigned char*>(address);
        if (code[-5] == 0xE8)                                       // call rel32
            return true;
        if (code[-6] == 0xFF && (code[-5] & 0x38) == 0x10)          // call [disp32], call [reg+disp32]
            return true;
        if (code[-3] == 0xFF && (code[-2] & 0x38) == 0x10)          // call [reg+disp8]
            return true;
        if (code[-2] == 0xFF && (code[-1] & 0x38) == 0x10)          // call reg, call [reg]
            return true;
        if (code[-7] == 0xFF && (code[-6] & 0x38) == 0x10)          // call [reg+reg*s+disp32]
            return true;
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void LogRecentCalls()
{
    const LONG next = g_ringNext;
    const unsigned count = next < static_cast<LONG>(kRingSize) ? static_cast<unsigned>(next) : kRingSize;
    Log("  last %u device calls (oldest first):\n", count);
    for (unsigned i = 0; i < count; ++i)
    {
        const CallRecord& record = g_ring[(next - count + i) & (kRingSize - 1)];
        char where[128];
        DescribeAddress(record.caller, where, sizeof where);
        Log("    t=%lu slot %3d this=%p from %s\n", record.tick, record.slot, record.self, where);
    }
}

// MSVC C++ exception: name of the thrown type, from the ThrowInfo passed to RaiseException.
void DescribeCppException(const EXCEPTION_RECORD* record, char* out, size_t size)
{
    _snprintf_s(out, size, _TRUNCATE, "?");
    if (record->NumberParameters < 3 || record->ExceptionInformation[0] != kCppExceptionMagic)
        return;
    __try
    {
        const auto* throwInfo = reinterpret_cast<const uint32_t*>(record->ExceptionInformation[2]);
        const auto* catchableTypes = reinterpret_cast<const uint32_t*>(throwInfo[3]);
        if (catchableTypes[0] < 1)
            return;
        const auto* firstType = reinterpret_cast<const uint32_t*>(catchableTypes[1]);
        const auto* typeDescriptor = reinterpret_cast<const char*>(firstType[1]);
        _snprintf_s(out, size, _TRUNCATE, "%s", typeDescriptor + 8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// The game's own exception objects start with an error code followed by a text message; log both if present.
void LogCppExceptionObject(const EXCEPTION_RECORD* record)
{
    if (record->NumberParameters < 2)
        return;
    const auto* object = reinterpret_cast<const unsigned char*>(record->ExceptionInformation[1]);
    uint32_t code = 0;
    if (!object || !SafeRead(object, code))
        return;
    char text[160] = {};
    __try
    {
        for (size_t i = 0; i + 1 < sizeof text; ++i)
        {
            const unsigned char c = object[4 + i];
            if (c == 0)
                break;
            if (c < 0x20 && c != '\t')
            {
                text[0] = 0;  // not a text message
                break;
            }
            text[i] = static_cast<char>(c);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        text[0] = 0;
    }
    Log("  object: first dword 0x%08x%s%s%s\n", code, text[0] ? ", text \"" : "", text, text[0] ? "\"" : "");
}

void WriteDump(EXCEPTION_POINTERS* pointers)
{
    const LONG index = InterlockedIncrement(&g_dumpsWritten);
    if (index > kMaxDumps)
        return;

    HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using WriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                      PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    auto writeDump = dbghelp ? reinterpret_cast<WriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (!writeDump)
    {
        Log("  (dbghelp.dll unavailable, no dump written)\n");
        return;
    }

    char name[64];
    _snprintf_s(name, sizeof name, _TRUNCATE, "dcogl-crash-%ld.dmp", index);
    char path[MAX_PATH];
    SiblingPath(name, path, sizeof path);
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        Log("  cannot create %s (error %lu)\n", path, GetLastError());
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info = {GetCurrentThreadId(), pointers, FALSE};
    const BOOL ok = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                              static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData |
                                                         MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                              &info, nullptr, nullptr);
    CloseHandle(file);
    Log("  dump %s: %s\n", ok ? "written" : "FAILED", path);
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* pointers)
{
    const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
    const DWORD code = record->ExceptionCode;
    const bool isCpp = code == kCppExceptionCode;
    const bool isError = (code & 0xF0000000) == 0xC0000000;  // access violations and other faults
    if ((!isCpp && !isError) || t_inHandler)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_loggedExceptions) > kMaxLoggedExceptions && !isError)
        return EXCEPTION_CONTINUE_SEARCH;

    t_inHandler = true;
    const DWORD lastError = GetLastError();
    const CONTEXT* context = pointers->ContextRecord;

    char time[16];
    FormatTimestamp(time, sizeof time);
    char where[128];
    DescribeAddress(reinterpret_cast<uintptr_t>(record->ExceptionAddress), where, sizeof where);

    if (isCpp)
    {
        char type[128];
        DescribeCppException(record, type, sizeof type);
        Log("[%s] EXCEPTION C++ %s, thread %lu, thrown at %s\n", time, type, GetCurrentThreadId(), where);
        LogCppExceptionObject(record);
        LogStackScan("  thrown from", reinterpret_cast<const uint32_t*>(context->Esp));
    }
    else
    {
        Log("[%s] EXCEPTION 0x%08lx at %s, thread %lu\n", time, code, where, GetCurrentThreadId());
        if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        {
            const ULONG_PTR kind = record->ExceptionInformation[0];
            Log("  %s address 0x%08x\n", kind == 0 ? "read from" : kind == 1 ? "write to" : "execute at",
                static_cast<unsigned>(record->ExceptionInformation[1]));
        }
        Log("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx eip=%08lx\n",
            context->Eax, context->Ebx, context->Ecx, context->Edx, context->Esi, context->Edi, context->Ebp,
            context->Esp, context->Eip);
        LogStackScan("  call stack (scan)", reinterpret_cast<const uint32_t*>(context->Esp));
        LogRecentCalls();

        // Stale pointers usually sit in a register, or are the faulting address itself.
        const uintptr_t faultAddress = record->NumberParameters >= 2 ? record->ExceptionInformation[1] : 0;
        const uintptr_t suspects[] = {context->Eax, context->Ebx, context->Ecx, context->Edx, context->Esi,
                                      context->Edi, context->Ebp, faultAddress};
        const char* const labels[] = {"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "fault address"};
        ReportFreedBlocks(suspects, labels, static_cast<int>(sizeof suspects / sizeof suspects[0]));

        WriteDump(pointers);
    }

    SetLastError(lastError);
    t_inHandler = false;
    return EXCEPTION_CONTINUE_SEARCH;  // never change how the game handles its own exceptions
}
}  // namespace

void InstallRecorder()
{
    if (!g_handler)
        g_handler = AddVectoredExceptionHandler(1, OnException);
}

void RemoveRecorder()
{
    if (g_handler)
    {
        RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
    }
}

void RecordDeviceCall(int slot, uintptr_t caller, const void* self)
{
    const LONG index = InterlockedIncrement(&g_ringNext) - 1;
    CallRecord& record = g_ring[index & (kRingSize - 1)];
    record.tick = GetTickCount();
    record.slot = slot;
    record.caller = caller;
    record.self = self;
}

void LogStackScan(const char* title, const uint32_t* stack)
{
    // Frame pointers can't be trusted in this game's code, so look for values on the stack that point just after
    // a CALL instruction in executable memory. Some entries may be stale, but real callers are always among them.
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto* end = static_cast<const uint32_t*>(tib->StackBase);
    Log("%s:\n", title);
    int frames = 0;
    for (int i = 0; i < kStackScanDwords && stack + i < end && frames < kStackScanFrames; ++i)
    {
        uint32_t value;
        if (!SafeRead(stack + i, value) || value < 0x10000 || !IsExecutable(value) || !FollowsCall(value))
            continue;
        char where[128];
        DescribeAddress(value, where, sizeof where);
        Log("    [esp+0x%03x] %s\n", i * 4, where);
        ++frames;
    }
}
