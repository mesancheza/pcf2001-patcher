// Tracing proxy for PC FÚTBOL 2001's graphics backend interface (development tool, not shipped).
//
// gfxwin.dll loads a graphics backend DLL by name (dcsf.dll, dcsfx.dll, dcd3d.dll or dcogl.dll) and talks to it
// through four exports plus a C++ "device" object whose vtable has 173 slots. This DLL is built as dcogl.dll, a
// name the game recognizes but never shipped, so nothing in the game folder gets replaced; it is selected by
// starting the game as `manager.exe dcogl.dll` (no trailing space: gfxwin compares the whole command line).
//
// It loads the real dcd3d.dll, forwards the exports, and swaps each device's vtable pointer for a copy whose
// entries log the call and then jump to the original method. The device object itself is left untouched, so
// any direct access to its fields from gfxwin.dll keeps working. It also runs the crash recorder (recorder.cpp).
//
// Output, next to this DLL (format described in docs/TOOLS.md):
//   dcogl-trace.log    - device calls, call sites, summaries, exceptions and shutdown call paths
//   dcogl-crash-N.dmp  - full memory dumps taken at the first faults

#include "heap_tracker.h"
#include "recorder.h"
#include "trace_log.h"

#include <windows.h>

#include <cstdint>
#include <intrin.h>

namespace
{
constexpr int kSlotCount = 173;
constexpr unsigned kLoggedCallsPerSlot = 40;
constexpr int kMaxCallersPerSlot = 32;
constexpr int kLoggedArgs = 6;

using CrearDeviceFn = void*(__cdecl*)(int index);
using DestruirDeviceFn = void(__cdecl*)(void** device);
using NumeroDispositivosFn = int(__cdecl*)();
using DescripcionDispositivoFn = const char*(__cdecl*)(int index);

struct SlotStats
{
    unsigned calls;
    int callerCount;
    uintptr_t callers[kMaxCallersPerSlot];
};

HMODULE g_inner;
CrearDeviceFn g_crearDevice;
DestruirDeviceFn g_destruirDevice;
NumeroDispositivosFn g_numeroDispositivos;
DescripcionDispositivoFn g_descripcionDispositivo;

bool g_fake3dModes;
void** g_originalVtable;
void* g_hookedVtable[kSlotCount + 1];  // [0] mirrors the RTTI locator that precedes the original vtable
SlotStats g_stats[kSlotCount];

CRITICAL_SECTION g_lock;

void WriteSummary(const char* reason)
{
    EnterCriticalSection(&g_lock);
    Log("---- summary (%s) ----\n", reason);
    for (int slot = 0; slot < kSlotCount; ++slot)
    {
        if (g_stats[slot].calls)
            Log("slot %3d +0x%03x calls=%u callSites=%d\n", slot, slot * 4, g_stats[slot].calls, g_stats[slot].callerCount);
    }
    LeaveCriticalSection(&g_lock);
}

// Called by CommonThunk before every device method. stack[0] is the caller's return address, stack[1..] the arguments.
void __cdecl TraceCall(int slot, void* self, const uint32_t* stack)
{
    const DWORD lastError = GetLastError();  // logging must not change what the game sees
    const uintptr_t caller = stack[0];
    RecordDeviceCall(slot, caller, self);
    HeapTrackerTick();

    EnterCriticalSection(&g_lock);
    SlotStats& stats = g_stats[slot];
    ++stats.calls;

    bool knownCaller = false;
    for (int i = 0; i < stats.callerCount && !knownCaller; ++i)
        knownCaller = stats.callers[i] == caller;

    char where[128];
    if (!knownCaller && stats.callerCount < kMaxCallersPerSlot)
    {
        stats.callers[stats.callerCount++] = caller;
        DescribeAddress(caller, where, sizeof where);
        Log("NEW  slot %3d +0x%03x from %s\n", slot, slot * 4, where);
    }
    if (stats.calls <= kLoggedCallsPerSlot)
    {
        DescribeAddress(caller, where, sizeof where);
        Log("CALL slot %3d +0x%03x #%u this=%p from %s args:", slot, slot * 4, stats.calls, self, where);
        for (int i = 1; i <= kLoggedArgs; ++i)
            Log(" %08x", stack[i]);
        Log("\n");
    }
    LeaveCriticalSection(&g_lock);
    SetLastError(lastError);
}
}  // namespace

// Original method addresses, indexed by slot; read by CommonThunk.
static void* g_originalSlots[kSlotCount];

// Entered with [esp] = slot number, [esp+4] = caller's return address, then the method's arguments; ECX = this.
// Logs the call with all registers and flags preserved, then continues into the original method as if called
// directly.
__declspec(naked) static void CommonThunk()
{
    __asm {
        pushfd
        pushad                          // slot is now at [esp+36], the return address at [esp+40]
        lea  eax, [esp + 40]
        push eax                        // stack pointer: return address followed by the arguments
        push ecx                        // this
        push dword ptr [esp + 44]       // slot
        call TraceCall
        add  esp, 12
        popad
        popfd
        push eax                        // replace the slot number with the original method's address...
        mov  eax, [esp + 4]
        mov  eax, dword ptr g_originalSlots[eax * 4]
        mov  [esp + 4], eax
        pop  eax
        ret                             // ...and "return" into it, leaving the caller's return address on top
    }
}

#define TRACE_THUNK(n) __declspec(naked) static void Thunk##n() { __asm push n __asm jmp CommonThunk }
#include "thunks.inc"
#undef TRACE_THUNK

