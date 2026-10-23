# The game installer

`pcf2001-setup.exe` installs PC FÚTBOL 2001 from the player's own CD and applies Dinamic's official updates, giving a ready v3.6 installation with the season's final data. It replaces the CD's own setup, which no longer runs on modern Windows ([GAME-INTERNALS.md](GAME-INTERNALS.md#the-cds-installer)).

It only installs the game as Dinamic released it. The fixes for modern Windows are a separate program, the patcher ([DESIGN.md D10](DESIGN.md#d10-the-patcher)).

The installer contains no Dinamic files: everything it installs comes from the player's CD and from the update files the player supplies.

## What the player needs

- The game CD, or an image of it (`.iso`) mounted as a drive (Windows mounts it with a double-click). A folder with a copy of the CD's contents also works.
- Dinamic's updates, all of them, in one folder and with their original names: `PCFPATCH3.6.EXE` and `PCFACT01.exe` to `PCFACT12.exe`. The installer checks each one against the SHA-256 of the original file and refuses damaged or modified copies.
- Administrator rights, once, for the installation (Windows asks). Playing never needs them.

## Using it

Double-click `pcf2001-setup.exe` and follow the wizard (in Spanish): CD, game folder (default `C:\Juegos\PC Futbol 2001`, with an optional desktop shortcut), folder with the updates, installation.

To uninstall, use *Desinstalar* in the Start menu, or the game's entry in *Settings → Apps*. **Saved games are always kept**: the `Save` folder stays in the game folder.

Command-line modes, for testing and for advanced users:

```
pcf2001-setup.exe /desatendido /cd=<unidad o carpeta> /destino=<carpeta> /actualizaciones=<carpeta> [/sinescritorio]
pcf2001-setup.exe /desinstalar [<carpeta del juego>] [/silencioso]
```

`/desatendido` installs without asking anything and returns exit code 0 on success. `/silencioso` uninstalls without asking or showing messages.

## What it does

1. **Checks everything first:** the CD, the game folder (new or empty, not under *Program Files*, enough free space), and the 13 updates. Nothing is written until all of it is right.
2. **Installs the game's files from the CD,** the way Dinamic's setup did with its *Typical* setup type (everything on disk). Details in [What gets installed](#what-gets-installed). Every file is checked against the MD5 checksum the CD's archive stores for it.
3. **Creates the empty folders** Dinamic's setup created: `Goles`, `Notas` and `Save\Manager`.
4. **Gives every user write access to the game folder,** because the game writes its settings and saved games there.
5. **Writes the registry value Dinamic's setup wrote:** `HKLM\SOFTWARE\DINAMIC MULTIMEDIA\PC FÚTBOL 2001`, value `DIR` = the game folder, in the 32-bit registry view. Dinamic's updaters read it, and so do some community tools.
6. **Applies Dinamic's 13 updates in the order Dinamic released them:** data updates 1 to 4, the 3.6 update, then data updates 5 to 12. For each one, see [Dinamic's updates](#dinamics-updates).
7. **Copies itself into the game folder** as the uninstaller, and writes `pcf2001-setup.files`, the list of every file in the game folder at that point.
8. **Creates the shortcuts,** for all users: a *PC FÚTBOL 2001* folder in the Start menu with the game (Dinamic's launcher, `pcf2001.exe`) and *Desinstalar*, and optionally a desktop shortcut.
9. **Registers the game in Windows' list of installed programs.**

If anything fails, everything installed so far is removed again, except the log.

The result was checked against a reference installation made by hand from the same CD, with the same 13 updates applied: all 402 files have the same SHA-256. That reference has exactly the files of the archive's groups listed below, so it matches what Dinamic's *Typical* setup installed, as far as the archive describes it.

### The log

The installer writes `pcf2001-setup.log` in the game folder: every step, each update's own messages, and any error. Like the patch's log ([DESIGN.md D14](DESIGN.md#d14-a-log-players-can-send)), it contains nothing that identifies the player beyond the game folder. When an installation fails, the log is the one file left in the folder, ready to send.

## What gets installed

The CD's setup is InstallShield 6. Its archive (`DATA1.HDR`, `DATA1.CAB`, `DATA2.CAB`) describes 438 files in file groups; the format is described in [GAME-INTERNALS.md](GAME-INTERNALS.md#the-cds-installer).

| File group | What it is | Installed? |
|---|---|---|
| `Exe & Dll` | The game's programs and libraries | Yes, except the plain `utils.dll` (see `MMX`) |
| `MMX` | `Utils.dll` built with MMX instructions, from the CD's `MMX` folder | Yes. Setup chose it on processors with MMX, which every processor that runs Windows 10 has. |
| `Root` | Help, cursors, tactics, simulator data, sound index… | Yes |
| `Cond_Base Datos` | The database (`DBDAT`) | Yes |
| `Cond_Graficos` | `GRAFICOS.PAK`, `RC_DBASE.PAK` | Yes |
| `Cond_Simulador` | The 3D simulator's stadiums and models (`Simuldat`) | Yes |
| `Cond_Sonido` | Music, commentary and effects (`SFX`) | Yes |
| `Setup` | Icon, intro video, autorun, `DISK.ID` | Only `PCF2001.ico` and `IntroPCF.mpg`. `DISK.ID` stays on the CD, where the game's CD check looks for it. |
| `Shared DLLs` | Microsoft runtime libraries of the time (MFC, MSVCRT…) that setup put into `System32` | No: today's Windows has the ones the game uses |
| `Externos` | DirectX 7/8 and Internet Explorer 5 installers | No: they don't install on today's Windows, and aren't needed |
| `<Engine>`, `<Support>`, `<Disk1>` | InstallShield's own engine and setup files | No |

Compressed files are unpacked from the archive. The others are plain files on the CD, which the installer finds by name and size and copies, checking their MD5.

File names in the archive are in Windows-1252 (some help files have `ñ` and `ó` in their names).

Not installed either: Dinamic's setup added two web links to the Start menu folder, to `infofutbol.com` and `pcfutbol.es`. Both sites have been gone for many years.

## Dinamic's updates

Each update is an InstallShield *PackageForTheWeb* self-extracting program. Inside is a standard Microsoft cabinet (`.cab`) with:

- Dinamic's updater, `PCFPATCH.exe`;
- the RTPatch engine, `patchw32.dll`;
- the patch data (`PCFACTxx.RTP`, or `PCFPATCH36.RTP` for 3.6, which also carries both versions of `Utils.dll`).

For each update, the installer:

1. finds the cabinet inside the program and unpacks it with Windows' built-in cabinet API, into a temporary folder;
2. runs `PCFPATCH.exe`, which finds the game through the `DIR` registry value, and checks that the data is at the matchday this update starts from;
3. presses *Instalar* in its window, waits, and reads its result message. Success is "*N* ficheros actualizados con éxito."; any other message is shown to the player as the reason for the failure.

The updaters run with administrator rights, inherited from the installer. Otherwise Windows would ask for them 13 more times: it treats programs with "PATCH" in their names as installers.

RTPatch is a proprietary format, so running Dinamic's own updater is the only way to apply these updates. It also keeps Dinamic's checks.

## Uninstalling

The uninstaller removes:

- the files listed in `pcf2001-setup.files`;
- the settings files the game writes (`manager.ini`, `MainOpt.dat`, `sound.ini`, `sip.ini`, `update.ini`);
- the patch's files, if the game is still patched: `dcogl.dll`, `dcogl.ini`, its logs and the `pcf2001-patch` folder ([PATCHER.md](PATCHER.md));
- the empty folders;
- the shortcuts, the entry in the list of programs, and the `DIR` registry value (if it still points to this folder).

It never touches the `Save` folder, or files it didn't install, such as tactics the player saved or community add-ons. The game folder itself goes only if nothing is left in it.

Because a program can't delete itself while it runs, the copy in the game folder starts a copy of itself from the temporary folder, which waits for the first to end, does the work, and is deleted at the next restart.

## Source code

`src/installer/`:

| File | What it does |
|---|---|
| `main.cpp` | Command line: the wizard, `/desatendido`, `/desinstalar` |
| `wizard.cpp` | The wizard's pages (layouts in `installer.rc`) |
| `install_job.cpp` | The installation and uninstallation steps |
| `game_files.cpp` | What gets installed from the CD, and copying it |
| `is6_cabinet.cpp`, `inflate.cpp` | Reading InstallShield 6 archives and their DEFLATE compression |
| `dinamic_updates.cpp` | Checking, unpacking and running Dinamic's updates |
| `system_integration.cpp` | Registry, permissions, shortcuts, list of installed programs |
| `setup_log.cpp`, `text.cpp`, `hashes.cpp` | Log, text conversions, MD5 and SHA-256 through Windows' cryptography API |

The development tool `is6-cab` lists, checks and extracts the CD's archive ([TOOLS.md](TOOLS.md#is6-cab)).
