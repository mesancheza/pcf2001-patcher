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

## The CD's installer

The CD's `SETUP.EXE` is **InstallShield 6.11** (32-bit). On Windows 10/11 it fails while initializing, most often with *"Access is denied"*, and with other errors depending on the compatibility settings used. InstallShield 6 runs its engine (`IKERNEL.EX_`, unpacked as `iKernel.exe`) as a COM server registered in the system, which today's Windows doesn't allow in the way it expects. Its script also offers to install DirectX and Internet Explorer 5, neither of which installs on today's Windows. The patch doesn't try to make it work: it replaces it with its own installer ([INSTALLER.md](INSTALLER.md)).

### The archive format

The files are described by `DATA1.HDR` and stored in volumes `DATA1.CAB`, `DATA2.CAB`. The format is the one documented by the open-source *unshield* project for InstallShield 5–6; the installer has its own implementation (`src/installer/is6_cabinet.cpp`). All values are little-endian.

- **Common header** (20 bytes, at the start of the `.HDR` and of each `.CAB`): signature `ISc(`, version (`0x0100600C`: InstallShield 6), volume information, offset and size of the cabinet descriptor (`0x200` in the `.HDR`).
- **Cabinet descriptor** (offsets relative to its start): file table offset at `+0x0C`, directory count at `+0x1C`, file count at `+0x28`, offset of the file descriptors within the file table at `+0x2C`, then 71 file group list heads at `+0x3E` and 71 component list heads at `+0x15A`. Strings are null-terminated, at offsets relative to the descriptor.
- **File table:** first one offset per directory name, then the file descriptors, 0x57 bytes each: flags (`+0x00`), uncompressed size (`+0x02`, 64-bit), compressed size (`+0x0A`), data offset (`+0x12`), MD5 of the uncompressed data (`+0x1A`), name offset (`+0x3A`), directory index (`+0x3E`), volume number (`+0x55`). Flags: 1 = split across volumes, 2 = obfuscated, 4 = compressed, 8 = invalid.
- **File groups and components** are linked lists of (name offset, descriptor offset, next offset). A file group's descriptor gives its name and the range of files it holds (first and last index, at `+0x16` and `+0x1A`). A component's descriptor lists the file groups it uses (count at `+0x6F`, table offset at `+0x71`).
- **Volume header** (after the common header of each `.CAB`): where the volume's first and last files start, and how much of them it holds, for files split across volumes.
- **Compressed data** is a sequence of chunks: a 16-bit length, then that many bytes of raw DEFLATE data. Each chunk decompresses independently.
- **Files without the "compressed" flag aren't in the volumes at all:** they are plain files on the CD (`GRAFICOS.PAK`, the `DBDAT`, `SFX` and `SIMULDAT` folders, `MMX\Utils.dll`…). Their folder on the CD doesn't always match the archive's directory, so the installer finds them by name and size and checks their MD5.
- The end of `DATA1.HDR` lists the empty folders setup creates (`Notas`, `Save\Manager`, `Goles`), the registry value it writes (`HKLM\SOFTWARE\DINAMIC MULTIMEDIA\PC FÚTBOL 2001`, `DIR` = `<TARGETDIR>`), and the Start menu shortcuts (the game, *Desinstalar*, and two web links).

