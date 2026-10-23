# pcf2001-patcher

An unofficial, open-source patch that makes **PC FÚTBOL 2001** (Dinamic Multimedia, 2000–2001) run correctly on modern Windows.

> **Status: early development. There is no release yet.** Nothing in this repository is ready for players. This page describes what the patch will do and how it is built, so anyone can follow and check the work.

*Versión en español: [LEEME.md](LEEME.md)*

## What it fixes

| Problem on modern Windows | Status |
|---|---|
| The CD's setup doesn't run (*"Access is denied"* and other errors), so the game can't even be installed | Replaced by our own installer, `pcf2001-setup.exe`, which also applies Dinamic's updates (in testing) |
| The game stops at startup with *"No se pudo iniciar DirectX. El programa no puede comenzar."* | Fixed by the new graphics component (in testing) |
| Full screen switches the whole desktop to 800×600 | Fixed (in testing): the game is drawn at its own resolution and scaled to your screen, keeping the aspect ratio |
| Windowed mode draws some screen elements in the wrong place | Should be fixed by the new graphics component; not tested yet |
| Some long team names show garbage, and some player names appear on the wrong players (previously only avoidable with "Windows XP SP2" compatibility mode) | Cause found: the game reads memory it has already freed. Worked around by a memory compatibility layer (in testing); fixes in the game's code in progress |
| The game can crash when quitting | Fixed by the new graphics component (in testing) |
| Random crashes ending in *"La aplicación no puede continuar"* | Cause found: the game writes to memory it has already freed. Much reduced by the memory compatibility layer (in testing); fixes in the game's code in progress |

It also fixes crashes the original game already had in 2001:

| Problem in the original game | Status |
|---|---|
| With the advertising boards (*Vallas*, in *Estadio*) on *Auto* and no *Encargado del campo* (groundskeeper), the game crashes with *"La aplicación no puede continuar"* | Fixed: the boards are still sold automatically, as if by the assistant, as the game does with other missing employees |

The details of each problem are in [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md).

## What you will need

- Your own copy of PC FÚTBOL 2001: the game CD, or an image of it. The game keeps its CD check, and Windows 10/11 can mount an `.iso` file with a double-click.
- Dinamic's official updates: the **version 3.6** update (`PCFPATCH3.6.EXE`) and the 12 data updates (`PCFACT01.exe` to `PCFACT12.exe`). Our installer applies them all; [docs/DESIGN.md](docs/DESIGN.md#d1-target-dinamics-v36-only) explains why only 3.6 is supported.
- Windows 10 or 11.

Then, two steps:

1. **Install the game** with `pcf2001-setup.exe` ([docs/INSTALLER.md](docs/INSTALLER.md)). It installs from your CD and applies Dinamic's updates. It asks for administrator rights once.
2. **Apply the patch** with `pcf2001-patch.exe` ([docs/PATCHER.md](docs/PATCHER.md)). It can be removed again the same way.

Playing needs no administrator rights, and nothing needs dgVoodoo, compatibility modes or any extra runtime.

## What the patch contains, and what it doesn't

- **It contains only our own work:** a replacement graphics component written from scratch, a list of small, documented changes to the game's own files, and the patcher program that applies them.
- **It contains no Dinamic files.** There are no game executables, data, graphics, sound or videos. It only works on top of a copy you already have.
- **It changes only the files listed in the release notes**, after checking each one is the exact v3.6 file it expects. It keeps backups and can be uninstalled.
- **It makes no network connections** and collects nothing.

## Why you can trust it

The patch is a program you run on your computer, so you should be able to check what it does:

- **All the source code is here,** under the MIT license.
- **You can build it yourself** with free Microsoft tools by running one script. See [docs/BUILDING.md](docs/BUILDING.md).
- **Builds are reproducible:** the same source and tools always produce exactly the same files, so you can compare your own build with a published release.
- **Every change to the game's files will be documented:** which file, where, the original bytes, the new bytes, and why. See [docs/DESIGN.md](docs/DESIGN.md).
- **Releases are built by GitHub Actions** from the public source, never on a developer's computer, and publish SHA-256 hashes of everything they contain, with the toolchain used. See [docs/RELEASING.md](docs/RELEASING.md).

## Reporting a problem

If the game crashes or something looks wrong, please report it and attach the log files from the game folder:

- `dcogl.log`: the current or most recent session;
- `dcogl.old.log`: the session before it. After a crash, people often start the game again before reporting, and this file still has the crash.

Say what you were doing when it happened, and roughly at what time: each line of the log has a time stamp. The logs say which Windows version, graphics card, game version and database you have, but nothing that identifies you, and the patch never sends them anywhere by itself.

## Documentation

| Document | Contents |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | Goals, scope, and every design decision with its reasoning |
| [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) | What we have learned about how the game works and why it fails on modern Windows |
| [docs/BACKEND-INTERFACE.md](docs/BACKEND-INTERFACE.md) | The game's graphics plug-in interface, which the new graphics component implements |
| [docs/INSTALLER.md](docs/INSTALLER.md) | The game installer: what it installs, and how |
| [docs/PATCHER.md](docs/PATCHER.md) | The patcher: what it changes, and how it applies and removes it |
| [docs/BUILDING.md](docs/BUILDING.md) | How to build everything from source |
| [docs/RELEASING.md](docs/RELEASING.md) | How releases are built and published |
| [docs/TOOLS.md](docs/TOOLS.md) | Development tools in this repository |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute, and what must never be committed |

## Legal

This project is not affiliated with, endorsed by, or connected to Dinamic Multimedia or any current owner of the *PC Fútbol* name or of the game's content. *PC FÚTBOL* is a trademark of its respective owner. The patch is distributed without any game files and only works with a copy of the game you already own.

The source code in this repository is licensed under the [MIT License](LICENSE).
