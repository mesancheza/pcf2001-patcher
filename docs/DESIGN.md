# Design

This document records the goals of the patch and every design decision, with the reasoning and the alternatives considered. When a decision changes, it is updated here rather than deleted, so the history stays visible.

## Goals

1. PC FÚTBOL 2001 v3.6 runs correctly on Windows 10 and 11, without extra tools or settings.
2. The game looks and plays exactly as it did on the hardware of its time, minus the bugs caused by modern Windows.
3. Players can verify what the patch does, and build it themselves.

## Non-goals

- Changing gameplay, balance, the database, or the interface. Mods are out of scope.
- Multiplayer (DirectPlay or TCP/IP). Nobody uses it today and it would need a separate effort.
- Windows versions older than 10, or other operating systems.
- Removing the CD check.

## Decisions

### D1. Target Dinamic's v3.6 only

The patch applies on top of the original game plus Dinamic's official 3.6 update (`PCFPATCH3.6.EXE`, February 2001), and nothing else.

- **Why:** 3.6 is the final, bug-fixed build. Most installs in the community run it, and community guides and updates assume it.
- **Why only one version:** fixes to the game's code are byte changes at exact locations, which only apply to one specific build. 3.6 rewrote `manager.exe`, `gfxwin.dll` and `dcd3d.dll`, so supporting the original build as well would roughly double the reverse-engineering and testing work.
- **Consequence:** the patcher identifies files by SHA-256 hash and refuses to touch anything else. An install without 3.6 is told to apply 3.6 first.

Supported files (v3.6):

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `manager.exe` | 7,991,296 | `BCBB057E80E370DFD1647C7124CAC80CD8AC4BDCDC51ABB36CAA43A828DD63BC` |
| `gfxwin.dll` | 1,097,797 | `D11C89B34F3D5AEF1DB32EF38B2259D6B0633EC2AA49FED66E582A00FA12A4C9` |
| `dcd3d.dll` | 65,536 | `0844CCB1DF64B02C2EFD702ADEBF044F1661EAB4FA3B113824B28CFDB405A441` |
| `dcsfx.dll` | 69,632 | `583AA62BDA79CEDA553E748DE59CF0F79C6D29583CB37A413A2D497943126BC6` |
| `DCSF.dll` | 86,016 | `EC581D48FDA3221D2544D154434862613D7C860B64C0ED3EB3209FDE1D9A1FFF` |

The pre-3.6 files have different hashes. `dcsfx.dll` and `DCSF.dll` are not changed by 3.6.

### D2. Any database

The executables don't depend on the database. Dinamic's in-season data updates (`PCFACT01`–`PCFACT12`) and community databases only change data files, so the patch works with any of them.

### D3. Ship a patch, never Dinamic's files

Releases contain only our own code and a description of the changes to make to the player's files. They never contain the game's executables, libraries, data or media, patched or not.

- **Why:** those files belong to their rights holders. Anyone who can use the patch already has them, so a patch gives exactly the same result while redistributing nothing.
- **Consequence:** the patcher reads the player's files, verifies them (D1), and writes the modified versions locally.

### D4. Standalone

The patched game must run with nothing else installed or configured:

- no dgVoodoo or other DirectX wrappers;
- no Windows compatibility modes (which also force the game to ask for administrator rights);
- no Visual C++ redistributable: everything we build links the C runtime statically;
- no administrator rights to play.

**Why:** every extra step is a support problem, and something players get wrong. It also forces bugs to be fixed at their cause rather than worked around.

### D5. Keep the CD check

The game checks for its CD at startup. The patch leaves that check as it is; players use their CD, or an image of it mounted as a drive (Windows 10/11 mounts `.iso` files natively).

**Why:** removing copy protection is a legal problem in most countries, and mounting an image already works on modern Windows.

### D6. Replace the graphics component through the game's own plug-in interface, using Direct3D 11

The game doesn't draw through DirectX directly. Its graphics library, `gfxwin.dll`, loads one of several **backend DLLs** that implement a fixed interface: four exported functions plus a "device" object with 173 methods. See [BACKEND-INTERFACE.md](BACKEND-INTERFACE.md). The patch adds a **new backend, written from scratch, on top of Direct3D 11**, and makes it the default.

- **Why a backend:** it's the boundary the game's own developers designed for swapping graphics systems, so the game's code barely changes. The new backend can be developed and tested one method at a time, with the original `dcd3d.dll` handling whatever isn't finished yet.
- **Why Direct3D 11:** it's part of every Windows 10/11 installation, it's supported by every graphics card and driver from the last fifteen years, and it's the simplest modern API to implement correctly.

**Alternatives considered:**

| Alternative | Why not |
|---|---|
| dgVoodoo or another DirectDraw/Direct3D 7 wrapper | It's an external tool (D4), and in testing it broke the game's Direct3D backend entirely. |
| Vulkan | Much more complex for an 800×600, mostly 2D game, with no benefit on Windows. |
| Implementing all of DirectDraw and Direct3D 7 ourselves | A far larger interface than the game actually uses. |
| Rewriting the drawing code inside `gfxwin.dll` | Very invasive changes to Dinamic's code, and much harder to verify. |
| Only switching the game to its existing `dcd3d.dll` backend | It starts, but it still relies on DirectDraw's legacy behavior and display-mode changes. This is the fallback the new backend replaces. |

### D7. Presentation: native resolution, scaled, no display mode changes

The game keeps rendering at its own resolution. The new backend scales each finished frame to the window or the screen, **keeping the original aspect ratio** and filling the rest with black bars. It never changes the desktop's display mode, in windowed or full-screen mode.

