# The mods tool

`pcf2001-mods.exe` installs and removes optional mods: changes to how the game plays, which the patch itself never makes ([DESIGN.md D16](DESIGN.md#d16-mods-a-separate-tool-each-one-optional)). Each mod is installed or removed on its own.

## Using it

The window lists the mods with a check box each. Check the ones you want, uncheck the ones to remove, and click *Aplicar los cambios*. The patch must be applied first, with the `pcf2001-patch.exe` of the same version, and the game must be closed.

From the command line, without asking anything (exit code 0 on success):

```
pcf2001-mods.exe /instalar <mod>... [<carpeta>]
pcf2001-mods.exe /quitar <mod>... [<carpeta>]
```

Mods are named by their id (below); the others stay as they are. Without a folder, the game is found through the registry value its installer wrote.

## The mods

| Id | Name | What it does |
|---|---|---|
| `juveniles-de-cualquier-pais` | *Jóvenes promesas de cualquier país* | The youth search finds youth players from any country, not only the club's, and ignores its competition's rule on foreign players |
| `extranjeros-sin-limite-en-plantilla` | *Sin límite de extranjeros en la plantilla* | Any number of foreign players can be signed, also in the divisions that allow none, by every club |
| `extranjeros-sin-limite-en-alineacion` | *Sin límite de extranjeros en las alineaciones* | Any number of foreign players can be in a line-up, for every club, automatic line-ups included |

### `juveniles-de-cualquier-pais`

The youth search draws from one hidden team, *Juveniles Españoles*, and keeps only the players of the club's own country who its competition allows ([GAME-INTERNALS.md](GAME-INTERNALS.md#the-youth-search)). With the mod, both conditions are skipped, as the game already does for a search made for no team. A youth player must still not be in the club already, be 18 or younger, and fit the positions searched.

| File | Offset | Before (as the patch leaves it) | After |
|---|---|---|---|
| `manager.exe` | `0x8DA2B` | `0F 84 A6 00 00 00` (`je 0x48DAD7`) | `E9 A7 00 00 00 90` (`jmp 0x48DAD7`) |

Tested with Boca Juniors (Argentina): the search found Spanish youth players, which the original game never offers an Argentinian club.

### `extranjeros-sin-limite-en-plantilla`

Every check of the squad's foreign players compares the club's count with its competition's maximum ([GAME-INTERNALS.md](GAME-INTERNALS.md#foreign-players)). The mod makes the count always 0, and in the three competitions whose maximum is 0 it points the maximum at the one the competitions without a limit use. It also removes *"No puedes ofrecer jugadores extranjeros a este equipo"*. It applies to every club: the computer's clubs' transfers go through the same checks.

| File | Offset | Before | After |
|---|---|---|---|
| `manager.exe` | `0x9A7E0` | `51 64 8B` (start of the count, `0x49A7E0`) | `33 C0 C3` (`xor eax,eax; ret`) |
| | `0x67A728` | `30 CE 40 00` (`0x40CE30`, maximum 0) | `80 FD 53 00` (`0x53FD80`, maximum 65535) |
| | `0x67CC6C` | `30 CE 40 00` | `80 FD 53 00` |
| | `0x67CF84` | `30 CE 40 00` | `80 FD 53 00` |
| | `0x1064D8` | `74 20` (`je 0x5064FA`) | `EB 20` (`jmp 0x5064FA`) |

The three offsets in `.rdata` are the maximum's entry (`+0x8C`) in the vtables at VA `0xA7A69C`, `0xA7CBE0` and `0xA7CEF8`. The functions themselves can't be changed: the linker merged identical functions, so the code that returns 0 also serves unrelated methods.

### `extranjeros-sin-limite-en-alineacion`

The six places that read the competition's line-up maximum get 65535 instead ([GAME-INTERNALS.md](GAME-INTERNALS.md#foreign-players)): each 6-byte call becomes a 6-byte `mov eax,0FFFFh; nop`. That covers confirming a line-up, swapping two players, and the automatic line-ups of every club.

| File | Offset | Before | After |
|---|---|---|---|
| `manager.exe` | `0x1F4F8`, `0x93470`, `0x944FC`, `0x569068` | `FF 92 94 00 00 00` (`call dword ptr [edx+94h]`) | `B8 FF FF 00 00 90` (`mov eax,0FFFFh; nop`) |
| | `0x93CBF`, `0x95D49` | `FF 90 94 00 00 00` (`call dword ptr [eax+94h]`) | `B8 FF FF 00 00 90` |

Both tested with a club with both limits: it signed foreign players past its limit, and confirmed a line-up with more foreign players than allowed.

## How it works

- **Each mod is a list of byte changes** in `src/patcher/mods.cpp`, with the bytes before and after. "Before" is the file as the patch leaves it: mods go on top of the patch, never on the v3.6 files.
- **Checking the game:** for each file the mods change, the tool undoes the mods it finds installed, in memory, and checks that the result is the file as this version of the patch leaves it (its SHA-256 in `src/patcher/patch.cpp`). Only then does it install or remove anything. So any combination of mods is checked against one known hash, and a file with changes from anywhere else is never touched.
- **Changing the mods:** each file is rebuilt from that result with the wanted mods, checked again, and written next to the original and moved over it, as the patcher does. Mods that change the same bytes can't be installed together.
- **No backups:** removing a mod writes its "before" bytes back. The patch's backups, in `pcf2001-patch\`, stay the one copy of the v3.6 files.
- **The log** is `pcf2001-patch\pcf2001-mods.log`, next to the patch's.

### With the patcher

The patcher shares the mods' code (`patch_core`), so it recognises a modded file as patched:

- **Applying the patch again** keeps the mods.
- **Removing the patch** restores the v3.6 files from its backups, so the mods go too. If a backup is missing or damaged, the patcher undoes the mods and then its own changes in place.
- **Updating to a newer version of the patch** changes the files the mods go on, so the old version's mods tool must remove them first. The patcher says so when it finds a file it doesn't know.

## Adding a mod

1. Find the change, and test it in the running game (for example with `cdb`, [TOOLS.md](TOOLS.md)) before writing it down.
2. Add it to `kMods` in `src/patcher/mods.cpp`: an id (lowercase, hyphens), its name and description for the player in Spanish, the file, and the byte changes, with comments as in `patch.cpp`. Its "before" bytes are the file as the patch leaves it.
3. Document it here: what it does, the bytes, and how it was tested; and in [GAME-INTERNALS.md](GAME-INTERNALS.md), the game's code it changes.
