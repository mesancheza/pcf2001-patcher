// Memory compatibility layer (see docs/DESIGN.md, D13, and docs/GAME-INTERNALS.md).
//
// The game keeps using memory after freeing it: it reads stale data (garbled or misassigned names) and writes into
// freed blocks, which corrupts the heap and eventually crashes ("La aplicación no puede continuar", or a crash inside
// Windows' heap code). On the Windows versions of its time freed memory usually stayed untouched for a while, which hid
// these bugs. This layer restores that: blocks the game frees are kept in a quarantine and only given back to Windows
// later, so stale reads still see the old data and stale writes land in memory nobody else uses.
//
//   - frees go into a first-in, first-out quarantine, limited by size; the oldest blocks are released first;
//   - a block that changed while in quarantine (the game still writes to it) is never released;
//   - freeing a block that is already in quarantine (a double free) is ignored;
//   - shrinking a block never moves it, as on Windows XP (the game keeps pointers into buffers it shrinks);
//   - growing a block keeps it in place when the heap can; when it has to move, the old block goes into quarantine
//     like a free.
//
// It intercepts HeapFree and HeapReAlloc in the imports of the game's modules and of the C runtime they use.
#pragma once

// Installs the interception in every game module loaded so far. Safe to call again: modules loaded later (e.g. the
// game's 3D backend) are patched, already-patched ones are skipped.
void InstallMemoryCompat();

// Writes the quarantine statistics to dcogl.log.
void LogMemoryCompatStats();
