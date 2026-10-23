# Game internals

What we have learned about how PC FÚTBOL 2001 v3.6 works, and why it fails on modern Windows. Addresses are relative virtual addresses (RVA) inside the v3.6 files listed in [DESIGN.md](DESIGN.md#d1-target-dinamics-v36-only), unless stated otherwise.

These notes describe the game's behavior in our own words. They don't contain Dinamic's code.

## Program structure

| File | Role |
|---|---|
| `pcf2001.exe` | Launcher. Checks for the CD, optionally plays the intro video, and starts `manager.exe`. |
| `manager.exe` | The game. MFC application built with Microsoft Visual C++ 6. |
| `gfxwin.dll` | Dinamic's graphics and windowing library: windows, controls, surfaces, fonts, file access, and the loader for graphics backends. |
| `dcsf.dll`, `dcsfx.dll`, `dcd3d.dll` | Graphics backends (see below). `dcogl.dll` is a fourth backend name the game knows, but it was never shipped. |
| `d2s2.dll` | Sound (DirectSound, EAX). |
| `Utils.dll` | Utilities. |
| `DllDPlay.dll`, `DLLSockets.dll` | Multiplayer (DirectPlay and TCP/IP). Out of scope. |

## CD check

Both `pcf2001.exe` and `manager.exe` look for a file named `DISK.ID`, containing `PCF9 00`, at the root of a drive that Windows reports as a CD-ROM. A disc image mounted with Windows 10/11's built-in "Mount" passes the check. The patch keeps this check ([DESIGN.md D5](DESIGN.md#d5-keep-the-cd-check)).

## Startup failure: "No se pudo iniciar DirectX"

### How the game chooses a graphics backend

`gfxwin.dll` holds a table of four backend DLL names, in this order:

| Index | DLL | Notes |
|---|---|---|
| 0 | `dcsf.dll` | Selecting it also forces 8-bit (256-color) mode |
| 1 | `dcsfx.dll` | **Default** |
| 2 | `dcd3d.dll` | Direct3D 7 |
| 3 | `dcogl.dll` | Never shipped |

The current index is a global variable at `gfxwin.dll` RVA `0xFE960`, with an initial value of 1. The name table starts at RVA `0xFE964`.

At startup, `CGFXWinApp::InitInstance` (RVA `0x7EA30`) checks the program's command line: if it is longer than 4 characters and ends in `.DLL` (case-insensitive), it looks for the **whole command line** in the table and selects that backend. So `manager.exe dcd3d.dll` selects `dcd3d.dll`. Because the comparison covers the whole command line, a trailing space breaks it. PowerShell's `Start-Process` adds one, for example, while Windows shortcuts don't.

### Why it fails

When the main window is created, `CGFXMainWindow::Start` (RVA `0x58DC0`) loads the selected backend with `CGFX_API::Load` (RVA `0x471E0`). The game always asks for 3D support, so `Load` only accepts the backend if at least one of its devices reports at least one **3D-capable display mode**.

On modern Windows:

| Backend | 2D modes reported | 3D modes reported | Result |
|---|---|---|---|
| `dcsfx.dll` (the default) | 32-bit only | **none** | rejected |
| `dcsf.dll` | none | none | rejected |
| `dcd3d.dll` | 16- and 32-bit | 16- and 32-bit | accepted |

So with the default backend, `Load` fails, `Start` fails, and `InitInstance` shows *"No se pudo iniciar DirectX. El programa no puede comenzar."*. On the hardware and drivers of 2001, `dcsfx.dll` reported 3D modes.

Starting the game as `manager.exe dcd3d.dll` works around it (the game reaches its main menu and plays), but `dcd3d.dll` still depends on legacy DirectDraw behavior and changes the display mode in full screen. The patch replaces it with a new backend instead ([DESIGN.md D6](DESIGN.md#d6-replace-the-graphics-component-through-the-games-own-plug-in-interface-using-direct3d-11)).

## Settings file: `manager.ini`

Plain text, one `KEY: VALUE` per line, read at startup and written on exit. A value is "on" when it starts with `ON`.

| Key | Meaning |
|---|---|
| `MUSICA`, `VOLUMEN MUSICA` | Music on/off and volume |
| `SONIDO`, `VOLUMEN SONIDO` | Sound effects on/off and volume |
| `TRANSICIONES` | Screen transitions on/off |
| `PANTALLA COMPLETA` | Full screen (`ON`) or windowed (`OFF`) |
| `POSICION PANTALLA` | Window position, `x, y` |
| `NETMANAGER`, `NETSIM`, `IPSERVER`, `NPORTSERVER` | Multiplayer settings |

The game also writes `sound.ini`, `sip.ini`, `update.ini` (contains `V3.6`) and `MainOpt.dat`.

## Windowed mode draws elements in the wrong place

In windowed mode, some elements, such as menu buttons and dialog contents, are drawn offset by the window's position on the desktop. The offset matches the `POSICION PANTALLA` value, so the problem is barely visible with the window near the top-left corner of the screen and obvious anywhere else. The old drawing path mixes up screen coordinates and window coordinates. The new backend presents complete frames into its own window ([DESIGN.md D7](DESIGN.md#d7-presentation-native-resolution-scaled-no-display-mode-changes)), which removes this whole class of problem.

## Garbled and misassigned names

**Symptom:** without the "Windows XP SP2" compatibility mode, some long team names display as garbage. For example, "Unión Deportiva Las Palmas, S.A.D." on the *Introduce nombre y equipo* screen. Some player names appear on the wrong players. With XP SP2 compatibility mode, everything displays correctly.

**What we have verified:**

- The database files are correct. Every index entry in the database files points at a valid record, and the names decode correctly at their indexed positions, both in the original data and after Dinamic's data updates.
- The text decoding is correct and simple (see below).

So the fault is in the game's code, and it depends on behavior that changed after Windows XP, such as how memory is reused.

**Cause:** the game reads names through pointers to memory it has already freed (see [Memory bugs](#memory-bugs)). Confirmed by experiment: on the *Introduce nombre y equipo* screen, choosing F.C. Barcelona shows garbage next to the coach's name on the original heap, and "F.C. Barcelona" with freed memory held in quarantine ([DESIGN.md D13](DESIGN.md#d13-a-memory-compatibility-layer-while-the-games-memory-bugs-are-found-and-fixed)). With a quarantine too small for the stale pointer's lifetime, a garbled name shows up again, in the *Fichar* screen's transfer list for example. The garbage is only in memory: loading the same saved game with a large enough quarantine shows the name correctly, so saved games are not damaged.

The first stale pointers found come from how the game loads its records: see [Found: pointers into buffers that get shrunk](#found-pointers-into-buffers-that-get-shrunk).

## Memory bugs

The game has use-after-free bugs in many places: it keeps pointers to heap blocks after freeing them, then reads and writes through them. On the Windows versions of its time this mostly went unnoticed. On Windows 10/11 the heap reuses freed memory differently, so:

- **Stale reads** return whatever reused the memory: garbled or misassigned names.
- **Stale writes** corrupt whatever reused the memory, or the heap's own bookkeeping. In crash dumps, the heap is overwritten with ASCII digits, and Windows' heap code faults on addresses such as `0x30313631` (the text "1610").

These are the random crashes that end with *"La aplicación no puede continuar"*. The game catches the fault, shows that message, and then exits.

Observed in an automated season with the memory layer on: the game played without errors for about 15 minutes, until its 192 MB quarantine filled up and started releasing blocks. Thirty seconds later Windows' heap code faulted on ASCII text and the game stopped. The game frees memory quickly: about 150,000 frees and 400,000 resizes in 15 minutes.

Both kinds of access go unnoticed under a debugger, because Windows uses a slower debug heap there, which reuses memory differently.

### Found: pointers into buffers that get shrunk

Found with Windows' page heap, which makes every access to freed memory fault immediately.

The function at VA `0x499F80` in `manager.exe` loads a team record:

1. It allocates one buffer for the whole record and stores it in the team object at `+0x88`.
2. It reads the record's strings into that buffer, one after another, and points fields of the team object into the middle of it. The team's name is at `+0x04`.
3. At the end, if the record used less space than allocated, it shrinks the buffer to the size actually used, with `gfxwin.dll`'s `ResizeMemFun` (a `HeapReAlloc`, at `0x49A403`).
4. Right after that, it formats `"<E>%s|%d</>"` with the name pointer (at `0x49A430`).

On Windows XP, shrinking a heap block always happened in place, so the interior pointers stayed valid. Today's heap moves a block when the new size falls into a different size class. The name pointers are then left on the freed old block. This is the cause of the garbled team names: on the *Introduce nombre y equipo* screen, F.C. Barcelona shows correctly once shrinking never moves blocks, even with the quarantine practically off. The game probably loads other records (players, coaches) the same way.

**Fix:** the memory layer never moves a block when the game shrinks it, as on Windows XP ([DESIGN.md D13](DESIGN.md#d13-a-memory-compatibility-layer-while-the-games-memory-bugs-are-found-and-fixed)). The only cost is the unused tail of each shrunk buffer.

## Text drawing reads outside its glyph image

`Utils.dll` blends text into 32-bit surfaces with an 8-bit glyph mask (the function at RVA `0x25FE0`). For each row it reads the mask byte **before** checking whether the position is inside the mask, so it reads one byte before the mask's pixels. With the original backends, surface pixels were heap memory, where that byte is always readable. Memory that starts exactly at a page boundary may have nothing mapped before it, and then the game faults. The new backend allocates every surface's pixels with 4 KB of spare memory before and after them.

## Full-screen window always on top

In full screen, `CGFXMainWindow::ResetMode` (`gfxwin.dll`) makes the game window topmost (`HWND_TOPMOST`). With DirectDraw's exclusive mode this didn't matter. With a borderless window it keeps the game covering the screen after Alt+Tab, so the new backend removes the topmost state.

## Crash on exit

**Symptom:** the game can crash while quitting, with an access violation at an address outside any loaded module.

**Cause, from a crash dump:**

1. When the game quits, `gfxwin.dll` shuts down graphics and destroys the backend's device objects. Their memory is freed.
2. Afterwards, the C runtime's exit code runs the destructors of `manager.exe`'s global objects. At least one of them is a `CGFXSurface`.
3. `CGFXSurface::Destroy` (`gfxwin.dll` RVA `0x67580`) checks whether the surface still holds a backend handle (field `+0x30`) that isn't marked as already released (field `+0x34`). If so, it calls slot 24 (`+0x60`, "release surface handle") on the device the surface was created with, stored in field `+0x38`.
4. That device has already been freed. Its memory has been reused, so the first value no longer points to a method table, and the call jumps to a garbage address.

This is a use-after-free: an ordering bug in the game's shutdown. It may have gone unnoticed on older Windows, where freed memory often kept its old contents for a while; that part is unconfirmed.

**Fix:** the new backend handles this in its own code, without changing the game's files. It keeps destroyed devices' objects valid until the process ends, and their methods do nothing once the device is shut down.

## Bugs reported in the original game

Bugs players remember from the game as it was in 2001, on the Windows of its time. Whether the patch fixes them is still to be decided: so far it only fixes what modern Windows broke ([DESIGN.md D8](DESIGN.md#d8-fix-bugs-at-their-cause-in-the-games-code)).

| Bug | Details |
|---|---|
| Absurd TV offers in lower divisions | See below. |
| The groundskeeper reports finished works that never happened | Every week, the *Encargado de Campo* (groundskeeper) reports that the works are finished, although nothing was done. |

### Absurd TV offers

**Symptom:** in *Finanzas → Ofertas TV*, third-division clubs get TV offers "por todos los derechos" (for all the rights) of millions of millions of pesetas, clearly not intended. Seen consistently with Italian Serie C1 clubs (Como, Fidelis Andria), for example 2.448.132, 28.060.451, 36.618.891 and 42.090.678 million. Players remember this from the original game in 2001. Second division unconfirmed.

**Related, probably the same bug:** some offers show only their duration, with no amount, and accepting one gives a contract worth 0. These appear in the same lists as the absurd amounts. A likely explanation is an amount so large that it overflows when it's formatted or converted, but that hasn't been checked. The offers are random, so the exact list changes with every new game.

**Ruled out:** the memory layer's quarantine. With its diagnostic fill of new memory (`fill_new=55`, [TOOLS.md](TOOLS.md#the-patchs-own-log-and-diagnostic-mode)), the amounts didn't take values that would come from uninitialized memory.

**Leads for a fix:** the offer texts come from the game's `contratostv.cpp` (their table entries are tagged `tx_contratostv_cpp`). The "%s por todos los derechos." format string is selected by language into the global at VA `0xBCE7B8` in `manager.exe`, by code at VA `0x986550`. The code that calculates the amounts hasn't been found yet.

**Decision pending:** whether the patch fixes this. It changes the game's economy, which is a gameplay decision ([DESIGN.md D8](DESIGN.md#d8-fix-bugs-at-their-cause-in-the-games-code), non-goals).

### Youth player offered a salary of 0

**Symptom:** offering a contract to a youth player (*Juveniles*; seen with a 16-year-old whose current salary was 1 million, Fidelis Andria, Serie C1) starts the salary at 0. Seen with the patch; not yet known whether the original game did the same.

## Text encoding

Strings in the data files are stored as a little-endian length followed by the encoded bytes. `gfxwin.dll` exports the decoders as methods of `CDMReadBinaryFile`:

| Method | Length field | Encoding |
|---|---|---|
| `Decode` | 2 bytes | every byte XOR `0x61` |
| `DecodeShort` | 1 byte | every byte XOR `0x61` |
| `DecodeEx` | 2 bytes | byte *i* XOR (*length* − `0x40` + *i*), applied to the bytes as read from the file |

Decoded text is Windows-1252 (Western European).

## Database files (`DBDAT\*.FDI`)

`EQ00022.FDI` (teams), `JUG00022.FDI` (players), `ENT00022.FDI` (coaches) and `EST00022.FDI` (stadiums) share a container format:

| Offset | Size | Content |
|---|---|---|
| 0 | 8 | Signature `DMFIv1.0` |
| 8 | 8 | Zero |
| 16 | 4 | Number of records, *n* |
| 20 | 13 × *n* | Index entries |

Each index entry is: record ID (4 bytes), one byte (purpose unknown, zero in the files examined), record offset (4 bytes), record length (4 bytes). All values are little-endian.

Team records start with the text `Copyright (c)2000 Dinamic Multimedia`; player, coach and stadium records don't. Record layouts are being documented as the work requires them.