**Why:** changing the display mode is slow, disruptive with multiple monitors, and unnecessary on modern hardware. It also removes the windowed-mode drawing bug, which comes from the old drawing path working in screen coordinates.

How full screen works:

- Full screen is a **borderless window covering the monitor**, not an exclusive mode. Mouse coordinates are mapped between the screen and the game's 800×600 frame.
- The window is **never "always on top"**, even though `gfxwin.dll` asks for it in full screen. With an exclusive mode Windows took the screen back on Alt+Tab; with a normal window, "always on top" would keep the game covering everything after switching away.
- Frames are presented **without waiting for the vertical blank**. The game redraws its progress screens while it computes (for example, between simulated matches), and waiting there would slow those loops to the monitor's refresh rate.
- The game can spend a long time computing without handling window messages. The backend turns off Windows' "not responding" ghost window, which would otherwise offer to close the game in the middle of a match day.

### D8. Fix bugs at their cause, in the game's code

Bugs that only appear on modern Windows, such as the garbled names, get fixed where they originate, not hidden by compatibility settings (D4). Each fix is documented: the file, the location, the original bytes, the new bytes, and why.

### D9. Toolchain: 32-bit C++, MSVC, CMake, static runtime

- **C++ with Microsoft's compiler:** the new backend must match the binary layout of the original C++ objects, which were built with Microsoft Visual C++ 6. Modern MSVC produces a compatible layout for the simple class model these objects use.
- **32-bit:** everything we build runs inside the game's 32-bit process.
- **CMake and Ninja:** both come with Visual Studio, and one script builds everything. See [BUILDING.md](BUILDING.md).
- **Static C runtime:** see D4.

### D10. The patcher

A single, small Windows program with a Spanish interface. Not built yet; planned behavior:

1. Ask for the game folder, or find it.
2. Verify each file to modify against its known v3.6 hash (D1). Do nothing if any file doesn't match.
3. Back up every file before modifying it.
4. Apply the documented changes and add the new backend DLL.
5. Offer to uninstall, restoring the backups exactly.

It needs no administrator rights unless the game is installed in a protected folder such as `Program Files`.

### D11. No network, no telemetry

Neither the patcher nor anything it installs makes network connections or collects data. The log (D14) stays on the player's computer unless they choose to send it.

### D12. Scope of testing

The patch is first tested by its developers on their own machines, then by members of the PC Fútbol community on varied hardware before any public release. The 3D match modes (Visionado and Interactivo) should work with the new backend, but the manager and the 2D Resumen mode come first.

### D13. A memory compatibility layer, while the game's memory bugs are found and fixed

The game uses memory after freeing it, in many places (see [GAME-INTERNALS.md](GAME-INTERNALS.md#memory-bugs)). This causes the garbled and misassigned names and the random "La aplicación no puede continuar" crashes. The new backend includes a layer that makes the game's frees safe:

- Freed blocks go into a **quarantine** instead of back to Windows, so stale reads still find the old data. The quarantine is first in, first out and limited in size (192 MB by default). The oldest blocks are released first.
- Before a block leaves the quarantine, the layer checks whether it changed since it was freed. **Blocks the game still writes to are never released**, because those writes would corrupt whatever reuses the memory, or the heap itself.
- Freeing a block twice is ignored.
- **Shrinking a block never moves it**, as on Windows XP. The game keeps pointers into the middle of buffers it shrinks; this alone fixes the garbled team names ([GAME-INTERNALS.md](GAME-INTERNALS.md#found-pointers-into-buffers-that-get-shrunk)).
- Growing a block keeps it in place when possible, as Windows does. When it has to move, the old block goes into the quarantine like a free.
- It works by redirecting `HeapFree` and `HeapReAlloc` in the import tables of the game's modules and of the C runtime they use.

**Why a layer first:** the game has many of these bugs, and each needs to be found in the code and patched at its cause (D8). The layer makes the game playable right away, and the bugs found with its diagnostics (D14) get fixed in the code one by one. Each code fix reduces how much the layer has to do.

**Why not the Windows XP compatibility mode:** it isn't standalone (D4) and it makes the game ask for administrator rights.

**Limits:** the game is a 32-bit program without large-address support, so it has 2 GB of address space, and the quarantine uses part of it. A stale pointer that outlives the quarantine still reads reused memory, so the layer postpones these bugs rather than removing them.

**Alternatives considered:**

| Alternative | Why not |
|---|---|
| Never free anything | The game frees hundreds of megabytes per hour of play; it would run out of address space. |
| Reproduce the old Windows heap's allocation order | Fragile, and it would hide the bugs without making them safe. |
| Only fix the bugs in the code | The right end state, but it needs every bug found first. |

### D14. A log players can send

The new backend writes `dcogl.log` next to the game, and keeps the previous session's log as `dcogl.old.log`. Players who report a problem attach both. The log starts with what the player is running: patch version, Windows version, screen and graphics card, the game version, and the size and checksum of the game's files and of the database files. After that it records setup, faults (with the module and offset), the game's own error exceptions, and memory statistics every 30 seconds.

- **Small:** a few kilobytes per hour of play, capped at 4 MB per session.
- **Private:** nothing that identifies the player. In particular no full paths, which usually contain the Windows user name. It is never sent anywhere automatically (D11).
- **Diagnostic mode:** `diagnose=1` in the `[memory]` section of `dcogl.ini` also records which code freed each block the game keeps writing to. It's off by default because it costs memory and speed.
