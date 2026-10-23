#include "heap_tracker.h"

#include "trace_log.h"

#include <cstring>
#include <intrin.h>

namespace
{
constexpr unsigned kTableBits = 19;                        // 512K entries, about 19 MB
constexpr unsigned kTableSize = 1u << kTableBits;
constexpr unsigned kMaxProbes = 16;
constexpr int kCallers = 4;
constexpr int kStackScanDwords = 96;
constexpr DWORD kStatsIntervalMs = 60 * 1000;

struct FreeRecord
{
    uintptr_t address;     // 0 = empty slot
    uint32_t size;
    DWORD freedTick;
    uintptr_t freedBy[kCallers];
    DWORD reusedTick;      // 0 = not handed out again yet
    uintptr_t reusedBy;
};

struct CodeRange
{
    uintptr_t start;
    uintptr_t end;
};

using HeapAllocFn = LPVOID(WINAPI*)(HANDLE, DWORD, SIZE_T);
using HeapFreeFn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID);
using HeapReAllocFn = LPVOID(WINAPI*)(HANDLE, DWORD, LPVOID, SIZE_T);
using HeapSizeFn = SIZE_T(WINAPI*)(HANDLE, DWORD, LPCVOID);

HeapAllocFn g_heapAlloc;
HeapFreeFn g_heapFree;
HeapReAllocFn g_heapReAlloc;
HeapSizeFn g_heapSize;

FreeRecord* g_table;
volatile LONG g_tableLock;
CodeRange g_gameCode[8];    // code sections of the game's own modules, for finding game callers on the stack
int g_gameCodeCount;

// Statistics since the last report
volatile LONG g_frees;
volatile LONG g_moves;
volatile LONG64 g_bytesFreed;
DWORD g_lastReport;

void Lock()
{
    while (InterlockedCompareExchange(&g_tableLock, 1, 0) != 0)
        YieldProcessor();
}

void Unlock()
{
    InterlockedExchange(&g_tableLock, 0);
}

// For the fault report: if the fault happened inside the tracker while it held the lock, waiting would deadlock.
bool TryLock(DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    while (InterlockedCompareExchange(&g_tableLock, 1, 0) != 0)
    {
        if (GetTickCount() - start > timeoutMs)
            return false;
        YieldProcessor();
    }
    return true;
}

unsigned Hash(uintptr_t address)
{
    return static_cast<unsigned>(((address >> 3) * 2654435761u) >> (32 - kTableBits));
}

bool IsGameCode(uintptr_t address)
{
    for (int i = 0; i < g_gameCodeCount; ++i)
    {
        if (address >= g_gameCode[i].start && address < g_gameCode[i].end)
            return true;
    }
    return false;
}

