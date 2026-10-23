#include "memory_compat.h"

#include "backend_log.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace
{
constexpr uint32_t kDefaultQuarantineMB = 192;
constexpr uint32_t kRingCapacity = 1u << 20;   // blocks in quarantine at most
constexpr uint32_t kIndexBits = 21;            // index slots: twice the ring capacity keeps probing short
constexpr uint32_t kIndexSize = 1u << kIndexBits;

constexpr int kFreedByFrames = 4;               // call stack recorded per freed block, with [memory] diagnose=1
constexpr uint64_t kMaxModifiedLogged = 60;

struct QuarantinedBlock
{
    HANDLE heap;
    void* block;
    uint32_t size;
    uint32_t checksum;  // of the contents when freed; a different value on release means the game still writes to it
};

struct FreedBy
{
    void* frames[kFreedByFrames];
};

using HeapFreeFn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID);
using HeapReAllocFn = LPVOID(WINAPI*)(HANDLE, DWORD, LPVOID, SIZE_T);
using HeapAllocFn = LPVOID(WINAPI*)(HANDLE, DWORD, SIZE_T);
using HeapSizeFn = SIZE_T(WINAPI*)(HANDLE, DWORD, LPCVOID);

HeapFreeFn g_heapFree;
HeapReAllocFn g_heapReAlloc;
HeapAllocFn g_heapAlloc;
HeapSizeFn g_heapSize;

SRWLOCK g_lock = SRWLOCK_INIT;
bool g_initialized;
bool g_enabled;            // the quarantine
bool g_fill;               // [memory] fill_new: diagnostic fill of new memory
uint8_t g_fillByte;
bool g_resizeInPlace = true;  // [memory] resize_in_place = 0: growing always moves (diagnostic, with page heap)
uint64_t g_limitBytes;

// FIFO ring of quarantined blocks, plus an open-addressing index of their addresses for double-free detection.
QuarantinedBlock* g_ring;
FreedBy* g_freedBy;    // parallel to g_ring, only when diagnosing
uint32_t g_ringHead;   // oldest
uint32_t g_ringCount;
uintptr_t* g_index;
uint64_t g_bytes;

// Statistics
uint64_t g_frees;
uint64_t g_moves;
uint64_t g_shrinksKept;    // shrinks the heap would have moved, kept in place instead
uint64_t g_doubleFrees;
uint64_t g_released;
uint64_t g_modified;       // blocks written to while in quarantine, never released
uint64_t g_modifiedBytes;
volatile LONG g_allocs;    // counted only while filling new memory

HMODULE g_patched[16];
int g_patchedCount;

uint32_t Slot(uintptr_t address)
{
    return static_cast<uint32_t>(((address >> 3) * 2654435761u) >> (32 - kIndexBits));
}

bool IndexContains(uintptr_t address)
{
    for (uint32_t i = Slot(address);; i = (i + 1) & (kIndexSize - 1))
    {
        if (g_index[i] == 0)
            return false;
        if (g_index[i] == address)
            return true;
    }
}

void IndexInsert(uintptr_t address)
{
    uint32_t i = Slot(address);
    while (g_index[i] != 0)
        i = (i + 1) & (kIndexSize - 1);
    g_index[i] = address;
}

// Linear-probing deletion with backward shift, so lookups never need tombstones.
void IndexRemove(uintptr_t address)
{
    uint32_t i = Slot(address);
    while (g_index[i] != address)
    {
        if (g_index[i] == 0)
            return;
        i = (i + 1) & (kIndexSize - 1);
    }
    uint32_t hole = i;
    for (uint32_t j = (hole + 1) & (kIndexSize - 1); g_index[j] != 0; j = (j + 1) & (kIndexSize - 1))
    {
        const uint32_t home = Slot(g_index[j]);
        // Move the entry back into the hole if its home slot is not between the hole and its position.
        const bool between = hole <= j ? (hole < home && home <= j) : (hole < home || home <= j);
        if (!between)
        {
            g_index[hole] = g_index[j];
            hole = j;
        }
    }
    g_index[hole] = 0;
}

