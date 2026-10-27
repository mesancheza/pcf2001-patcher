# The patcher

`pcf2001-patch.exe` applies the fixes for modern Windows to an installed PC FÚTBOL 2001 v3.6, and removes them again. It's a separate program from the game installer ([INSTALLER.md](INSTALLER.md)): the installer puts the game on disk as Dinamic released it, the patcher fixes it ([DESIGN.md D10](DESIGN.md#d10-the-patcher)).

## Using it

Run `pcf2001-patch.exe`. It finds the game by itself, through the registry value the installer writes; *Cambiar...* chooses another folder. The window shows the game's state:

| State | Meaning | Buttons |
|---|---|---|
| *sin el parche* | v3.6, unpatched | Aplicar |
| *con el parche X aplicado* | v3.6 with this version of the patch | Quitar |
| *con otra versión del parche* | v3.6 with an older or newer version, or a half-applied patch | Actualizar, Quitar |
| *no es el de la versión 3.6 original* | Not v3.6, or modified files: the patcher does nothing | — |

It needs no administrator rights: it only writes into the game folder, which the installer made writable for every user. The game must be closed.

Command-line modes, for testing and for advanced users:

```
pcf2001-patch.exe /aplicar [<carpeta del juego>]
pcf2001-patch.exe /quitar [<carpeta del juego>]
```

They do the same without any window, and return exit code 0 on success.

## What it changes

