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

Nothing else changes. The patcher also writes `pcf2001-patch\gfxwin.dll.original` (the backup) and `pcf2001-patch\pcf2001-patch.log` in the game folder.

### The change to `gfxwin.dll`

`gfxwin.dll` holds the default graphics backend as an index into its list of backend names, `dcsf.dll`, `dcsfx.dll`, `dcd3d.dll`, `dcogl.dll` ([GAME-INTERNALS.md](GAME-INTERNALS.md#how-the-game-chooses-a-graphics-backend)). The patch changes it from 1 (`dcsfx.dll`, which fails on modern Windows) to 3 (`dcogl.dll`, the new component), so every way of starting the game uses Direct3D 11, including Dinamic's launcher `pcf2001.exe` with its CD check and intro video.

| | |
|---|---|
| Location | File offset `0xFE960` (RVA `0xFE960`, `.data` section), a 32-bit value |
| Original bytes | `01 00 00 00` |
| New bytes | `03 00 00 00` |
| SHA-256 before | `d11c89b34f3d5aef1db32ef38b2259d6b0633ec2aa49fed66e582a00fa12a4c9` (v3.6) |
| SHA-256 after | `bfa9fef56641836c641aac9764b747c9d5fc10568d165d2e882a67ee96ec1cf5` |

Starting `manager.exe dcogl.dll` does the same for one session, without changing the file.

## Applying

1. **Check the game:** `manager.exe`, `gfxwin.dll`, `dcd3d.dll`, `dcsfx.dll` and `DCSF.dll` must have the v3.6 SHA-256 values ([DESIGN.md D1](DESIGN.md#d1-target-dinamics-v36-only)), or `gfxwin.dll` the patched one. The new component and its memory layer were made for exactly these files, so with any other version the patcher does nothing. It also refuses while the game is running.
2. **Write `dcogl.dll`.**
3. **Back up `gfxwin.dll`** to `pcf2001-patch\gfxwin.dll.original`, change the byte, and check the result's SHA-256 before writing it.
4. **Check the game again:** the state must now be "applied".

Every file is written next to its destination and then moved over it, so a failure never leaves a half-written file. Applying over an applied patch only replaces `dcogl.dll`, which is how a newer patch version updates an older one.

## Removing

1. **Restore `gfxwin.dll`** from the backup, after checking that the backup has the v3.6 SHA-256. If the backup is missing or damaged, the patcher undoes the one-byte change in place instead, and checks the result just the same.
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