uint32_t Checksum(const void* block, uint32_t size)
{
    // FNV-1a over 32-bit words, plus the tail bytes.
    const auto* words = static_cast<const uint32_t*>(block);
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < size / 4; ++i)
        hash = (hash ^ words[i]) * 16777619u;
    const auto* tail = static_cast<const uint8_t*>(block) + (size & ~3u);
    for (uint32_t i = 0; i < (size & 3u); ++i)
        hash = (hash ^ tail[i]) * 16777619u;
    return hash;
}

void Describe(void* address, char* out, size_t size)
{
    HMODULE module = nullptr;
    if (address && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      static_cast<LPCSTR>(address), &module) && module)
    {
        char path[MAX_PATH];
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* slash = strrchr(path, '\\');
        _snprintf_s(out, size, _TRUNCATE, "%s+0x%x", slash ? slash + 1 : path,
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
    }
    else
    {
        _snprintf_s(out, size, _TRUNCATE, "%p", address);
    }
}

void LogModified(const QuarantinedBlock& entry, uint32_t ringIndex)
{
    // The first bytes of the block, as text where printable: the game's stale writes are often numbers and names.
    char preview[3 * 24 + 1];
    char text[24 + 1];
    const auto* bytes = static_cast<const uint8_t*>(entry.block);
    const uint32_t count = std::min<uint32_t>(entry.size, 24);
    for (uint32_t i = 0; i < count; ++i)
    {
        _snprintf_s(preview + 3 * i, sizeof preview - 3 * i, _TRUNCATE, "%02x ", bytes[i]);
        text[i] = bytes[i] >= 0x20 && bytes[i] < 0x7f ? static_cast<char>(bytes[i]) : '.';
    }
    preview[3 * count] = 0;
    text[count] = 0;
    char freedBy[4 * 48] = "";
    if (g_freedBy)
    {
        size_t used = 0;
        for (void* frame : g_freedBy[ringIndex].frames)
        {
            if (!frame)
                break;
            char where[48];
            Describe(frame, where, sizeof where);
            used += _snprintf_s(freedBy + used, sizeof freedBy - used, _TRUNCATE, " %s", where);
        }
    }
    BackendLog("memory: freed block of %u bytes at %p was written to afterwards; kept. Starts: %s|%s|%s%s\n", entry.size,
               entry.block, preview, text, g_freedBy ? " freed by:" : "", freedBy);
}

void ReleaseOldest()
{
    QuarantinedBlock& oldest = g_ring[g_ringHead];
    IndexRemove(reinterpret_cast<uintptr_t>(oldest.block));
    if (Checksum(oldest.block, oldest.size) != oldest.checksum)
    {
        // The game still writes into this block through a stale pointer. Giving it back to Windows would let those
        // writes corrupt whatever reuses it (or the heap's own bookkeeping), so it is never released.
        if (g_modified++ < kMaxModifiedLogged)
            LogModified(oldest, g_ringHead);
        g_modifiedBytes += oldest.size;
    }
    else
    {
        g_heapFree(oldest.heap, 0, oldest.block);
        ++g_released;
    }
    g_bytes -= oldest.size;
    g_ringHead = (g_ringHead + 1) & (kRingCapacity - 1);
    --g_ringCount;
}

// Puts a block in quarantine. Caller holds the lock. Returns false if it already was there (a double free).
bool Quarantine(HANDLE heap, void* block, uint32_t size, const FreedBy* freedBy)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);
    if (IndexContains(address))
        return false;
    while (g_ringCount > 0 && (g_ringCount >= kRingCapacity - 1 || g_bytes + size > g_limitBytes))
        ReleaseOldest();
    const uint32_t index = (g_ringHead + g_ringCount) & (kRingCapacity - 1);
    QuarantinedBlock& slot = g_ring[index];
    slot.heap = heap;
    slot.block = block;
    slot.size = size;
    slot.checksum = Checksum(block, size);
    if (g_freedBy)
        g_freedBy[index] = *freedBy;
    ++g_ringCount;
    IndexInsert(address);
    g_bytes += size;
    return true;
}