bool FollowsCall(uintptr_t address)
{
    __try
    {
        const auto* code = reinterpret_cast<const unsigned char*>(address);
        return code[-5] == 0xE8 || (code[-6] == 0xFF && (code[-5] & 0x38) == 0x10) ||
               (code[-3] == 0xFF && (code[-2] & 0x38) == 0x10) || (code[-2] == 0xFF && (code[-1] & 0x38) == 0x10) ||
               (code[-7] == 0xFF && (code[-6] & 0x38) == 0x10);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The innermost return addresses into the game's own code, skipping the C runtime and Windows frames.
int GameCallers(const uint32_t* stack, uintptr_t* out, int max)
{
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto* end = static_cast<const uint32_t*>(tib->StackBase);
    int found = 0;
    for (int i = 0; i < kStackScanDwords && stack + i < end && found < max; ++i)
    {
        const uintptr_t value = stack[i];
        if (IsGameCode(value) && FollowsCall(value))
            out[found++] = value;
    }
    for (int i = found; i < max; ++i)
        out[i] = 0;
    return found;
}

void RecordFree(uintptr_t address, uint32_t size, const uint32_t* stack)
{
    uintptr_t callers[kCallers];
    GameCallers(stack, callers, kCallers);
    const DWORD now = GetTickCount();

    Lock();
    unsigned index = Hash(address);
    FreeRecord* slot = &g_table[index];
    for (unsigned probe = 0; probe < kMaxProbes; ++probe)
    {
        FreeRecord& candidate = g_table[(index + probe) & (kTableSize - 1)];
        if (candidate.address == address || candidate.address == 0)
        {
            slot = &candidate;
            break;
        }
    }
    slot->address = address;
    slot->size = size;
    slot->freedTick = now;
    memcpy(slot->freedBy, callers, sizeof callers);
    slot->reusedTick = 0;
    slot->reusedBy = 0;
    Unlock();

    InterlockedIncrement(&g_frees);
    InterlockedExchangeAdd64(&g_bytesFreed, size);
}

void RecordReuse(uintptr_t address, const uint32_t* stack)
{
    const unsigned index = Hash(address);
    Lock();
    for (unsigned probe = 0; probe < kMaxProbes; ++probe)
    {
        FreeRecord& candidate = g_table[(index + probe) & (kTableSize - 1)];
        if (candidate.address == 0)
            break;
        if (candidate.address == address)
        {
            uintptr_t caller;
            GameCallers(stack, &caller, 1);
            candidate.reusedTick = GetTickCount();
            candidate.reusedBy = caller;
            break;
        }
    }
    Unlock();
}

LPVOID WINAPI HookedHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    LPVOID block = g_heapAlloc(heap, flags, bytes);
    if (block)
        RecordReuse(reinterpret_cast<uintptr_t>(block), static_cast<const uint32_t*>(_AddressOfReturnAddress()));
    return block;
}

BOOL WINAPI HookedHeapFree(HANDLE heap, DWORD flags, LPVOID block)
{
    if (block)
    {
        const SIZE_T size = g_heapSize(heap, 0, block);
        RecordFree(reinterpret_cast<uintptr_t>(block), size == static_cast<SIZE_T>(-1) ? 0 : static_cast<uint32_t>(size),
                   static_cast<const uint32_t*>(_AddressOfReturnAddress()));
    }
    return g_heapFree(heap, flags, block);
}

LPVOID WINAPI HookedHeapReAlloc(HANDLE heap, DWORD flags, LPVOID block, SIZE_T bytes)
{
    const SIZE_T oldSize = block ? g_heapSize(heap, 0, block) : 0;
    LPVOID moved = g_heapReAlloc(heap, flags, block, bytes);
    if (moved && block && moved != block)
    {
        // The block moved: its old address is now free memory, exactly like a HeapFree.
        const auto* stack = static_cast<const uint32_t*>(_AddressOfReturnAddress());
        RecordFree(reinterpret_cast<uintptr_t>(block), oldSize == static_cast<SIZE_T>(-1) ? 0 : static_cast<uint32_t>(oldSize), stack);
        RecordReuse(reinterpret_cast<uintptr_t>(moved), stack);
        InterlockedIncrement(&g_moves);
    }
    return moved;
}

// Points every import of `function` in `module` at `replacement`. Returns the number of entries patched.
int PatchImports(HMODULE module, const char* function, void* replacement)
{
    auto* base = reinterpret_cast<BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return 0;

    int patched = 0;
    for (auto* import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); import->Name; ++import)
    {
        if (!import->OriginalFirstThunk)
            continue;  // no name table to match against
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(byName->Name), function) != 0)
                continue;
            DWORD protection;
            VirtualProtect(&addresses->u1.Function, sizeof addresses->u1.Function, PAGE_READWRITE, &protection);
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            VirtualProtect(&addresses->u1.Function, sizeof addresses->u1.Function, protection, &protection);
            ++patched;
        }
    }
    return patched;
}

void AddGameCode(HMODULE module)
{
    if (!module || g_gameCodeCount >= static_cast<int>(sizeof g_gameCode / sizeof g_gameCode[0]))
        return;
    auto* base = reinterpret_cast<BYTE*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) && g_gameCodeCount < 8)
        {
            g_gameCode[g_gameCodeCount].start = reinterpret_cast<uintptr_t>(base) + section->VirtualAddress;
            g_gameCode[g_gameCodeCount].end = g_gameCode[g_gameCodeCount].start + section->Misc.VirtualSize;
            ++g_gameCodeCount;
        }
    }
}
}  // namespace