| File | Change |
|---|---|
| `dcogl.dll` | Added: the new graphics component (Direct3D 11), with the memory compatibility layer and the log ([DESIGN.md D6, D13, D14](DESIGN.md#d6-replace-the-graphics-component-through-the-games-own-plug-in-interface-using-direct3d-11)). The patcher carries it inside itself, so players download one program. |
| `gfxwin.dll` | The default graphics backend, and a fix in its shutdown code. Details below. |
| `manager.exe` | Fixes in the game's code. Details below. |

Nothing else changes. The patcher also writes the backups `pcf2001-patch\gfxwin.dll.original` and `pcf2001-patch\manager.exe.original`, and `pcf2001-patch\pcf2001-patch.log`, in the game folder.

### The changes to `gfxwin.dll`

**The default graphics backend.** 
`gfxwin.dll` holds the default graphics backend as an index into its list of backend names, `dcsf.dll`, `dcsfx.dll`, `dcd3d.dll`, `dcogl.dll` ([GAME-INTERNALS.md](GAME-INTERNALS.md#how-the-game-chooses-a-graphics-backend)). The patch changes it from 1 (`dcsfx.dll`, which fails on modern Windows) to 3 (`dcogl.dll`, the new component), so every way of starting the game uses Direct3D 11, including Dinamic's launcher `pcf2001.exe` with its CD check and intro video.

| | |
|---|---|
| Location | File offset `0xFE960` (RVA `0xFE960`, `.data` section), a 32-bit value |
| Original bytes | `01 00 00 00` |
| New bytes | `03 00 00 00` |
| SHA-256 before | `d11c89b34f3d5aef1db32ef38b2259d6b0633ec2aa49fed66e582a00fa12a4c9` (v3.6) |

Starting `manager.exe dcogl.dll` does the same for one session, without changing the file (but without the fixes to `manager.exe` and `gfxwin.dll`).

**[The freed API object read at exit](GAME-INTERNALS.md#found-the-main-window-checks-a-freed-api-object-at-exit).** Where `CGFXMainWindow::End` clears its list of API objects, a call to 13 bytes of new code in the padding after `~CGFXMainWindow` clears the list and `m_pApiActual` too. Both new instructions hold absolute addresses, so two entries of the relocation table change with them: the one for the replaced instruction now points at the first, and the page block's padding entry at the second. Offsets are file offsets; in `gfxwin.dll` the VA is the offset plus `0x10000000`.

| Offset | Original bytes | New bytes |
|---|---|---|
| `0x591C0` | `89 1D 0C 11 11 10` | `E8 8C 02 00 00 90` |
| `0x59451` | `90` × 13 | `89 1D 0C 11 11 10 89 1D 30 11 11 10 C3` |
| `0x106C44` (relocations) | `C2 31` | `53 34` |
| `0x106D66` (relocations) | `00 00` | `59 34` |

SHA-256 after both changes: `5f1dcc3cc690ac864f53971efb980c19d355c0c7a13bd2bbd59779cc4b1fbc41`.

### The changes to `manager.exe`

Each fixes a bug in the game's code at its cause ([DESIGN.md D8](DESIGN.md#d8-fix-bugs-at-their-cause-in-the-games-code)), except the last, which removes an animation that flickers. Offsets are file offsets; in `manager.exe` the VA is the offset plus `0x400000`.

| Fix | Offset | Original bytes | New bytes |
|---|---|---|---|
| [Crash with the advertising boards on Auto and no groundskeeper](GAME-INTERNALS.md#crash-with-the-advertising-boards-on-auto-and-no-groundskeeper) | `0xABA8C` | `33 C9 8A 48 01 89 4C 24 20` | `E9 D0 FB FF FF 89 44 24 20` |
| | `0xAB661` | `90` × 13 | `85 C0 74 04 0F B6 40 01 E9 23 04 00 00` |
| [Absurd TV offers in lower divisions](GAME-INTERNALS.md#absurd-tv-offers) | `0x928FD` | `89 6E 18 C6 46 1C 00` | `E9 4F F6 FF FF 90 90` |
| | `0x91F51` | `90` × 14 | `89 6E 18 C6 46 1C 00 89 2E E9 A5 09 00 00` |
| [Empty TV offers left out](GAME-INTERNALS.md#absurd-tv-offers) | `0x9CB6E` | `8B CE E8 9B 43 00 00` | `E8 AF 2E 00 00 90 90` |
| | `0x9FA22` | `90` × 12 | `0B 51 08 8B CE 74 38 E9 E2 14 00 00` |
| | `0x9FA61` | `90` × 15 | `FF 74 24 04 E8 A0 AA 53 00 83 C4 04 C2 04 00` |
| [The last board sold by *Rellena* shows as unsold](GAME-INTERNALS.md#the-last-board-sold-by-rellena-shows-as-unsold) | `0xE593B` | `3B C3 C6 44 24 68 03` | `E9 81 76 FE FF 90 90` |
| | `0xCCFC1` | `90` × 15 | `C6 44 24 68 03 6A 00 FF B5 DC 21 00 00 EB 31` |
| | `0xCD001` | `90` × 13 | `8B CD E8 08 81 01 00 6A 01 8B CD EB 33` |
| | `0xCD041` | `90` × 13 | `FF 15 60 A0 A6 00 33 C0 E9 F4 88 01 00` |
| [Two services works at once](GAME-INTERNALS.md#two-services-works-at-once) | `0xDE536` | `8B 88 64 04 00 00` | `E8 E6 0E 00 00 90` |
| | `0xDF421` | `90` × 13 | `8B 88 64 04 00 00 23 88 54 04 00 00 C3` |
| [Absurd stadium expenses after a cafeteria is finished](GAME-INTERNALS.md#absurd-stadium-expenses-after-a-cafeteria-is-finished) | `0xCD91E` | `8B 86 64 04 00 00 6A 03 8D 0C C5 00 00 00 00 2B C8 C6 84 8E B8 01 00 00 01 8A 56 0D FE C2 8B CB 88 56 0D C7 86 64 04 00 00 FF FF FF FF E8 50 50 FF FF 6A 02 8B CB E8 77 50 FF FF` | `8B 86 64 04 00 00 6A 03 8D 0C C5 00 00 00 00 2B C8 C6 84 8E B8 01 00 00 01 FE 46 0D 8B CB 83 8E 64 04 00 00 FF E8 58 50 FF FF 6A 02 8B CB E8 7F 50 FF FF 33 C0 EB 42 90 90 90 90` |
| [Rows with equal values swap when a list is scrolled](GAME-INTERNALS.md#rows-with-equal-values-swap-when-a-list-is-scrolled) | `0x4BE783` | `74 3D` | `90 90` |
| | `0x4BE8C4` | `74 3D` | `90 90` |
| | `0x4BE9EF` | `74 3C` | `90 90` |
| [Ageing undone at managed clubs](GAME-INTERNALS.md#ageing-undone-at-managed-clubs) | `0xAF549` | `8A 86 F5 00 00 00 3C 23 7C 08 2C 05 88 86 F5 00 00 00 8A 86 F6 00 00 00 3C 23 7C 08 2C 05 88 86 F6 00 00 00 8A 86 F7 00 00 00 3C 21 7C 08 2C 03 88 86 F7 00 00 00 8A 86 FD 00 00 00 3C 21 0F 8C F9 00 00 00 2C 03 88 86 FD 00 00 00 E9 EC 00 00 00` | `8D 8E F5 00 00 00 66 BA 23 05 E8 03 01 00 00 41 E8 FD 00 00 00 41 66 BA 21 03 E8 F3 00 00 00 8D 8E FD 00 00 00 E8 E8 00 00 00 E9 0E 01 00 00` + `90` × 34 |
| | `0xAF62C` | `8B 44 24 14 25 FF 00 00 00 03 F8 83 FF 63 7D 08 02 D1 88 96 F5 00 00 00 8D 14 28 83 FA 63 7D 08 02 D9 88 9E F6 00 00 00 8B 54 24 20 03 D0 83 FA 63 7D 0C 8A 54 24 12 02 D1 88 96 F7 00 00 00 8B 54 24 1C 03 C2 83 F8 63 7D 0C 8A 44 24 13 02 C1 88 86 F8 00 00 00` | `8A D1 8D 8E F5 00 00 00 E8 14 00 00 00 41 E8 0E 00 00 00 41 E8 08 00 00 00 41 E8 02 00 00 00 EB 35 8A 01 02 C2 3C 63 7D 05 88 01 00 51 0D C3 8A 01 3A C2 7C 05 28 31 28 71 0D C3` + `90` × 27 |
| [Crash choosing shirt numbers](GAME-INTERNALS.md#crash-choosing-shirt-numbers) | `0x32B9A` | `8D 0C 40 C1 E1 05 03 C8 8D 8C 8E 98 23 00 00 FF D3` | `69 C8 84 01 00 00 03 CE E8 DA F3 FF FF 90 90 90 90` |
| | `0x32C1A` | `8D 14 40 C1 E2 05 03 D0 8D 8C 96 98 23 00 00 FF D3` | `69 C8 84 01 00 00 03 CE E8 5A F3 FF FF 90 90 90 90` |
| | `0x32C63` | `8D 0C 40 C1 E1 05 03 C8 8D 8C 8E 98 23 00 00 FF 15 60 A0 A6 00` | `69 C8 84 01 00 00 03 CE E8 11 F3 FF FF` + `90` × 8 |
| | `0x31F81` | `90` × 15 | `48 83 F8 32 73 15 81 C1 98 23 00 00 EB 07 90` |
| | `0x31F96` | `90` × 10 | `FF 25 60 A0 A6 00 C2 04 00 90` |
| [The player menu's animation](GAME-INTERNALS.md#the-player-menus-animation) (not a bug fix) | `0x55664C` | `75 76` | `EB 76` |
| | `0x554196` | `76 5C` | `90 90` |
| | `0x5542E6` | `72 34` | `90 90` |

In each fix, the first change jumps to new code in the padding between two functions, which jumps back. For the groundskeeper: quality 0 when there's none, his quality as before when there is one. For the TV offers: the two instructions the jump replaced, then the "all rights" amount set to 0. For the empty offers: if the offer has no money, the next padding run deletes it; otherwise it's added to the list as before. For *Rellena*: the instruction the jump replaced, then the current section shown again (its list filled and redrawn), across three padding runs linked by short jumps. For the services works: a call instead of a jump, to code that reads both pending-work fields and returns. For the stadium expenses: no new code elsewhere; the block is rewritten in place, shorter, and ends with a jump. For the lists: no new code either; one conditional jump in each of the sort's three loops becomes two `nop`s. For the ageing: both blocks are rewritten in place, shorter, as calls to two helpers placed in the space this frees (`0x4AF64D` for a gain, `0x4AF65B` for a decline), which change the rating as before and its base by the same amount. For the shirt numbers: each of the three places computes the button's address more compactly and calls a helper, across two padding runs linked by a short jump, that redraws the button only for numbers 1 to 50. For the player menu: a conditional jump becomes unconditional so the menu is laid out complete, and the two jumps that schedule each animation's next step become `nop`s.

| | |
|---|---|
| SHA-256 before | `bcbb057e80e370dfd1647c7124cac80cd8ac4bdcdc51abb36caa43a828dd63bc` (v3.6) |
| SHA-256 after | `70755417cdd1e6469f918a96e7fa5927e00dee3a4f496355599b1990f99412b6` |

## Applying

1. **Check the game:** `manager.exe`, `gfxwin.dll`, `dcd3d.dll`, `dcsfx.dll` and `DCSF.dll` must have the v3.6 SHA-256 values ([DESIGN.md D1](DESIGN.md#d1-target-dinamics-v36-only)), or `gfxwin.dll` and `manager.exe` the patched ones, with or without mods ([MODS.md](MODS.md#with-the-patcher)). The new component and its memory layer were made for exactly these files, so with any other version the patcher does nothing. It also refuses while the game is running.
2. **Write `dcogl.dll`.**
3. **Change `gfxwin.dll` and `manager.exe`,** each one that is still original: back it up to `pcf2001-patch\<file>.original`, check that every location holds the original bytes, change them, and check the result's SHA-256 before writing it.
4. **Check the game again:** the state must now be "applied".

Every file is written next to its destination and then moved over it, so a failure never leaves a half-written file. Applying over an applied patch replaces `dcogl.dll` and changes the files a previous version didn't, which is how a newer patch version updates an older one.

## Removing

1. **Restore `gfxwin.dll` and `manager.exe`,** each one that was changed, from its backup, after checking that the backup has the v3.6 SHA-256. If a backup is missing or damaged, the patcher undoes the changes in place instead, mods first, after checking that every location holds the new bytes, and checks the result just the same.
2. **Delete** `dcogl.dll`, `dcogl.ini` (if the player created one), the logs `dcogl.log` and `dcogl.old.log`, and the `pcf2001-patch` folder.
3. **Check the game again:** the state must now be "unpatched".

If the game is uninstalled while patched, the installer's uninstaller removes the patch's files too.

## Source code

`src/patcher/`:

| File | What it does |
|---|---|
| `main.cpp` | The window and the command line |
| `patch.cpp` | Checking the game's state, applying and removing the patch |
| `mods.cpp` | The mods and their code ([MODS.md](MODS.md)), so the patcher recognises them |
| `patcher.rc` | The window's layout, version information, and `dcogl.dll` embedded as a resource (built from `src/backend`) |

`patch.cpp` and `mods.cpp` make the library `patch_core`, which the mods tool (`src/mods`) uses too. It reuses the installer's code for hashes, the log, and finding the game through the registry.
