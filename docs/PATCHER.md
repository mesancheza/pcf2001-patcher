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
| `gfxwin.dll` | One byte: the default graphics backend. Details below. |
| `manager.exe` | Fixes in the game's code. Details below. |

Nothing else changes. The patcher also writes the backups `pcf2001-patch\gfxwin.dll.original` and `pcf2001-patch\manager.exe.original`, and `pcf2001-patch\pcf2001-patch.log`, in the game folder.

### The change to `gfxwin.dll`

`gfxwin.dll` holds the default graphics backend as an index into its list of backend names, `dcsf.dll`, `dcsfx.dll`, `dcd3d.dll`, `dcogl.dll` ([GAME-INTERNALS.md](GAME-INTERNALS.md#how-the-game-chooses-a-graphics-backend)). The patch changes it from 1 (`dcsfx.dll`, which fails on modern Windows) to 3 (`dcogl.dll`, the new component), so every way of starting the game uses Direct3D 11, including Dinamic's launcher `pcf2001.exe` with its CD check and intro video.

| | |
|---|---|
| Location | File offset `0xFE960` (RVA `0xFE960`, `.data` section), a 32-bit value |
| Original bytes | `01 00 00 00` |
| New bytes | `03 00 00 00` |
| SHA-256 before | `d11c89b34f3d5aef1db32ef38b2259d6b0633ec2aa49fed66e582a00fa12a4c9` (v3.6) |
| SHA-256 after | `bfa9fef56641836c641aac9764b747c9d5fc10568d165d2e882a67ee96ec1cf5` |

Starting `manager.exe dcogl.dll` does the same for one session, without changing the file (but without the fixes to `manager.exe`).

### The changes to `manager.exe`

Each fixes a bug in the game's code at its cause ([DESIGN.md D8](DESIGN.md#d8-fix-bugs-at-their-cause-in-the-games-code)). Offsets are file offsets; in `manager.exe` the VA is the offset plus `0x400000`.

| Fix | Offset | Original bytes | New bytes |
|---|---|---|---|
| [Crash with the advertising boards on Auto and no groundskeeper](GAME-INTERNALS.md#crash-with-the-advertising-boards-on-auto-and-no-groundskeeper) | `0xABA8C` | `33 C9 8A 48 01 89 4C 24 20` | `E9 D0 FB FF FF 89 44 24 20` |
| | `0xAB661` | `90` × 13 | `85 C0 74 04 0F B6 40 01 E9 23 04 00 00` |

The first change jumps to the second, new code in the padding between two functions: quality 0 when there's no groundskeeper, his quality as before when there is one, and back.

| | |
|---|---|
| SHA-256 before | `bcbb057e80e370dfd1647c7124cac80cd8ac4bdcdc51abb36caa43a828dd63bc` (v3.6) |
| SHA-256 after | `750c45dae266abea0656cbf6f7f6b4a0f41f1e39c42d4caa6891c44b30e15551` |

## Applying

1. **Check the game:** `manager.exe`, `gfxwin.dll`, `dcd3d.dll`, `dcsfx.dll` and `DCSF.dll` must have the v3.6 SHA-256 values ([DESIGN.md D1](DESIGN.md#d1-target-dinamics-v36-only)), or `gfxwin.dll` and `manager.exe` the patched ones. The new component and its memory layer were made for exactly these files, so with any other version the patcher does nothing. It also refuses while the game is running.
2. **Write `dcogl.dll`.**
3. **Change `gfxwin.dll` and `manager.exe`,** each one that is still original: back it up to `pcf2001-patch\<file>.original`, check that every location holds the original bytes, change them, and check the result's SHA-256 before writing it.
4. **Check the game again:** the state must now be "applied".

Every file is written next to its destination and then moved over it, so a failure never leaves a half-written file. Applying over an applied patch replaces `dcogl.dll` and changes the files a previous version didn't, which is how a newer patch version updates an older one.

## Removing

1. **Restore `gfxwin.dll` and `manager.exe`,** each one that was changed, from its backup, after checking that the backup has the v3.6 SHA-256. If a backup is missing or damaged, the patcher undoes the changes in place instead, after checking that every location holds the new bytes, and checks the result just the same.
2. **Delete** `dcogl.dll`, `dcogl.ini` (if the player created one), the logs `dcogl.log` and `dcogl.old.log`, and the `pcf2001-patch` folder.
3. **Check the game again:** the state must now be "unpatched".

If the game is uninstalled while patched, the installer's uninstaller removes the patch's files too.

## Source code

`src/patcher/`:

| File | What it does |
|---|---|
| `main.cpp` | The window and the command line |
| `patch.cpp` | Checking the game's state, applying and removing the patch |
| `patcher.rc` | The window's layout, version information, and `dcogl.dll` embedded as a resource (built from `src/backend`) |

It reuses the installer's code for hashes, the log, and finding the game through the registry.