The archive holds 438 files: the game (331 files once installed, see [INSTALLER.md](INSTALLER.md#what-gets-installed)), InstallShield's engine and setup files, old Microsoft runtime libraries for `System32`, and DirectX and Internet Explorer 5 installers.

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

## Default 3D settings

On its first start, when `MainOpt.dat` doesn't exist yet, `manager.exe` (v3.6, the function at VA `0x60BDA0`) builds default settings for the 3D match modes:

1. It picks the 3D backend (`dcd3d.dll`, if it can do 3D) and that backend's **last** device.
2. It asks whether 640×480, then 320×240, are valid modes for it.
3. If neither is, it reads the first entry of `CGFXMainWindow::Modes(device)` without checking that there is one.

`Modes` looks the device number up in the **2D** backend, not the 3D one. On a PC where `dcd3d.dll` reports two DirectDraw devices ("Primary Display Driver" and the graphics card) and the 2D backend only one, `Modes` returns nothing and the game crashes reading a null pointer (at VA `0x60C027`). With Dinamic's backends the counts usually matched. The new backend reports as many devices as `dcd3d.dll` does, all of them the same Direct3D 11 output.

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

Bugs players remember from the game as it was in 2001, on the Windows of its time. The patch fixes the ones that crash the game; whether it fixes the others, which change how the game plays, is still to be decided ([DESIGN.md D8](DESIGN.md#d8-fix-bugs-at-their-cause-in-the-games-code)).

| Bug | Details |
|---|---|
| Crash with the advertising boards on Auto and no groundskeeper | Fixed by the patch. See below. |
| Absurd TV offers in lower divisions | Fixed by the patch. See below. |
| *Rellena* and *Auto* in *Vallas* seem to leave the last board unsold | Fixed by the patch. See below. |
| A Cafeterías work shortens another services work ordered the same week | Fixed by the patch. See below. |
| Absurd *Remodelar estadio* expenses after a cafeteria is finished | Fixed by the patch. See below. |
| The groundskeeper reports finished works that never happened | Every week, the *Encargado de Campo* (groundskeeper) reports that the works are finished, although nothing was done. Fixed by the patch for new cases: see [Absurd stadium expenses after a cafeteria is finished](#absurd-stadium-expenses-after-a-cafeteria-is-finished). |

### Crash with the advertising boards on Auto and no groundskeeper

**Symptom:** with the advertising boards (*Vallas*, in the *Estadio* screen) set to *Auto* and no *Encargado del campo* (groundskeeper) hired, the game stops with *"La aplicación no puede continuar"* while it advances the days, every time. Players remember a crash in the *Estadio* section without a groundskeeper from the original game. `dcogl.log` shows an access violation at `MANAGER.EXE+0xaba8e`, reading address `0x00000001`.

**To reproduce** (without the patch's fix): with no groundskeeper hired, set *Vallas* to *Auto* in *Estadio* and advance the days. Seen four times with Atl. Catania (Serie C1) in the 2000 preseason, the last two from a save made to test it, once within 15 seconds of advancing. With the fix, the same test save advances without crashing, and the messages go to the assistant.

**The team's staff:** the object that holds the managed team's state (at least `0x6024` bytes; the team record at `+0x11C` points to it) has the hired employees at `+0x5FD4`, one pointer per job, null when nobody is hired. The order is the one of the *Personal del club* screen and the *Empleados* help page: 0 to 3 the tactical, technical, fitness and goalkeeping coaches, 4 the doctor, 5 the psychologist, 6 the observer, 7 the technical secretary, 8 the youth coach, 9 the scout, 10 the groundskeeper (checked against a save's hired staff and the pointers in a dump). Each employee object has its quality in the byte at `+1`.

| VA | What it does |
|---|---|
| `0x4AB300` | Hires (or, with null, removes) the employee for a job. Removing touches only the job's pointer. |
| `0x49F730` | Returns the employee for a job, or null. |
| `0x4AB410` | Whether a kind of message has someone to go to: for kind 7, the groundskeeper (job 10); kind 8 is the assistant, always present. |
| `0x4AB5C0` | Delivers a message to the employee its kind belongs to. If nobody is hired for it (`0x4AB410`), it goes to the assistant instead. |

**The *Auto* option:** byte `+2` of the team state. The *Estadio* screen's handler for its control `0x3EC` (VA `0x4E5A10`) switches it on and off.

**Cause:** while the days advance, the code at VA `0x49DFF6` calls the function at VA `0x4ABA30` when *Auto* is on (and game option 6, read by `0x4D0690`, is off). That function handles the boards on its own: for each of 4 groups (probably the sides of the ground) it collects the sponsors' offers from the team state's list at `+0x6018`, sorts them by a criterion that depends on the groundskeeper's quality (5 to 7 one way, 8 to 10 another, any other value keeps their order), and has VA `0x4AB930` accept them while there is room left. It reads the quality through `0x49F730` without checking for null, so with no groundskeeper it reads address 1. Every other place that needs the groundskeeper checks first, and messages for him go to the assistant through `0x4AB5C0`; this one doesn't.

**Fix:** when there's no groundskeeper, quality 0: the boards are still sold automatically, taking the offers in the order they came, as the assistant would, and the messages go to the assistant as before. With a groundskeeper hired, nothing changes. The original instructions at VA `0x4ABA8C` have no room for a null check, so they jump to 13 bytes of new code in the padding after the function at `0x4AB5C0` (VA `0x4AB661`, 15 bytes of `nop` in v3.6), which jumps back. Bytes in [PATCHER.md](PATCHER.md#the-changes-to-managerexe).

Found with `dump_on_fault` ([TOOLS.md](TOOLS.md#the-patchs-own-log-and-diagnostic-mode)): the game catches the fault itself, so Windows never writes a dump.

### Absurd TV offers

**Symptom:** in *Finanzas → Ofertas TV*, third-division clubs get TV offers "por todos los derechos" (for all the rights) of millions of millions of pesetas, clearly not intended. Seen consistently with Italian Serie C1 clubs (Como, Fidelis Andria), for example 2.448.132, 28.060.451, 36.618.891 and 42.090.678 million. Players remember this from the original game in 2001. Second division unconfirmed.

Also seen with Atl. Catania (Serie C1): of three offers, two with only "+40 mill. por cada partido de la Copa de Italia" and one with 29.873.841 million for all the rights plus the same 40 per match.

**Empty offers:** some offers show only their duration, with no amount. For the lowest divisions the money per match depends on a random number from 0 to 5, and on 4 or 5 there is none; with no "all rights" amount either, about one offer in three has no money at all, and accepting it gives a contract worth 0. That's how the code is written, not a fault, but the patch leaves these offers out (a decision about how the game plays, [DESIGN.md D8](DESIGN.md#d8-fix-bugs-at-their-cause-in-the-games-code)), so a lower-division team can get fewer than three offers, or none.

**How many offers a lower-division team sees:** while the team has no TV contract, every cycle the offers are thrown away (or the one the player chose is accepted, `0x4AAE30`) and new ones are made. `0x4A0F10` also deletes a new offer that matches one already in the list in every amount and in duration (`0x493370`; the TV channel doesn't count). For Catania (level 9) there are only 10 different offers with money: +40 per match with or without +5 for each extra home match, times 5 durations. So a week shows 3 offers about 29% of the time, 2 about 40%, 1 about 27% and none about 4%. Several single-offer weeks in a row are uncommon but expected. The original game gave the same choice; it only listed the empty offers too. Re-rolling empty offers to always show three was considered and rejected: it would give lower divisions more choice than the original did.

**The offers:** the team state (the team record at `+0x11C` points to it) keeps the current TV contract at `+0x5FA0` and the list of offers at `+0x5FA8`, linked through `+0x20`. Each offer is `0x24` bytes; its amounts are in millions of pesetas:

| Offset | Content |
|---|---|
| `+0x00` | For all the rights ("%s por todos los derechos.", global at VA `0xBCE7B8`; 0 means none) |
| `+0x04` | A second amount, with its own text (global at VA `0xBCEF40`) |
| `+0x08` | Per match of a competition |
| `+0x20` | Next offer |

| VA | What it does |
|---|---|
| `0x49CAE7` | Makes the team's new offers (6 for levels 0 to 3, 3 for the others), one by one, and adds each to the list with `0x4A0F10` |
| `0x4A0F10` | Adds an offer to the list, or deletes it if the list already has an identical one |
| `0x492710` | The offer's constructor: calculates the amounts, from the team's level (`team+0xEC`) and random numbers (`0x4C7AD0(n)`, 0 to n−1) |
| `0x492CE0` | Writes the offer's text, each amount multiplied by 1,000,000 (the double at VA `0xA6D990`) |
| `0x4AA4C8` and on | Reads the offers from a saved game |
| `0x4AAE30` | Accepts an offer: it becomes the current contract, and the "all rights" amount goes to the club's income |

**Cause:** the constructor sets the "all rights" amount for levels 0 to 6 (5,000, 3,625, 2,500, 1,625, 1,000, 625 and 500 million, then reduced at random). For level 7 or higher, the lowest divisions, it sets the other amounts (per match: 130 − 10 × level, so 40 for Catania, level 9) but **never the "all rights" amount**. Then, like for every level, it takes 10% off that amount a random number of times and scales it by a random percentage. The offer's memory comes straight from `new`, so the amount is whatever that memory held: 0 when it was clean, which shows no amount, as most lower-division offers do; an absurd figure when it held old data. The game already did this in 2001, on the Windows of its time. An offer saved with an absurd amount keeps it in the saved game.

The memory layer's diagnostic fill of new memory (`fill_new`) was once thought to rule this out, because the amounts didn't look like the fill byte. They wouldn't: the code scales the value before showing it, and the 32-bit multiplication overflows.

**Fix:** the constructor sets the "all rights" amount to 0 for level 7 or higher, so lower-division offers never have one, as every correct offer already shows. The original instructions at VA `0x4928FD` have no room for it, so they jump to new code in the padding after the function that ends at VA `0x491F50` (VA `0x491F51`, 15 bytes of `nop` in v3.6), which repeats them, sets the amount, and jumps back. Offers already in a saved game keep their amounts.

**Empty offers left out:** where the generation adds each new offer (VA `0x49CB6E`), it calls new code instead: if the offer has neither an "all rights" amount nor money per match, it's deleted instead of added. The check (12 bytes) and the delete (15 bytes) don't fit in one padding run, so they use two a short jump apart: VA `0x49FA22` and `0x49FA61`. Bytes for both changes in [PATCHER.md](PATCHER.md#the-changes-to-managerexe).

### The last board sold by *Rellena* shows as unsold

**Symptom:** in *Finanzas → Estadio → Vallas*, *Rellena* (fill) and switching *Auto* on sell the advertising boards, but one section (the last one filled, *Oeste*, at the top of the drawing) shows one offer still on offer and its boards free. Clicking *Rellena* again then shows it sold.

**What really happens:** the sale is made. Traced with the debugger on a *Rellena*: section 3 had room for 10 boards, an offer for 6 and one for 4 were sold, and the second one's "sold" byte (`+0x18`) was 1 in memory while the screen showed it unsold. Only the screen is wrong; the second click sells nothing, it just draws the section again.

**The boards:** each offer of the team's board-offers object (the team state's `+0x6018`) has its section at `+0x0C` (0 to 3; 6, 10, 6 and 10 boards), its size in boards at `+0x10`, and whether it's sold at `+0x18`.

| VA | What it does |
|---|---|
| `0x4C8290` | The n-th unsold offer of a section, counting from 1 |
| `0x4C80C0` | Free boards in a section: its size minus the sizes of its sold offers (`0x4C7EE0`) |
| `0x4C8590` | Marks an offer sold (1) or not (0) |
| `0x4E56A0` | *Rellena*: for each section, collects its unsold offers, sorts them by value per board (the groundskeeper's quality picks the criterion, as in [Auto](#crash-with-the-advertising-boards-on-auto-and-no-groundskeeper)), and calls `0x4E5440` |
| `0x4E5440` | Sells the offers that fit, in that order, highlighting each one in the on-screen list |
| `0x4E59C0` | Switching *Auto* on: also runs *Rellena* |
| `0x4E5BB0` | The section buttons (*Sur*, *Norte*, *Este*, *Oeste*): shows a section, with `0x4E5110` (fill the list) and a redraw |

**Cause:** *Rellena* draws each section again before it looks for each offer to sell, and before it starts the next section, but never after a sale. So the last offer sold in a section shows up as sold only when the next section starts. For the last section there is no next one, and the screen keeps showing its last sale as unsold.

**Fix:** after the loop over the sections, *Rellena* shows the current section again, as the loop does before each section: `0x4E5110(section, 0)` and the redraw. The new code (about 40 bytes) goes in three padding runs linked by short jumps, VA `0x4CCFC1`, `0x4CD001` and `0x4CD041`, from VA `0x4E593B`. Bytes in [PATCHER.md](PATCHER.md#the-changes-to-managerexe). The automatic sale when the days advance (`0x4ABA30`) doesn't draw anything, and the screen shows the right state when it's opened.

### Two services works at once

**Symptom:** in *Finanzas → Estadio → Remodelar → Servicios*, ordering *Tienda del club* (for example *Cinco*, 5 weeks) and then, the same week, a *Cafeterías* work: the Tienda takes the Cafeterías' duration, and both finish together. In *Obras* the Tienda's weeks change to the Cafeterías'. In the other order, the second work can't be ordered: its options are disabled.

**The services works:** the game builds one services work at a time. The stadium object keeps two kinds:

| Field | Content |
|---|---|
| `+0x454` | The pending *Enfermería*, *Tienda del club* or *Lavabos-W.C.* work, as `0x1FF + row × 4 + option`; −1 when none. Reset to −1 when it finishes (VA `0x4CD7D7`). |
| `+0x464` | The pending *Cafeterías* work, an index into the cafeterías' entries at `+0x1B8` (`0x1C` bytes each: finished at `+0`, weeks left at `+4`, option at `+8`, cost at `+0x18`); −1 when none. |
| `+0x1A8`, `+0x1A4` | The services works' weeks left and weekly cost, shared by both kinds |

| VA | What it does |
|---|---|
| `0x4CD050` | The stadium's update, every week and right after *Aceptar*; for the services it writes `+0x1A8`/`+0x1A4` from whichever work is pending |
| `0x4DDCE0` | Draws the *Remodelar* panels and enables each option |
| `0x4DEE70` | *Aceptar*: asks for confirmation, then, for a cafetería row, `0x4DEF20` sets `+0x464` and the entry (`0x8AAEB0`) |

**Cause:** *Enfermería*, *Tienda* and *Lavabos* enable their options only when no work of either kind is pending (`+0x454` through the panel's argument, and `+0x464`). The *Cafeterías* rows, drawn by their own code, check only `+0x464` (VA `0x4DE536`). So right after a Tienda work is ordered, a Cafeterías work can still be ordered; the weekly update then writes the Cafeterías' weeks and cost over the Tienda's in the shared `+0x1A8`/`+0x1A4`, and when that countdown ends both are done. Found with the debugger: the Cafeterías *Aceptar* found `+0x464` at −1 with the Tienda work already ordered.

**Fix:** the Cafeterías rows check both: at VA `0x4DE536`, instead of reading `+0x464`, a call to new code in the padding at VA `0x4DF421` reads `+0x464` and ANDs it with `+0x454`; the result is −1 only if neither work is pending, and the original `inc`/`jne` that follows does the rest. Bytes in [PATCHER.md](PATCHER.md#the-changes-to-managerexe). The game keeps building one services work at a time, as designed.

### Absurd stadium expenses after a cafeteria is finished

**Symptom:** found with the strict test settings ([TOOLS.md](TOOLS.md#windows-page-heap), `fill_new=55`): two weeks after a cafeteria was finished, *Caja* showed *Remodelar estadio* expenses of 7.352.982.962.176 pesetas in each of weeks 46 and 47, and the club was left at −14,705,135,890,053 pesetas. Without the test settings the same bug costs nothing visible, or, with other leftovers in memory, a random amount.

**Cause:** when a cafeteria work finishes, the weekly update (VA `0x4CD91E`) marks its entry finished and sets the current cafeteria work, the stadium's `+0x464`, to −1 ([Two services works at once](#two-services-works-at-once)). It then goes on to the code that recomputes the services' weekly cost from the current work's entry (VA `0x4CD959`), which reads `+0x464` again: −1. "Entry −1" is the 0x1C bytes before the entries at `+0x1B8`, so its weeks are the stadium's `+0x1A0` and its cost is `+0x1B4`, two fields with other uses. The services' weeks (`+0x1A8`) became 2 and their weekly cost (`+0x1A4`) the float at `+0x1B4` halved. `+0x1B4` holds two bytes the game sets (`FE FF`) and two it never does; in the original game those are usually 0, which makes a float of almost nothing, and with `fill_new=55` they were `55 55`, which makes about 14,700,000,000,000. Found in a dump of the running game: `+0x1A4` held `0x54D5FFFE`, which is `0x5555FFFE` (the dword at `+0x1B4`) divided by 2.

**The groundskeeper's weekly "works finished":** every week the team pays each kind of stadium work its weekly cost while its weeks counter isn't 0, and when the counter is exactly 1 the groundskeeper reports the works finished (VA `0x4A6353`–`0x4A64A4`; message `0x3F1`, or `0x3F2` for some services). The stadium update decides the counter. The code that recomputes the services' weekly cost has a branch for a work with 0 weeks left (VA `0x4CD996`) that sets the cost to 0 but the weeks to 1, and never moves it again; the "entry −1" read above lands there whenever `+0x1A0` is 0. From then on the club pays 0 every week and the groundskeeper reports finished works every week: the bug players remember. It persists in saved games.

**Fix:** the block that finishes the work ends by setting the services' weekly cost and weeks to 0 (VA `0x4CD997`, the 0-weeks branch without its increment), as the other services do when theirs finish, instead of recomputing from the current work. The block is rewritten more compactly in place to make room (`inc byte ptr [esi+0Dh]` for three instructions, `or dword ptr [esi+464h],-1` for a longer `mov`), with its two calls aimed at the same functions. Bytes in [PATCHER.md](PATCHER.md#the-changes-to-managerexe). A saved game whose services counter is already stuck at 1 keeps the weekly message until its next services work.

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
