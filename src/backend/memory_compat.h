// Memory compatibility layer (see docs/DESIGN.md, D13, and docs/GAME-INTERNALS.md).
//
// The game keeps using memory after freeing it: it reads stale data (garbled or misassigned names) and writes into
// freed blocks, which corrupts the heap and eventually crashes ("La aplicación no puede continuar", or a crash inside
// Windows' heap code). On the Windows versions of its time freed memory usually stayed untouched for a while, which hid
// these bugs. This layer restores that: blocks the game frees are kept in a quarantine and only given back to Windows
// later, so stale reads still see the old data and stale writes land in memory nobody else uses.
//
//   - frees go into a first-in, first-out quarantine, limited by size; the oldest blocks are released first (off by
//     default now: [memory] quarantine_mb turns it on, for testing);
//   - a block that changed while in quarantine (the game still writes to it) is never released;
//   - freeing a block that is already in quarantine (a double free) is ignored;
//   - shrinking a block never moves it, as on Windows XP (the game keeps pointers into buffers it shrinks);
//   - growing a block keeps it in place when the heap can; when it has to move, the old block goes into quarantine
//     like a free;
//   - under memory pressure (an allocation or a surface that fails, or the address space running short) the quarantine
//     gives its memory back and lowers its limit: the game is a 32-bit program, with 2 GB of address space at most.
//
// It intercepts HeapAlloc, HeapFree and HeapReAlloc in the imports of the game's modules and of the C runtime they use.
// The same hooks count the blocks the game holds, for the log, and with [memory] allocation_sites = 1 record which code
// allocated them, to find what grows during a long session.
#pragma once

#include <cstdint>

// Installs the interception in every game module loaded so far. Safe to call again: modules loaded later (e.g. the
// game's 3D backend) are patched, already-patched ones are skipped.
void InstallMemoryCompat();

// Writes the quarantine statistics to dcogl.log.
void LogMemoryCompatStats();

// After something failed for lack of memory: gives the quarantine's memory back, logging why. Returns whether anything
// was released, i.e. whether retrying what failed may now succeed.
bool RelieveMemoryPressure(const char* why);

// Called with each memory report's figures: when the free address space, or its largest block, runs short, gives the
// quarantine's memory back and halves its limit for the rest of the session (down to a floor).
void NoteAddressSpace(uint64_t freeBytes, uint64_t largestFree);

// Whether [memory] allocation_sites is on: then other diagnostics (the surfaces) record their creators too.
bool AllocationSitesEnabled();

// The innermost return addresses into the game's own code on the stack, from `stack` up (a function's
// _AddressOfReturnAddress()), as the allocation sites find them. Unused entries are null.
void CaptureGameCallers(const void* stack, void** frames, int count);

// "module+0xoffset" for a code address.
void DescribeCode(void* address, char* out, size_t size);
