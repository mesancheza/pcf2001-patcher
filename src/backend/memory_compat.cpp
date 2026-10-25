#include "memory_compat.h"

#include "backend_log.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <iterator>

namespace
{
constexpr uint32_t kDefaultQuarantineMB = 0;  // off: [memory] quarantine_mb turns it on, for testing (DESIGN.md D13)
// Blocks in quarantine at most. The game's freed blocks average about 750 bytes (a logged session: 97.5 MB in 134,000
// blocks), so the default limit holds some 260,000; past the capacity the oldest go first, as when the limit is reached.
// The bookkeeping (16 bytes per entry, plus the index) is committed up front and takes address space for good.
constexpr uint32_t kRingCapacity = 1u << 19;
constexpr uint32_t kIndexBits = 20;            // index slots: twice the ring capacity keeps probing short

// Memory pressure: when an allocation fails, the quarantine gives its memory back (and the allocation is retried); when
// the address space itself runs short (the game is a 32-bit program, 2 GB at most), it also halves its limit, down to
// a floor. Protecting the game's stale reads matters less than the game running out of memory. Large blocks (from about
// 512 KB) are separate allocations that go back to Windows on release, which also helps the contiguous space large
// surfaces need.
constexpr uint64_t kPressureLargestFree = 64ull << 20;
constexpr uint64_t kPressureFree = 256ull << 20;
constexpr uint64_t kMinimumLimitBytes = 16ull << 20;

constexpr int kFreedByFrames = 4;               // call stack recorded per freed block, with [memory] diagnose=1
constexpr int kStackScanDwords = 512;           // how far up the stack the game's callers are looked for (2 KB)
constexpr uint64_t kMaxModifiedLogged = 60;

// [memory] allocation_sites = 1 (diagnostic): which code allocated the memory the game holds, to find what grows
// during a long session. Every block the game holds is recorded with its allocation site, its first callers above the
// C runtime; every few minutes the log lists the sites whose memory grew the most.
constexpr int kSiteFrames = 3;
constexpr uint32_t kSiteCapacity = 1u << 14;    // distinct sites; one more entry collects the rest if they run out
constexpr uint32_t kTrackedBits = 20;           // blocks recorded: up to three quarters of 2^20
constexpr uint32_t kSitesListed = 12;
constexpr uint32_t kReportsPerSiteList = 10;    // memory reports, 30 seconds apart: a list every 5 minutes

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

struct AllocationSite
{
    void* frames[kSiteFrames];
    uint32_t allocations;   // 0: an unused entry
    int32_t blocks;         // held by the game now
    int64_t bytes;
    int32_t listedBlocks;   // at the previous list
    int64_t listedBytes;
};

// A table of block addresses, with a 16-bit value each when values is set. Open addressing with linear probing;
// deletion by backward shift, so lookups never need tombstones. Address 0 marks a free slot.
struct AddressTable
{
    uintptr_t* keys;
    uint16_t* values;
    uint32_t bits;

    uint32_t Size() const { return 1u << bits; }

    uint32_t Home(uintptr_t address) const
    {
        return static_cast<uint32_t>(((address >> 3) * 2654435761u) >> (32 - bits));
    }

    // The slot holding the address, or Size() if it isn't in the table.
    uint32_t Find(uintptr_t address) const
    {
        for (uint32_t i = Home(address);; i = (i + 1) & (Size() - 1))
        {
            if (keys[i] == 0)
                return Size();
            if (keys[i] == address)
                return i;
        }
    }

    void Insert(uintptr_t address, uint16_t value)
    {
        uint32_t i = Home(address);
        while (keys[i] != 0)
            i = (i + 1) & (Size() - 1);
        keys[i] = address;
        if (values)
            values[i] = value;
    }