#define TRACE_THUNK(n) reinterpret_cast<void*>(&Thunk##n),
static void* const kThunks[kSlotCount] = {
#include "thunks.inc"
};
#undef TRACE_THUNK

namespace
{
bool EnsureInner()
{
    if (g_inner)
        return true;

    // Optional dcogl.ini next to this DLL:
    //   [trace]
    //   backend=dcd3d.dll     ; which of the game's backends to wrap
    //   fake3dmodes=1         ; report the 2D mode list as 3D modes when the backend has none (see ReportModes3D)
    char ini[MAX_PATH];
    SiblingPath("dcogl.ini", ini, sizeof ini);
    char backend[64];
    GetPrivateProfileStringA("trace", "backend", "dcd3d.dll", backend, sizeof backend, ini);
    g_fake3dModes = GetPrivateProfileIntA("trace", "fake3dmodes", 0, ini) != 0;

    char path[MAX_PATH];
    SiblingPath(backend, path, sizeof path);
    g_inner = LoadLibraryA(path);
    if (!g_inner)
    {
        Log("cannot load %s (error %lu)\n", path, GetLastError());
        return false;
    }
    g_crearDevice = reinterpret_cast<CrearDeviceFn>(GetProcAddress(g_inner, "CrearDevice"));
    g_destruirDevice = reinterpret_cast<DestruirDeviceFn>(GetProcAddress(g_inner, "DestruirDevice"));
    g_numeroDispositivos = reinterpret_cast<NumeroDispositivosFn>(GetProcAddress(g_inner, "NumeroDispositivos"));
    g_descripcionDispositivo = reinterpret_cast<DescripcionDispositivoFn>(GetProcAddress(g_inner, "DescripcionDispositivo"));
    if (!g_crearDevice || !g_destruirDevice || !g_numeroDispositivos || !g_descripcionDispositivo)
    {
        Log("%s is missing backend exports\n", path);
        FreeLibrary(g_inner);
        g_inner = nullptr;
        return false;
    }
    Log("loaded %s%s\n", path, g_fake3dModes ? " (reporting 2D modes as 3D modes if it has none)" : "");
    return true;
}

// Slot 9 replacement. gfxwin only accepts a backend whose devices report at least one 3D-capable mode, because
// the game always asks for 3D. On modern Windows dcsfx.dll (the game's original default 2D backend) reports none,
// which is what makes the game fail to start. Its 3D methods are empty anyway: 3D matches run on dcd3d.dll as a
// separate 3D backend. So when the real list is empty, report the 2D mode list instead. Declared __fastcall with
// an unused EDX parameter so it can stand in for a __thiscall method (ECX = this).
using ModeListFn = void*(__thiscall*)(void* self);

void* __fastcall ReportModes3D(void* self, void* /*edx*/)
{
    void* modes3d = reinterpret_cast<ModeListFn>(g_originalSlots[9])(self);
    if (modes3d && *reinterpret_cast<const int*>(static_cast<const char*>(modes3d) + 4) > 0)
        return modes3d;
    return reinterpret_cast<ModeListFn>(g_originalSlots[8])(self);
}

void HookDevice(void* device, int index)
{
    void** vtable = *static_cast<void***>(device);
    EnterCriticalSection(&g_lock);
    if (!g_originalVtable)
    {
        g_originalVtable = vtable;
        g_hookedVtable[0] = vtable[-1];
        for (int slot = 0; slot < kSlotCount; ++slot)
        {
            g_originalSlots[slot] = vtable[slot];
            g_hookedVtable[slot + 1] = kThunks[slot];
        }
        if (g_fake3dModes)
            g_hookedVtable[9 + 1] = reinterpret_cast<void*>(&ReportModes3D);
        // Hooked objects point into this DLL from now on, so it must never be unloaded.
        HMODULE pinned;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCSTR>(&HookDevice), &pinned);
    }
    char time[16];
    FormatTimestamp(time, sizeof time);
    if (vtable == g_originalVtable)
    {
        *static_cast<void***>(device) = &g_hookedVtable[1];
        Log("[%s] device %d created at %p, tracing\n", time, index, device);
    }
    else
    {
        Log("[%s] device %d created at %p with unexpected vtable %p, not traced\n", time, index, device, vtable);
    }
    LeaveCriticalSection(&g_lock);
}
}  // namespace

extern "C" int __cdecl NumeroDispositivos()
{
    return EnsureInner() ? g_numeroDispositivos() : 0;
}

extern "C" const char* __cdecl DescripcionDispositivo(int index)
{
    return EnsureInner() ? g_descripcionDispositivo(index) : nullptr;
}

extern "C" void* __cdecl CrearDevice(int index)
{
    if (!EnsureInner())
        return nullptr;
    void* device = g_crearDevice(index);
    if (device)
        HookDevice(device, index);
    return device;
}

extern "C" void __cdecl DestruirDevice(void** device)
{
    char time[16];
    FormatTimestamp(time, sizeof time);
    Log("[%s] destroying device %p\n", time, device ? *device : nullptr);
    // Shows which code decided to shut graphics down (e.g. a normal quit, or the game giving up after an error).
    LogStackScan("  requested from", static_cast<const uint32_t*>(_AddressOfReturnAddress()));
    WriteSummary("device destroyed");
    if (EnsureInner())
        g_destruirDevice(device);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_lock);
        InitTraceLog(instance);
        InstallRecorder();
        InstallHeapTracker();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        RemoveRecorder();
        WriteSummary("unload");
        CloseTraceLog();
    }
    return TRUE;
}