void InstallHeapTracker()
{
    if (g_table)
        return;
    g_table = static_cast<FreeRecord*>(VirtualAlloc(nullptr, kTableSize * sizeof(FreeRecord), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_table)
    {
        Log("heap tracker: cannot allocate its table\n");
        return;
    }
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    g_heapAlloc = reinterpret_cast<HeapAllocFn>(GetProcAddress(kernel32, "HeapAlloc"));
    g_heapFree = reinterpret_cast<HeapFreeFn>(GetProcAddress(kernel32, "HeapFree"));
    g_heapReAlloc = reinterpret_cast<HeapReAllocFn>(GetProcAddress(kernel32, "HeapReAlloc"));
    g_heapSize = reinterpret_cast<HeapSizeFn>(GetProcAddress(kernel32, "HeapSize"));

    // Callers are attributed to the game's own code only; the C runtime and Windows are just the plumbing.
    static const char* const kGameModules[] = {"manager.exe", "gfxwin.dll", "mfc42.dll", "d2s2.dll", "utils.dll"};
    for (const char* name : kGameModules)
        AddGameCode(GetModuleHandleA(name));

    // Every heap operation of the game goes through these modules' imports (see docs/GAME-INTERNALS.md).
    static const char* const kPatchedModules[] = {"manager.exe", "gfxwin.dll", "msvcrt.dll", "mfc42.dll",
                                                  "d2s2.dll", "utils.dll", "dcd3d.dll"};
    int patched = 0;
    for (const char* name : kPatchedModules)
    {
        HMODULE module = GetModuleHandleA(name);
        if (!module)
            continue;
        patched += PatchImports(module, "HeapAlloc", reinterpret_cast<void*>(&HookedHeapAlloc));
        patched += PatchImports(module, "HeapFree", reinterpret_cast<void*>(&HookedHeapFree));
        patched += PatchImports(module, "HeapReAlloc", reinterpret_cast<void*>(&HookedHeapReAlloc));
    }
    g_lastReport = GetTickCount();
    Log("heap tracker: %d imports intercepted, %u-entry table\n", patched, kTableSize);
}

void HeapTrackerTick()
{
    if (!g_table)
        return;
    const DWORD now = GetTickCount();
    if (now - g_lastReport < kStatsIntervalMs)
        return;
    const double minutes = (now - g_lastReport) / 60000.0;
    g_lastReport = now;
    const LONG frees = InterlockedExchange(&g_frees, 0);
    const LONG moves = InterlockedExchange(&g_moves, 0);
    const LONG64 bytes = InterlockedExchange64(&g_bytesFreed, 0);
    char time[16];
    FormatTimestamp(time, sizeof time);
    Log("[%s] heap: %.0f frees/min (%.0f of them moved blocks), %.2f MB freed/min\n", time, frees / minutes,
        moves / minutes, bytes / minutes / (1024.0 * 1024.0));
}

void ReportFreedBlocks(const uintptr_t* addresses, const char* const* labels, int count)
{
    if (!g_table)
        return;
    const DWORD now = GetTickCount();
    Log("  freed blocks referenced at the fault:\n");
    int reported = 0;
    if (!TryLock(200))
    {
        Log("    (heap tracker busy, lookup skipped)\n");
        return;
    }
    for (int i = 0; i < count; ++i)
    {
        const uintptr_t value = addresses[i];
        if (value < 0x10000)
            continue;
        for (unsigned j = 0; j < kTableSize; ++j)
        {
            const FreeRecord& record = g_table[j];
            if (!record.address || value < record.address || value >= record.address + (record.size ? record.size : 1))
                continue;
            char by[4][96] = {};
            for (int k = 0; k < kCallers; ++k)
            {
                if (record.freedBy[k])
                    DescribeAddress(record.freedBy[k], by[k], sizeof by[k]);
            }
            Log("    %s=0x%08x is inside block 0x%08x (%u bytes) freed %.1f s before the fault by %s %s %s %s\n", labels[i],
                static_cast<unsigned>(value), static_cast<unsigned>(record.address), record.size,
                (now - record.freedTick) / 1000.0, by[0], by[1], by[2], by[3]);
            if (record.reusedTick)
            {
                char reused[96] = {};
                if (record.reusedBy)
                    DescribeAddress(record.reusedBy, reused, sizeof reused);
                Log("      ...and the address was handed out again %.1f s before the fault, to %s\n",
                    (now - record.reusedTick) / 1000.0, reused);
            }
            ++reported;
        }
    }
    Unlock();
    if (!reported)
        Log("    (none)\n");
}