// Who freed a block: the first frames above the C runtime and MFC, which only pass frees through.
void CaptureFreedBy(FreedBy& out)
{
    memset(&out, 0, sizeof out);
    if (!g_freedBy)
        return;
    void* frames[16];
    const USHORT captured = RtlCaptureStackBackTrace(1, 16, frames, nullptr);
    static HMODULE runtime[3];
    if (!runtime[0])
    {
        runtime[0] = GetModuleHandleA("msvcrt.dll");
        runtime[1] = GetModuleHandleA("mfc42.dll");
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&CaptureFreedBy), &runtime[2]);
    }
    int kept = 0;
    for (USHORT i = 0; i < captured && kept < kFreedByFrames; ++i)
    {
        HMODULE module = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(frames[i]), &module);
        if (kept == 0 && std::find(std::begin(runtime), std::end(runtime), module) != std::end(runtime))
            continue;
        out.frames[kept++] = frames[i];
    }
}

BOOL WINAPI CompatHeapFree(HANDLE heap, DWORD flags, LPVOID block)
{
    if (!block || !g_enabled)
        return g_heapFree(heap, flags, block);
    const SIZE_T size = g_heapSize(heap, 0, block);
    if (size == static_cast<SIZE_T>(-1))
        return g_heapFree(heap, flags, block);  // not a block of this heap: let Windows report it as before

    // The game may look at GetLastError after freeing; nothing done here must change it.
    const DWORD lastError = GetLastError();
    FreedBy freedBy;
    CaptureFreedBy(freedBy);
    AcquireSRWLockExclusive(&g_lock);
    ++g_frees;
    if (!Quarantine(heap, block, static_cast<uint32_t>(size), &freedBy))
    {
        if (g_doubleFrees++ == 0)
            BackendLog("memory: the game freed a block twice (%p); ignoring repeated frees from now on\n", block);
    }
    ReleaseSRWLockExclusive(&g_lock);
    SetLastError(lastError);
    return TRUE;
}

// Diagnostic (fill_new): new memory gets a recognizable byte instead of whatever the heap had there, so the game's
// reads of memory it never initialized show up as strange values. HEAP_ZERO_MEMORY requests are left alone.
void FillNew(void* block, SIZE_T from, SIZE_T to, DWORD flags)
{
    if (g_fill && block && to > from && !(flags & HEAP_ZERO_MEMORY))
        memset(static_cast<uint8_t*>(block) + from, g_fillByte, to - from);
}

LPVOID WINAPI CompatHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    LPVOID block = g_heapAlloc(heap, flags, bytes);
    if (block)
    {
        FillNew(block, 0, bytes, flags);
        InterlockedIncrement(&g_allocs);
    }
    return block;
}

