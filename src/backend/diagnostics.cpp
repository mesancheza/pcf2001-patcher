#include "diagnostics.h"

#include "backend_log.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr DWORD kCppExceptionCode = 0xE06D7363;  // raised by MSVC's throw
constexpr DWORD kCppExceptionMagic = 0x19930520;
constexpr LONG kMaxCppExceptionsLogged = 40;     // the game also throws exceptions as normal control flow
constexpr LONG kMaxFaultsLogged = 20;

volatile LONG g_cppExceptions;
volatile LONG g_faults;
__declspec(thread) bool t_inHandler;

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
    }
    SetLastError(lastError);
    t_inHandler = false;
    return EXCEPTION_CONTINUE_SEARCH;  // only observe; the game handles its exceptions as it always did
}
}  // namespace

void InstallDiagnostics(HMODULE)
{
    AddVectoredExceptionHandler(1, OnException);
}

void LogProcessExit(bool processTerminating)
{
    BackendLog("%s\n", processTerminating ? "process exiting" : "backend unloaded");
}
