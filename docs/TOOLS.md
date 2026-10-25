# Development tools

Tools in this repository that help develop the patch. **None of them are part of the patch players install.**

## The patch's own log and diagnostic mode

The new backend writes `dcogl.log` for every session, and keeps the previous one as `dcogl.old.log` ([DESIGN.md D14](DESIGN.md#d14-a-log-players-can-send)). Settings for testing go in `dcogl.ini`, next to `dcogl.dll`:

```ini
[memory]
; 0 turns the whole memory layer off, to compare with the original behavior. Default 1.
layer=1
; Size of the quarantine for freed memory, in MB. Off (0) by default: freed blocks go straight back to Windows, while
; the rest of the layer (shrunk blocks stay in place, the counts in the log) stays on. For testing, e.g. 192.
quarantine_mb=0
; 1 records which code freed each block the game keeps writing to, and writes it to the log when that block leaves
; the quarantine. Costs about 16 MB and some speed.
diagnose=0
; Fills every new block the game allocates with this byte (hexadecimal), instead of whatever the heap had there, so
; reads of memory the game never initialized show up as strange values. Off when absent. Works with the quarantine on
; or off. With 55, a 32-bit number read from uninitialized memory is 1431655765, and a pointer is 0x55555555.
fill_new=55
; 0 never resizes a block in place: growing always moves it to a new block (shrinking still keeps it). For use with
; Windows' page heap (below): its in-place resizes can report overruns that aren't there, and the moved blocks get
; a guard page, so a real overrun faults at the code that writes it. Default 1.
resize_in_place=1
; 1 lists in the log, every 5 minutes, the code whose allocations grew the most since the previous list, and every
; 30 minutes the code holding the most: to find what grows during a long session. The same for the code that created
; the surfaces alive. Costs about 7 MB and a little speed.
allocation_sites=0

[diagnostics]
; Writes a full memory dump, dcogl-<date>-<time>.dmp next to dcogl.dll, for each of the first N faults in the game's
; own code (manager.exe, gfxwin.dll and the other files in the game's folder). The game catches its faults and shows
; "La aplicación no puede continuar" without Windows writing a dump, so this is the way to see what led to one. Each
; dump is about as large as the game's memory, often 1 GB, and holds everything in it: for testing, not for players
; to post. Off when absent or 0.
dump_on_fault=1
; Writes every region of the address space to dcogl-memory-NNN.tsv, next to dcogl.dll, every N memory reports (one
; every 30 seconds): allocation, region, size, state (commit or reserve), type (image, mapped or private) and the file
; behind it. Comparing two lists shows what took the space in between. Off when absent or 0.
memory_map=10
```

A small quarantine (for example `quarantine_mb=32`) makes the memory bugs show up within minutes, which helps when testing changes to the layer.

| Line | Meaning |
|---|---|
| `memory: the game holds N blocks, X MB; N allocations, N frees, …` | Every 30 seconds: the blocks the game holds on the heap and their size (allocated minus freed since the backend started), then what it freed, the quarantine's size, how many blocks went back to Windows, and how many were kept because the game wrote to them after freeing them. |
| `memory: address space X MB free, largest free block X MB; images …, mapped …, private …; … surfaces …; … handles` | Every 30 seconds, and before and after the first Direct3D device: the game's 2 GB of address space by kind of use ([GAME-INTERNALS.md](GAME-INTERNALS.md#address-space)). *Mapped* includes the surfaces' pixels. |
| `memory: N allocation sites, …; grown the most:`, then lines `memory:   +X MB, +N blocks; now X MB in N blocks, … at <module>+<offset> < …` | With `allocation_sites=1`, every 5 minutes: the code whose allocations grew the most since the previous list, identified by its first return addresses in the game's code. A leak is a site that keeps growing list after list. Every 30 minutes the list *allocation sites holding the most* follows. |
| `surfaces: N creation sites; grown the most:`, then lines `surfaces:   +N; now N alive, X MB of address space, N created in all; at gfxwin.dll+0x66863 < …` | With `allocation_sites=1`, every 5 minutes: the code whose surfaces grew in number the most. The first address is always `CGFXSurface::Create`; the next ones are the loader and the game code that asked for the surface. Every 30 minutes the list *creation sites holding the most* follows. |
| `memory: <reason>; quarantine emptied (X MB given back), its limit …` | The memory layer gave its quarantine back because an allocation or a surface failed, or because the address space ran short ([DESIGN.md D13](DESIGN.md#d13-a-memory-compatibility-layer-while-the-games-memory-bugs-are-found-and-fixed)). |
| `memory: freed block of N bytes at … was written to afterwards; kept. Starts: … freed by: …` | A freed block the game kept writing to. Shows its first bytes, as hexadecimal and as text, and, in diagnostic mode, the code that freed it. |
| `exception: C++ <type>, code …` | The game threw a C++ exception. Code `0x4e3b` is the game's normal way of quitting. |
| `exception: access violation at <module>+<offset>, reading address …` | A fault. The game may handle it and continue, or show *"La aplicación no puede continuar"*. |
| `diagnostics: dump written to dcogl-….dmp` | With `dump_on_fault`, the dump of the fault logged just before. The game is frozen while it's written, a few seconds. |
| `window was made topmost; making it a normal window again` | Expected once per session; see [GAME-INTERNALS.md](GAME-INTERNALS.md#full-screen-window-always-on-top). |

### Windows' page heap

The strictest test for the game's memory bugs. With page heap on, every block the game allocates gets its own page, followed by an inaccessible one, and freed memory is made inaccessible, so the game faults at the very instruction that overruns a block or touches freed memory, instead of corrupting something that crashes later. It's a Windows setting for `manager.exe` (administrator rights), in both registry views because the game is 32-bit:

```
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\manager.exe
HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\manager.exe
    GlobalFlag    = 0x02000000   (page heap)
    PageHeapFlags = 0x3          (full page heap, with allocation stack traces)
```

Delete both values (or the keys) to turn it off. Use it with `quarantine_mb=0` (freed blocks reach Windows at once, so page heap sees every later access to them; the layer's fix for shrunk buffers stays on, or that known bug would stop the game first), `resize_in_place=0`, and `dump_on_fault`. The game is much slower, and every block takes at least 8 KB of address space, so a session runs out of its 2 GB after one to three hours ([GAME-INTERNALS.md](GAME-INTERNALS.md#address-space)). **Turn it off for anything but hunting memory bugs.**

When page heap finds a damaged block it stops the game with `0xC0000421` (`STATUS_VERIFIER_STOP`) without going through the patch's exception handler, so `dump_on_fault` doesn't see it. Windows' own crash dumps do, when set up for `manager.exe` (`HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\manager.exe`, `DumpType` 2); in the dump, `!analyze -v` names the stop and `!ext.heap -p -a <block>` shows the block's history.

## is6-cab

`tools/is6-cab` builds `is6-cab.exe`, which reads InstallShield 6 archives like the game CD's `DATA1.HDR` with the same code as the installer ([INSTALLER.md](INSTALLER.md)):

```
is6-cab list <CD>\DATA1.HDR                 components, file groups, and every file with its size and flags
is6-cab manifest <CD>\DATA1.HDR             every file with its MD5 and where it is: "archive", or its path on the CD
is6-cab extract <CD>\DATA1.HDR <carpeta>    unpacks the files inside the archive, into one folder per file group
```

`manifest` checks the whole CD: it finds every file that isn't inside the archive and verifies its MD5, and exits with code 1 if any is missing or damaged. Useful to check a CD image before installing, or to compare installations.

## backend-trace

`tools/backend-trace` builds `dcogl.dll`, a logging stand-in for the game's graphics backend. It does two jobs while the game runs normally:

- **Tracing:** it records how the game uses each method of the backend interface ([BACKEND-INTERFACE.md](BACKEND-INTERFACE.md)).
- **Crash recording:** it works as a "black box" that captures what happened when the game crashes or quits on its own, without a debugger. A debugger changes how Windows hands out memory, and that hides some of this game's bugs.

### How it works

- **Name:** it's built as `dcogl.dll`, the name of a fourth backend that the game recognizes but that was never shipped. It replaces none of the game's files.
- **Forwarding:** when the game loads it, it loads the game's real `dcd3d.dll` from the same folder and passes every request through to it.
- **Logging:** for every device object the real backend creates, it swaps the object's method table for a copy whose entries log each call and then continue into the original method. All registers and flags are preserved, so the game behaves exactly as with `dcd3d.dll`.
- **The object itself is untouched,** so code that reads the device's fields directly still works.
- **Exception recorder:** an exception handler sees every exception the moment it's raised, before the game's own error handling runs, and logs it. It never changes how the exception is handled.
  - For C++ exceptions it logs the thrown type and the error code and text the game's exception objects carry. The game also throws exceptions as part of normal control flow, for example three when quitting.
  - For faults, such as access violations, it logs the registers, the code addresses found on the stack and the last 64 device calls, and writes a full memory dump for the first two.
- **Shutdown call path:** when a device is destroyed, it logs the code addresses on the stack, which shows what decided to shut graphics down.
- **Heap tracker:** it intercepts `HeapAlloc`, `HeapFree` and `HeapReAlloc` in the game's modules. All of the game's memory management ends up there: `gfxwin.dll`'s own helpers call them directly, and the C runtime calls them for every `malloc`, `free`, `new` and `delete`. It remembers every freed block in a fixed table of about 19 MB: address, size, time, and the first few callers in the game's own code. A block that moves during a resize counts as a free of its old address.
  - At a fault, it checks the registers and the faulting address against that table, and logs any freed block they point into: who freed it, how long before the fault, and whether the address has been handed out again since.
  - Once a minute, it logs how much memory the game frees.
- **Call stacks are approximate:** the game's code doesn't keep reliable frame pointers, so the stack is scanned for values that point just after a call instruction. Real callers are always among the entries, but some entries may be stale.

### Using it

1. Build it with `build.bat` (see [BUILDING.md](BUILDING.md)).
2. Copy `build\Release\tools\backend-trace\dcogl.dll` into the game folder, next to `manager.exe`.
3. Start the game with the command line `manager.exe dcogl.dll`, for example from a Windows shortcut whose target ends in `manager.exe dcogl.dll`.
   - The command line must be exactly that, with no trailing space. [GAME-INTERNALS.md](GAME-INTERNALS.md#how-the-game-chooses-a-graphics-backend) explains why.
   - PowerShell's `Start-Process` adds a trailing space; Windows shortcuts don't.
4. Play normally, then quit the game from its menu so the final summary gets written.
5. Read `dcogl-trace.log` in the game folder.

To remove it, delete `dcogl.dll`, `dcogl-trace.log` and any `dcogl-crash-*.dmp` files from the game folder.

### Output files

| File | Contents |
|---|---|
| `dcogl-trace.log` | Everything below. Reset each time the game starts. |
| `dcogl-crash-1.dmp`, `dcogl-crash-2.dmp` | Full memory dumps taken at the first two faults. They're several hundred MB each, and they contain the game's memory, so never share them publicly or commit them (see [CONTRIBUTING.md](../CONTRIBUTING.md)). |

### Log format

Lines starting with a `[HH:MM:SS.mmm]` time stamp use the computer's local time, to match events with what the player was doing.

| Line | Meaning |
|---|---|
| `loaded <path>` | The real backend was loaded. |
| `[time] device N created at <address>, tracing` | Device for adapter N was created and is being traced. |
| `[time] destroying device <address>` | A device is being destroyed. Followed by `requested from:` and the code addresses that led to it. |
| `[time] EXCEPTION C++ <type>, thread T, thrown at <where>` | A C++ exception was thrown. Followed by `object:` (the first value of the exception object and its text, if it has one) and `thrown from:` (code addresses on the stack). |
| `[time] EXCEPTION 0x<code> at <where>, thread T` | A fault, e.g. `0xc0000005` for an access violation. Followed by the address involved, the registers, `call stack (scan):`, the last device calls, `freed blocks referenced at the fault:`, and whether a dump was written. |
| `heap tracker: N imports intercepted, …` | The heap tracker is active. |
| `[time] heap: … frees/min (… of them moved blocks), … MB freed/min` | Memory freed by the game during the last minute. |
| `reg=0x… is inside block 0x… (N bytes) freed X s before the fault by …` | A register or the faulting address points into memory that had been freed, with the game code that freed it. A following line says if the address was handed out again since. |
| `NEW  slot S +0xOFF from <module>+0x<offset>` | First call to method S from that call site. `<module>+0x<offset>` is the return address, which identifies the calling code. |
| `CALL slot S +0xOFF #K this=<address> from <caller> args: …` | The K-th call to method S, with the first six 32-bit values on the stack. Not every method takes six arguments; the extra values are whatever else is on the stack. Only the first 40 calls of each method are logged. |
| `---- summary (<reason>) ----` | Call counts and number of call sites per method, written when a device is destroyed and when the DLL unloads. |

The log is reset each time the game starts.