LPVOID WINAPI CompatHeapReAlloc(HANDLE heap, DWORD flags, LPVOID block, SIZE_T bytes)
{
    if (!block)
        return g_heapReAlloc(heap, flags, block, bytes);
    const SIZE_T oldSize = g_heapSize(heap, 0, block);
    if (oldSize == static_cast<SIZE_T>(-1))
        return g_heapReAlloc(heap, flags, block, bytes);
    if (!g_enabled || (flags & HEAP_REALLOC_IN_PLACE_ONLY))
    {
        LPVOID resized = g_heapReAlloc(heap, flags, block, bytes);
        FillNew(resized, oldSize, bytes, flags);
        return resized;
    }

    const DWORD lastError = GetLastError();
    // Resize in place when the heap can, as it always did: some of the game's code relies on blocks staying put.
    // (Without HEAP_GENERATE_EXCEPTIONS: not fitting in place is expected, not an error.)
    LPVOID same = nullptr;
    if (g_resizeInPlace)
        same = g_heapReAlloc(heap, (flags & ~HEAP_GENERATE_EXCEPTIONS) | HEAP_REALLOC_IN_PLACE_ONLY, block, bytes);
    if (same)
    {
        FillNew(same, oldSize, bytes, flags);
        SetLastError(lastError);
        return same;
    }
    // Shrinking never moves a block. The game keeps pointers into the middle of buffers it shrinks: for example it
    // reads a whole team record into one buffer, points the team's name into it, then shrinks the buffer to the size
    // actually used (docs/GAME-INTERNALS.md, "Memory bugs"). The heap of Windows XP always shrank in place; today's
    // heap moves blocks that change size class, which leaves those pointers on freed memory. Keeping the block as it
    // is wastes at most the unused tail.
    if (bytes <= oldSize)
    {
        AcquireSRWLockExclusive(&g_lock);
        ++g_shrinksKept;
        ReleaseSRWLockExclusive(&g_lock);
        SetLastError(lastError);
        return block;
    }

    // The block has to move: whoever still holds the old address keeps reading valid (old) data instead of a reused block.
    LPVOID moved = g_heapAlloc(heap, flags & (HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE), bytes);
    if (!moved)
    {
        BackendLog("memory: allocation of %lu bytes failed while resizing a block\n", static_cast<unsigned long>(bytes));
        return nullptr;  // like HeapReAlloc: on failure the original block stays valid
    }
    memcpy(moved, block, std::min<SIZE_T>(oldSize, bytes));
    FillNew(moved, oldSize, bytes, flags);

    FreedBy freedBy;
    CaptureFreedBy(freedBy);
    AcquireSRWLockExclusive(&g_lock);
    ++g_moves;
    // Already quarantined means resizing a freed block: just copy.
    Quarantine(heap, block, static_cast<uint32_t>(oldSize), &freedBy);
    ReleaseSRWLockExclusive(&g_lock);
    SetLastError(lastError);  // the failed in-place attempt set it; a real HeapReAlloc that moves does not
    return moved;
}