    void RemoveAt(uint32_t hole)
    {
        for (uint32_t j = (hole + 1) & (Size() - 1); keys[j] != 0; j = (j + 1) & (Size() - 1))
        {
            const uint32_t home = Home(keys[j]);
            // Move the entry back into the hole if its home slot is not between the hole and its position.
            const bool between = hole <= j ? (hole < home && home <= j) : (hole < home || home <= j);
            if (!between)
            {
                keys[hole] = keys[j];
                if (values)
                    values[hole] = values[j];
                hole = j;
            }
        }
        keys[hole] = 0;
    }
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
bool g_enabled;            // the layer: its hooks, the shrink fix, the counts
bool g_quarantine;         // freed blocks go into the quarantine (off with [memory] quarantine_mb = 0)
bool g_fill;               // [memory] fill_new: diagnostic fill of new memory
uint8_t g_fillByte;
bool g_resizeInPlace = true;  // [memory] resize_in_place = 0: growing always moves (diagnostic, with page heap)
uint64_t g_limitBytes;

// FIFO ring of quarantined blocks, plus an index of their addresses for double-free detection.
QuarantinedBlock* g_ring;
FreedBy* g_freedBy;    // parallel to g_ring, only when diagnosing
uint32_t g_ringHead;   // oldest
uint32_t g_ringCount;
AddressTable g_index;
uint64_t g_bytes;

// With [memory] allocation_sites: the sites (kSiteCapacity entries by hash of their frames, plus one for the rest),
// and every block the game holds with its site.
AllocationSite* g_sites;
uint32_t g_siteCount;
AddressTable g_tracked;
uint32_t g_trackedCount;
uint64_t g_untracked;  // blocks not recorded because the table was full

// Statistics
uint64_t g_frees;
uint64_t g_moves;
uint64_t g_shrinksKept;    // shrinks the heap would have moved, kept in place instead
uint64_t g_doubleFrees;
uint64_t g_released;
uint64_t g_modified;       // blocks written to while in quarantine, never released
uint64_t g_modifiedBytes;
uint64_t g_pressureReleases;  // times memory pressure emptied the quarantine
// What the game holds: blocks allocated minus blocks freed, and their bytes, counted from when the layer starts (a
// block allocated before and freed after counts as a free).
volatile LONG g_allocs;
volatile LONG g_heldBlocks;
volatile LONG g_heldBytes;

HMODULE g_patched[16];
int g_patchedCount;

// The executable sections of the game's own modules (not the C runtime or MFC), for finding its calls on the stack.
struct CodeRange
{
    uintptr_t start, end;
};
CodeRange g_gameCode[16];
volatile LONG g_gameCodeCount;

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
    const uint32_t slot = g_index.Find(reinterpret_cast<uintptr_t>(oldest.block));
    if (slot != g_index.Size())
        g_index.RemoveAt(slot);
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
    if (g_index.Find(address) != g_index.Size())
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
    g_index.Insert(address, 0);
    g_bytes += size;
    return true;
}

// Empties the quarantine, and with lowerLimit halves its limit too (down to the floor). Caller holds the lock. Blocks
// the game still writes to stay kept, as always. Returns whether anything was released.
bool ReleaseUnderPressure(const char* why, bool lowerLimit)
{
    constexpr double kMB = 1024.0 * 1024.0;
    if (g_ringCount == 0)
        return false;
    const uint64_t before = g_bytes;
    while (g_ringCount > 0)
        ReleaseOldest();
    if (lowerLimit)
        g_limitBytes = std::max(g_limitBytes / 2, kMinimumLimitBytes);
    if (g_pressureReleases++ < kMaxModifiedLogged)
        BackendLog("memory: %s; quarantine emptied (%.1f MB given back)%s%.0f MB\n", why, before / kMB,
                   lowerLimit ? ", its limit is now " : ", its limit stays at ", g_limitBytes / kMB);
    return true;
}

void AddGameCode(HMODULE module)
{
    auto* base = reinterpret_cast<BYTE*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        const LONG count = g_gameCodeCount;
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || count >= static_cast<LONG>(sizeof g_gameCode / sizeof g_gameCode[0]))
            continue;
        const uintptr_t start = reinterpret_cast<uintptr_t>(base) + section->VirtualAddress;
        g_gameCode[count] = CodeRange{start, start + section->Misc.VirtualSize};
        InterlockedExchange(&g_gameCodeCount, count + 1);  // published after the entry: the hooks read it unlocked
    }
}

