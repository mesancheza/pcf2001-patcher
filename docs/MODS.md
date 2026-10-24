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
| `dorsales-en-todas-las-competiciones` | *Dorsales en todas las competiciones* | Shirt numbers can be chosen in every league and division, not only in the four competitions that had them |
| `banderas-de-todos-los-jugadores` | *Banderas de todos los jugadores* | Player lists show every player's flag, not only the foreign players' |
| `pais-de-nacimiento-para-todos` | *País de nacimiento para todos* | Every player born outside his nationality's country is domestic, not foreign, in both countries: his nationality's and his country of birth's, as the nationalised ones already were |
| `bandera-del-pais-de-nacimiento` | *Bandera del país de nacimiento* | Players' flags show their country of birth when the game knows it, instead of their nationality |
| `juveniles-entrenando-sin-limite` | *Juveniles entrenando sin límite* | Any number of youth players can train at once, not only 1 to 4 depending on the youth coach |

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

### `dorsales-en-todas-las-competiciones`

Only four competitions use fixed shirt numbers ([GAME-INTERNALS.md](GAME-INTERNALS.md#shirt-numbers)). In the others, the club menu has no *Dorsales* entry, and every line-up renumbers the players by their place in it. The mod makes the nine places that ask a competition whether it uses fixed shirt numbers get "yes": each 6-byte call becomes `mov al,1` and four `nop`s.

| File | Offset | Before | After |
|---|---|---|---|
| `manager.exe` | `0x18EAE`, `0x19052`, `0x9B8F7` | `FF 90 9C 00 00 00` (`call dword ptr [eax+9Ch]`) | `B0 01 90 90 90 90` (`mov al,1`) |
| | `0x93A2E`, `0x940AC`, `0x9D42A`, `0x9B27F`, `0x9C4B4`, `0xAC848` | `FF 92 9C 00 00 00` (`call dword ptr [edx+9Ch]`) | `B0 01 90 90 90 90` |

**Known limitation:** the database has no shirt numbers for the clubs of the other competitions, so their players start with repeated numbers, mostly 1, until they're chosen on the *Dorsales* screen. The numbers are only shown: the game tells players apart by their records, so repeated numbers have no other effect. Giving those clubs unique numbers automatically was considered and left out: it needs new code, and could overwrite numbers chosen on purpose.

Tested with a club of a competition without shirt numbers: the *Dorsales* entry appeared, numbers could be chosen, and line-ups kept them.

### `banderas-de-todos-los-jugadores`

Player lists draw a player's flag only if he counts as foreign in the current competition ([GAME-INTERNALS.md](GAME-INTERNALS.md#flags-in-player-lists)); otherwise they jump over the block that draws it. In the fifteen places that do so, the jump becomes `nop`s. Each of those blocks only fetches the flag, places it and draws it, so nothing else changes.

| File | Offsets | Before | After |
|---|---|---|---|
| `manager.exe` | `0x2BF1`, `0x36871`, `0x3E112`, `0x12484E`, `0x126955`, `0x3DF10F`, `0x4C9D12`, `0x559599` | `74 xx` (`je`, over the flag) | `90 90` |
| | `0x3331A`, `0x35DD5`, `0x361CC`, `0x5234C`, `0x52795`, `0x11F6EF`, `0x11AF1F` | `0F 84 xx 00 00 00` (`je`, over the flag) | `90 90 90 90 90 90` |

The flag of a nationality with no image would make `0x4C5D30` read through a null pointer, but every nationality's flag was already shown somewhere in the original game: in any competition where it is foreign.

Tested in the squad, *Jóvenes promesas* and other player lists: every player showed his flag, and no screen failed.

**Not covered:** the team selection screen at the start of a game (`tx_pcfwinseleccionpro`) shows a club's squad without any flags, foreign players included: it never draws them, so there is nothing to enable. Traced: clicking clubs there never asks for a flag (`0x4C5D30`).

### `pais-de-nacimiento-para-todos`

The database has each player's nationality and, in full records, his country of birth and a "nationalised" flag. The game keeps the country of birth only for the players marked nationalised, and counts them as from both countries for the foreign-player rules ([GAME-INTERNALS.md](GAME-INTERNALS.md#nationality-and-country-of-birth)); for the others it drops it when it reads the database. The mod keeps it for every player.

**What it does:** every player born outside his nationality's country becomes domestic in both countries. For the foreign-player rules (signings, squads and line-ups, [GAME-INTERNALS.md](GAME-INTERNALS.md#foreign-players)), he doesn't count as foreign in the competitions of his nationality's country, as before, nor in those of his country of birth, which is new. Davids (Netherlands, born in Suriname) is domestic in the Netherlands and in Suriname, and foreign everywhere else, as before. That is how the game already treated the players the database marks as nationalised; the mod treats the others the same way. In the database supplied with the game that changes 324 players, such as Davids and Seedorf; players born in their nationality's country are unaffected. With `bandera-del-pais-de-nacimiento`, their flag also shows their country of birth.

| File | Offset | Before | After |
|---|---|---|---|
| `manager.exe` | `0xAE4D1` | `75 07` (`jne 0x4AE4DA`: keep it only if nationalised) | `EB 07` (`jmp 0x4AE4DA`: always keep it) |

It changes how the game reads the database, so it applies to games started after installing it: a saved game keeps the data it was saved with. Removing it doesn't change games already started with it.

### `bandera-del-pais-de-nacimiento`

Where a player list reads the nationality to draw the flag (the fourteen places of `banderas-de-todos-los-jugadores` that draw a player's flag), it calls a small function instead, which returns the country of birth when the game has one and the nationality otherwise. The game has a country of birth for the players the database marks as nationalised, or for every player born outside his nationality's country with `pais-de-nacimiento-para-todos`.

Each place reads the nationality with `xor reg,reg; mov regL,[player+61h]`, 5 bytes, which become a 5-byte `call`. The places use seven pairs of registers (the player, the result), so there are seven functions, each in a 15-byte padding run between two of the game's functions:

```
movzx reg, byte ptr [player+13Eh]   ; country of birth, 0 if unknown
test  reg, reg
jnz   done
mov   regL, byte ptr [player+61h]   ; the nationality (reg is already 0)
done: ret
```

| File offset (VA) | Registers | Bytes |
|---|---|---|
| `0xB42C1` (`0x4B42C1`) | `edi` → `edx` | `0F B6 97 3E 01 00 00 85 D2 75 03 8A 57 61 C3` |
| `0xB45E1` (`0x4B45E1`) | `edi` → `eax` | `0F B6 87 3E 01 00 00 85 C0 75 03 8A 47 61 C3` |
| `0xB5081` (`0x4B5081`) | `edi` → `ecx` | `0F B6 8F 3E 01 00 00 85 C9 75 03 8A 4F 61 C3` |
| `0xB5C61` (`0x4B5C61`) | `ebp` → `eax` | `0F B6 85 3E 01 00 00 85 C0 75 03 8A 45 61 C3` |
| `0xB6A11` (`0x4B6A11`) | `edx` → `eax` | `0F B6 82 3E 01 00 00 85 C0 75 03 8A 42 61 C3` |
| `0xB6A41` (`0x4B6A41`) | `ebx` → `edx` | `0F B6 93 3E 01 00 00 85 D2 75 03 8A 53 61 C3` |
| `0xB7381` (`0x4B7381`) | `eax` → `edx` | `0F B6 90 3E 01 00 00 85 D2 75 03 8A 50 61 C3` |

Each run was `90` × 15, after a `ret` and before the next function, and nothing in the game refers to it.

| File offset (VA) | Before | After |
|---|---|---|
| `0x2BF7` (`0x402BF7`), `0x3333C`, `0x35DE1`, `0x361D8`, `0x52371`, `0x4C9D18`, `0x55959F` | `33 D2 8A 57 61` | `call 0x4B42C1` |
| `0x3E118` (`0x43E118`), `0x527BA` | `33 C0 8A 47 61` | `call 0x4B45E1` |
| `0x36879` (`0x436879`) | `33 C9 8A 4F 61` | `call 0x4B5081` |
| `0x11F6F9` (`0x51F6F9`) | `33 C0 8A 45 61` | `call 0x4B5C61` |
| `0x124860` (`0x524860`) | `33 C0 8A 42 61` | `call 0x4B6A11` |
| `0x12695D` (`0x52695D`) | `33 D2 8A 53 61` | `call 0x4B6A41` |
| `0x3DF119` (`0x7DF119`) | `33 D2 8A 50 61` | `call 0x4B7381` |

The fifteenth place of the flags mod (`0x51AF17`) draws a nationality from a list, not a player's, and keeps it. On its own, without the other two mods, this one changes only the flags the game already shows: those of foreign players.

Tested in steps, with Inter: with `banderas-de-todos-los-jugadores` only, every player showed his nationality's flag (Zanetti Italy, Davids Netherlands); adding this mod, Zanetti (marked nationalised) showed Argentina and Davids still the Netherlands; adding `pais-de-nacimiento-para-todos` and starting a new game, Davids and Seedorf showed Suriname.

### `juveniles-entrenando-sin-limite`

In *Jóvenes promesas*, the *Entrenando* button turns a youth player's training on only while fewer players are training than the youth coach can take: 1 to 4, depending on his quality ([GAME-INTERNALS.md](GAME-INTERNALS.md#youth-training)). Otherwise it shows *"No puedes entrenar a más jugadores"*. The mod removes that check, the only place the game applies the limit. A youth coach is still needed, firing him still stops everyone's training, and the weekly update still ends each player's training when the game decides.

| File | Offset | Before | After |
|---|---|---|---|
| `manager.exe` | `0x51D48` | `73 23` (`jae 0x451D6D`, the message) | `90 90` |

Tested: with more youth players training than the coach's limit, no message, and after a few weeks all of them had trained.

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