int PatchImports(HMODULE module, const char* function, void* replacement)
{
    auto* base = reinterpret_cast<BYTE*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return 0;
    int patched = 0;
    for (auto* import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); import->Name; ++import)
    {
        if (!import->OriginalFirstThunk)
            continue;
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

bool Initialize()
{
    char ini[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&Initialize), &self);
    GetModuleFileNameA(self, ini, MAX_PATH);
    char* slash = strrchr(ini, '\\');
    strcpy_s(slash ? slash + 1 : ini, MAX_PATH - (slash ? slash + 1 - ini : 0), "dcogl.ini");
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    g_heapFree = reinterpret_cast<HeapFreeFn>(GetProcAddress(kernel32, "HeapFree"));
    g_heapReAlloc = reinterpret_cast<HeapReAllocFn>(GetProcAddress(kernel32, "HeapReAlloc"));
    g_heapAlloc = reinterpret_cast<HeapAllocFn>(GetProcAddress(kernel32, "HeapAlloc"));
    g_heapSize = reinterpret_cast<HeapSizeFn>(GetProcAddress(kernel32, "HeapSize"));

    // [memory] fill_new = <hex byte> fills new memory with that byte (diagnostic; absent or empty = off). Works with
    // the quarantine on or off.
    char fill[16] = "";
    GetPrivateProfileStringA("memory", "fill_new", "", fill, sizeof fill, ini);
    if (fill[0])
    {
        g_fill = true;
        g_fillByte = static_cast<uint8_t>(strtoul(fill, nullptr, 16));
        BackendLog("memory: diagnostic fill of new memory with 0x%02x\n", g_fillByte);
    }

    // [memory] resize_in_place = 0 never resizes a block in place: growing always moves it (shrinking still keeps
    // it). Diagnostic, for Windows' page heap: its in-place resizes of "normal" blocks can disagree with its own
    // bookkeeping and stop the game for overruns that aren't there, and they leave no guard page after the block.
    if (!GetPrivateProfileIntA("memory", "resize_in_place", 1, ini))
    {
        g_resizeInPlace = false;
        BackendLog("memory: blocks are never resized in place (diagnostic)\n");
    }

    // [memory] quarantine_mb = 0 turns the layer off (to compare with the original behavior).
    const UINT megabytes = GetPrivateProfileIntA("memory", "quarantine_mb", kDefaultQuarantineMB, ini);
    g_limitBytes = static_cast<uint64_t>(megabytes) * 1024 * 1024;
    g_enabled = megabytes > 0;
    if (!g_enabled)
    {
        BackendLog("memory: compatibility layer disabled in dcogl.ini\n");
        return true;
    }

    g_ring = static_cast<QuarantinedBlock*>(VirtualAlloc(nullptr, kRingCapacity * sizeof(QuarantinedBlock), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_index = static_cast<uintptr_t*>(VirtualAlloc(nullptr, kIndexSize * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_ring || !g_index)
    {
        BackendLog("memory: cannot allocate the quarantine bookkeeping; layer disabled\n");
        g_enabled = false;
        return true;
    }
    // [memory] diagnose = 1 records who freed each block, to find the game code behind the stale writes.
    if (GetPrivateProfileIntA("memory", "diagnose", 0, ini))
    {
        g_freedBy = static_cast<FreedBy*>(VirtualAlloc(nullptr, kRingCapacity * sizeof(FreedBy), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        BackendLog("memory: diagnostics %s\n", g_freedBy ? "on" : "unavailable (out of memory)");
    }
    BackendLog("memory: compatibility layer on, quarantine up to %u MB\n", megabytes);
    return true;
}
}  // namespace

void InstallMemoryCompat()
{
    AcquireSRWLockExclusive(&g_lock);
    if (!g_initialized)
        g_initialized = Initialize();
    if (g_enabled || g_fill)
    {
        // The C runtime (msvcrt.dll) carries every malloc/free/new/delete of the game; the others call the heap directly.
        static const char* const kModules[] = {"msvcrt.dll", "gfxwin.dll", "d2s2.dll", "dcd3d.dll", "manager.exe", "utils.dll", "mfc42.dll"};
        for (const char* name : kModules)
        {
            HMODULE module = GetModuleHandleA(name);
            if (!module || std::find(g_patched, g_patched + g_patchedCount, module) != g_patched + g_patchedCount)
                continue;
            int patched = PatchImports(module, "HeapFree", reinterpret_cast<void*>(&CompatHeapFree)) +
                          PatchImports(module, "HeapReAlloc", reinterpret_cast<void*>(&CompatHeapReAlloc));
            if (g_fill)
                patched += PatchImports(module, "HeapAlloc", reinterpret_cast<void*>(&CompatHeapAlloc));
            if (g_patchedCount < static_cast<int>(sizeof g_patched / sizeof g_patched[0]))
                g_patched[g_patchedCount++] = module;
            if (patched)
                BackendLog("memory: %s covered (%d imports)\n", name, patched);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void LogMemoryCompatStats()
{
    if (g_fill)
        BackendLog("memory: %ld new blocks filled with 0x%02x\n", g_allocs, g_fillByte);
    if (!g_enabled)
        return;
    AcquireSRWLockShared(&g_lock);
    BackendLog("memory: %llu frees, %llu moved resizes, %llu shrinks kept in place, %llu double frees ignored; quarantine %u "
               "blocks, %.1f MB; %llu released, %llu kept after stale writes (%.1f MB)\n",
               g_frees, g_moves, g_shrinksKept, g_doubleFrees, g_ringCount, g_bytes / (1024.0 * 1024.0), g_released,
               g_modified, g_modifiedBytes / (1024.0 * 1024.0));
    ReleaseSRWLockShared(&g_lock);
}