bool IsGameCode(uintptr_t address)
{
    const LONG count = g_gameCodeCount;
    for (LONG i = 0; i < count; ++i)
    {
        if (address >= g_gameCode[i].start && address < g_gameCode[i].end)
            return true;
    }
    return false;
}

// Whether the instruction just before the address is a call: then it's a return address, not some stale value.
bool FollowsCall(uintptr_t address)
{
    __try
    {
        const auto* code = reinterpret_cast<const unsigned char*>(address);
        return code[-5] == 0xE8 ||                                   // call rel32
               (code[-6] == 0xFF && (code[-5] & 0x38) == 0x10) ||    // call [disp32], call [reg+disp32]
               (code[-3] == 0xFF && (code[-2] & 0x38) == 0x10) ||    // call [reg+disp8]
               (code[-2] == 0xFF && (code[-1] & 0x38) == 0x10) ||    // call reg, call [reg]
               (code[-7] == 0xFF && (code[-6] & 0x38) == 0x10);      // call [reg+reg*scale+disp32]
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Whether the call just before the return address `ret` can have led to `inner`, the frame found below it: a direct
// call, or a call through an import, must go to a function that starts a little before `inner`. Calls through
// registers or objects (virtual methods) can't be checked and pass.
bool CallCanReach(uintptr_t ret, uintptr_t inner)
{
    constexpr uintptr_t kLargestFunction = 64 * 1024;
    __try
    {
        const auto* code = reinterpret_cast<const unsigned char*>(ret);
        uintptr_t target;
        if (code[-5] == 0xE8)
            target = ret + *reinterpret_cast<const int32_t*>(code - 4);
        else if (code[-6] == 0xFF && code[-5] == 0x15)
            target = **reinterpret_cast<const uintptr_t* const*>(code - 4);
        else
            return true;
        return target <= inner && inner - target < kLargestFunction;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The innermost return addresses into the game's own code, from a scan of the stack starting at a heap function's
// return address. The game's code doesn't keep reliable frame pointers, so frames can't be followed. The first match is
// the call that reached the heap, directly or through the C runtime. Each later one must be a call that can lead to the
// one before: frames can be far apart (gfxwin's MemAllocFun keeps a 516-byte buffer on the stack), and the space in
// between can hold stale return addresses from earlier calls.
void CaptureCallers(const uint32_t* stack, void** out, int count)
{
    const auto* end = static_cast<const uint32_t*>(reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
    int found = 0;
    for (int i = 0; i < kStackScanDwords && stack + i < end && found < count; ++i)
    {
        const uintptr_t value = stack[i];
        if (!IsGameCode(value) || !FollowsCall(value))
            continue;
        if (found > 0)
        {
            const auto inner = reinterpret_cast<uintptr_t>(out[found - 1]);
            if (value == inner || !CallCanReach(value, inner))
                continue;
        }
        out[found++] = reinterpret_cast<void*>(value);
    }
    for (int i = found; i < count; ++i)
        out[i] = nullptr;
}

// Who freed a block, when diagnosing.
void CaptureFreedBy(FreedBy& out, const uint32_t* stack)
{
    memset(&out, 0, sizeof out);
    if (g_freedBy)
        CaptureCallers(stack, out.frames, kFreedByFrames);
}

// --- Allocation sites ([memory] allocation_sites). The caller holds the lock. ---

uint16_t FindSite(void* const* frames)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < kSiteFrames; ++i)
        hash = (hash ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frames[i]))) * 16777619u;
    for (uint32_t i = hash & (kSiteCapacity - 1);; i = (i + 1) & (kSiteCapacity - 1))
    {
        AllocationSite& site = g_sites[i];
        if (site.allocations == 0)
        {
            if (g_siteCount >= kSiteCapacity / 4 * 3)
                return static_cast<uint16_t>(kSiteCapacity);  // full: the entry for the rest
            memcpy(site.frames, frames, sizeof site.frames);
            ++g_siteCount;
            return static_cast<uint16_t>(i);
        }
        if (memcmp(site.frames, frames, sizeof site.frames) == 0)
            return static_cast<uint16_t>(i);
    }
}

// The game freed a block (or the heap is handing out an address the table still has).
void ForgetBlock(void* block, SIZE_T bytes)
{
    const uint32_t slot = g_tracked.Find(reinterpret_cast<uintptr_t>(block));
    if (slot == g_tracked.Size())
        return;  // allocated before the layer started, or not recorded
    AllocationSite& site = g_sites[g_tracked.values[slot]];
    --site.blocks;
    site.bytes -= bytes;
    g_tracked.RemoveAt(slot);
    --g_trackedCount;
}

void RecordBlock(void* block, SIZE_T bytes, void* const* frames)
{
    ForgetBlock(block, 0);  // a stale entry would otherwise shadow this one
    if (g_trackedCount >= g_tracked.Size() / 4 * 3)
    {
        ++g_untracked;
        return;
    }
    const uint16_t index = FindSite(frames);
    AllocationSite& site = g_sites[index];
    ++site.allocations;
    ++site.blocks;
    site.bytes += bytes;
    g_tracked.Insert(reinterpret_cast<uintptr_t>(block), index);
    ++g_trackedCount;
}

// A block changed size, in place or by moving to resized. It stays with the site that allocated it.
void RecordResize(void* block, void* resized, SIZE_T oldSize, SIZE_T bytes)
{
    const uint32_t slot = g_tracked.Find(reinterpret_cast<uintptr_t>(block));
    if (slot == g_tracked.Size())
        return;
    const uint16_t index = g_tracked.values[slot];
    g_sites[index].bytes += static_cast<int64_t>(bytes) - static_cast<int64_t>(oldSize);
    if (resized == block)
        return;
    g_tracked.RemoveAt(slot);
    --g_trackedCount;
    ForgetBlock(resized, 0);
    g_tracked.Insert(reinterpret_cast<uintptr_t>(resized), index);
    ++g_trackedCount;
}

void LogSite(const AllocationSite& site)
{
    constexpr double kMB = 1024.0 * 1024.0;
    char where[kSiteFrames * 48] = "";
    size_t used = 0;
    for (void* frame : site.frames)
    {
        if (!frame)
            break;
        char one[48];
        Describe(frame, one, sizeof one);
        used += _snprintf_s(where + used, sizeof where - used, _TRUNCATE, "%s%s", used ? " < " : "", one);
    }
    BackendLog("memory:   %+.2f MB, %+ld blocks; now %.2f MB in %ld blocks, %lu allocated in all; at %s\n",
               (site.bytes - site.listedBytes) / kMB, static_cast<long>(site.blocks - site.listedBlocks), site.bytes / kMB,
               static_cast<long>(site.blocks), static_cast<unsigned long>(site.allocations), used ? where : "(other sites)");
}

// The sites whose memory grew the most since the previous list and, with holders, the sites holding the most.
void LogAllocationSites(bool holders)
{
    static uint16_t order[kSiteCapacity + 1];
    uint32_t count = 0;
    for (uint32_t i = 0; i <= kSiteCapacity; ++i)
        if (g_sites[i].allocations)
            order[count++] = static_cast<uint16_t>(i);
    const auto growth = [](uint16_t i) { return g_sites[i].bytes - g_sites[i].listedBytes; };
    std::sort(order, order + count, [&](uint16_t a, uint16_t b) { return growth(a) > growth(b); });
    BackendLog("memory: %u allocation sites, %u blocks recorded (%llu not recorded: table full); grown the most:\n", count,
               g_trackedCount, g_untracked);
    for (uint32_t k = 0; k < count && k < kSitesListed && growth(order[k]) > 0; ++k)
        LogSite(g_sites[order[k]]);
    if (holders)
    {
        std::sort(order, order + count, [](uint16_t a, uint16_t b) { return g_sites[a].bytes > g_sites[b].bytes; });
        BackendLog("memory: allocation sites holding the most:\n");
        for (uint32_t k = 0; k < count && k < kSitesListed; ++k)
            LogSite(g_sites[order[k]]);
    }
    for (uint32_t k = 0; k < count; ++k)
    {
        g_sites[order[k]].listedBlocks = g_sites[order[k]].blocks;
        g_sites[order[k]].listedBytes = g_sites[order[k]].bytes;
    }
}

// What the game holds, after it resized a block (in place, or moved to resized). Lock not held.
void NoteResize(void* block, void* resized, SIZE_T oldSize, SIZE_T bytes)
{
    InterlockedExchangeAdd(&g_heldBytes, static_cast<LONG>(bytes) - static_cast<LONG>(oldSize));
    if (!g_sites)
        return;
    AcquireSRWLockExclusive(&g_lock);
    RecordResize(block, resized, oldSize, bytes);
    ReleaseSRWLockExclusive(&g_lock);
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
    CaptureFreedBy(freedBy, static_cast<const uint32_t*>(_AddressOfReturnAddress()));
    AcquireSRWLockExclusive(&g_lock);
    ++g_frees;
    if (!g_quarantine)
    {
        InterlockedDecrement(&g_heldBlocks);
        InterlockedExchangeAdd(&g_heldBytes, -static_cast<LONG>(size));
        if (g_sites)
            ForgetBlock(block, size);
        ReleaseSRWLockExclusive(&g_lock);
        SetLastError(lastError);
        return g_heapFree(heap, flags, block);
    }
    if (Quarantine(heap, block, static_cast<uint32_t>(size), &freedBy))
    {
        InterlockedDecrement(&g_heldBlocks);
        InterlockedExchangeAdd(&g_heldBytes, -static_cast<LONG>(size));
        if (g_sites)
            ForgetBlock(block, size);
    }
    else if (g_doubleFrees++ == 0)
    {
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
    // An allocation that fails gets one more try after the quarantine gives its memory back. The first try never raises
    // an exception; the second does if the caller asked for one, as HeapAlloc would have.
    LPVOID block = g_heapAlloc(heap, flags & ~HEAP_GENERATE_EXCEPTIONS, bytes);
    if (!block && g_enabled && RelieveMemoryPressure("an allocation failed"))
        block = g_heapAlloc(heap, flags, bytes);
    else if (!block && (flags & HEAP_GENERATE_EXCEPTIONS))
        block = g_heapAlloc(heap, flags, bytes);
    if (block)
    {
        FillNew(block, 0, bytes, flags);
        InterlockedIncrement(&g_allocs);
        InterlockedIncrement(&g_heldBlocks);
        InterlockedExchangeAdd(&g_heldBytes, static_cast<LONG>(bytes));
        if (g_sites)
        {
            void* frames[kSiteFrames];
            CaptureCallers(static_cast<const uint32_t*>(_AddressOfReturnAddress()), frames, kSiteFrames);
            AcquireSRWLockExclusive(&g_lock);
            RecordBlock(block, bytes, frames);
            ReleaseSRWLockExclusive(&g_lock);
        }
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
        if (resized)
            NoteResize(block, resized, oldSize, bytes);
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
        NoteResize(block, same, oldSize, bytes);
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
    const DWORD moveFlags = flags & (HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE);
    LPVOID moved = g_heapAlloc(heap, moveFlags & ~HEAP_GENERATE_EXCEPTIONS, bytes);
    if (!moved && RelieveMemoryPressure("a block could not grow"))
        moved = g_heapAlloc(heap, moveFlags & ~HEAP_GENERATE_EXCEPTIONS, bytes);
    if (!moved && (moveFlags & HEAP_GENERATE_EXCEPTIONS))
        moved = g_heapAlloc(heap, moveFlags, bytes);  // raises, as HeapReAlloc would have
    if (!moved)
    {
        BackendLog("memory: allocation of %lu bytes failed while resizing a block\n", static_cast<unsigned long>(bytes));
        return nullptr;  // like HeapReAlloc: on failure the original block stays valid
    }
    memcpy(moved, block, std::min<SIZE_T>(oldSize, bytes));
    FillNew(moved, oldSize, bytes, flags);

    FreedBy freedBy;
    CaptureFreedBy(freedBy, static_cast<const uint32_t*>(_AddressOfReturnAddress()));
    AcquireSRWLockExclusive(&g_lock);
    ++g_moves;
    // Already quarantined means resizing a freed block: just copy. The copy is then a new block the game holds.
    // Without the quarantine the old block goes straight back to Windows (below).
    if (!g_quarantine || Quarantine(heap, block, static_cast<uint32_t>(oldSize), &freedBy))
    {
        InterlockedExchangeAdd(&g_heldBytes, static_cast<LONG>(bytes) - static_cast<LONG>(oldSize));
        if (g_sites)
            RecordResize(block, moved, oldSize, bytes);
    }
    else
    {
        InterlockedIncrement(&g_heldBlocks);
        InterlockedExchangeAdd(&g_heldBytes, static_cast<LONG>(bytes));
        if (g_sites)
        {
            void* const unknown[kSiteFrames] = {};
            RecordBlock(moved, bytes, unknown);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!g_quarantine)
        g_heapFree(heap, 0, block);
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

    // [memory] layer = 0 turns the whole layer off (to compare with the original behavior).
    if (!GetPrivateProfileIntA("memory", "layer", 1, ini))
    {
        BackendLog("memory: compatibility layer disabled in dcogl.ini\n");
        return true;
    }
    g_enabled = true;

    // [memory] quarantine_mb = 0 turns off only the quarantine: freed blocks go straight back to Windows, and shrunk
    // blocks still stay in place.
    const UINT megabytes = GetPrivateProfileIntA("memory", "quarantine_mb", kDefaultQuarantineMB, ini);
    g_limitBytes = static_cast<uint64_t>(megabytes) * 1024 * 1024;
    if (megabytes > 0)
    {
        g_ring = static_cast<QuarantinedBlock*>(VirtualAlloc(nullptr, kRingCapacity * sizeof(QuarantinedBlock), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        g_index.bits = kIndexBits;
        g_index.keys = static_cast<uintptr_t*>(VirtualAlloc(nullptr, g_index.Size() * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        g_quarantine = g_ring && g_index.keys;
        if (!g_quarantine)
            BackendLog("memory: cannot allocate the quarantine bookkeeping; quarantine off\n");
    }
    // [memory] diagnose = 1 records who freed each block, to find the game code behind the stale writes.
    if (g_quarantine && GetPrivateProfileIntA("memory", "diagnose", 0, ini))
    {
        g_freedBy = static_cast<FreedBy*>(VirtualAlloc(nullptr, kRingCapacity * sizeof(FreedBy), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        BackendLog("memory: diagnostics %s\n", g_freedBy ? "on" : "unavailable (out of memory)");
    }
    // [memory] allocation_sites = 1 records which code allocated each block the game holds, and lists the sites that
    // grow (about 7 MB).
    if (GetPrivateProfileIntA("memory", "allocation_sites", 0, ini))
    {
        g_tracked.bits = kTrackedBits;
        g_tracked.keys = static_cast<uintptr_t*>(VirtualAlloc(nullptr, g_tracked.Size() * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        g_tracked.values = static_cast<uint16_t*>(VirtualAlloc(nullptr, g_tracked.Size() * sizeof(uint16_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        auto* sites = static_cast<AllocationSite*>(VirtualAlloc(nullptr, (kSiteCapacity + 1) * sizeof(AllocationSite), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (g_tracked.keys && g_tracked.values)
            g_sites = sites;  // last: a set g_sites means everything is ready
        BackendLog("memory: allocation sites %s\n", g_sites ? "recorded" : "unavailable (out of memory)");
    }
    if (g_quarantine)
        BackendLog("memory: compatibility layer on, quarantine up to %u MB\n", megabytes);
    else
        BackendLog("memory: compatibility layer on, without the quarantine\n");
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
            // HeapAlloc too: an allocation that fails is retried after the quarantine gives its memory back.
            const int patched = PatchImports(module, "HeapFree", reinterpret_cast<void*>(&CompatHeapFree)) +
                                PatchImports(module, "HeapReAlloc", reinterpret_cast<void*>(&CompatHeapReAlloc)) +
                                PatchImports(module, "HeapAlloc", reinterpret_cast<void*>(&CompatHeapAlloc));
            if (g_patchedCount < static_cast<int>(sizeof g_patched / sizeof g_patched[0]))
                g_patched[g_patchedCount++] = module;
            if (patched)
                BackendLog("memory: %s covered (%d imports)\n", name, patched);
            if (_stricmp(name, "msvcrt.dll") != 0 && _stricmp(name, "mfc42.dll") != 0)
                AddGameCode(module);
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
    AcquireSRWLockExclusive(&g_lock);
    BackendLog("memory: the game holds %ld blocks, %.1f MB; %ld allocations, %llu frees, %llu moved resizes, %llu shrinks "
               "kept in place, %llu double frees ignored; quarantine %u blocks, %.1f MB of %.0f MB; %llu released, %llu kept "
               "after stale writes (%.1f MB); %llu releases under pressure\n",
               g_heldBlocks, g_heldBytes / (1024.0 * 1024.0), g_allocs, g_frees, g_moves, g_shrinksKept, g_doubleFrees,
               g_ringCount, g_bytes / (1024.0 * 1024.0), g_limitBytes / (1024.0 * 1024.0), g_released, g_modified,
               g_modifiedBytes / (1024.0 * 1024.0), g_pressureReleases);
    // Every 5 minutes the sites that grew, every 30 minutes also the ones holding the most.
    static uint32_t reports;
    if (g_sites && ++reports % kReportsPerSiteList == 0)
        LogAllocationSites(reports % (6 * kReportsPerSiteList) == 0);
    ReleaseSRWLockExclusive(&g_lock);
}

bool RelieveMemoryPressure(const char* why)
{
    // One failure leaves the limit alone: code that tries a large allocation and then a smaller one is normal.
    if (!g_quarantine)
        return false;
    AcquireSRWLockExclusive(&g_lock);
    const bool released = ReleaseUnderPressure(why, false);
    ReleaseSRWLockExclusive(&g_lock);
    return released;
}

void NoteAddressSpace(uint64_t freeBytes, uint64_t largestFree)
{
    // The address space itself running short lowers the limit for the rest of the session.
    if (!g_quarantine || (largestFree >= kPressureLargestFree && freeBytes >= kPressureFree))
        return;
    AcquireSRWLockExclusive(&g_lock);
    ReleaseUnderPressure("the address space is running short", true);
    ReleaseSRWLockExclusive(&g_lock);
}

bool AllocationSitesEnabled()
{
    return g_sites != nullptr;
}

void CaptureGameCallers(const void* stack, void** frames, int count)
{
    CaptureCallers(static_cast<const uint32_t*>(stack), frames, count);
}

void DescribeCode(void* address, char* out, size_t size)
{
    Describe(address, out, size);
}
