# pcf2001-patcher

An unofficial, open-source patch that makes **PC FÚTBOL 2001** (Dinamic Multimedia, 2000–2001) run correctly on modern Windows.

> **Status: early development. There is no release yet.** Nothing in this repository is ready for players. This page describes what the patch will do and how it is built, so anyone can follow and check the work.

*Versión en español: [LEEME.md](LEEME.md)*

## What it fixes

| Problem on modern Windows | Status |
|---|---|
| The game stops at startup with *"No se pudo iniciar DirectX. El programa no puede comenzar."* | Root cause found, fix in progress |
| Full screen switches the whole desktop to 800×600 | Planned: rendered at the game's resolution and scaled to your screen, keeping the aspect ratio |
| Windowed mode draws some screen elements in the wrong place | Planned: same fix as above |
| Some long team names show garbage, and some player names appear on the wrong players (previously only avoidable with "Windows XP SP2" compatibility mode) | Being investigated; will be fixed in the game's code |
| The game can crash when quitting | Cause found (the game uses already-freed graphics objects while shutting down); fix planned in the new graphics component |
| Other random crashes reported by players | To be investigated after the issues above |

The details of each problem are in [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md).

## What you will need

- Your own copy of PC FÚTBOL 2001, updated with Dinamic's official **version 3.6** update (`PCFPATCH3.6.EXE`). The main menu shows "V3.6" in the top-right corner once it is installed. [docs/DESIGN.md](docs/DESIGN.md#d1-target-dinamics-v36-only) explains why only 3.6 is supported.
- The game CD, or an image of it mounted as a drive. The patch keeps the game's CD check, and Windows 10/11 can mount an `.iso` file with a double-click.
- Windows 10 or 11.

The patch does **not** need dgVoodoo, compatibility modes, administrator rights, or any extra runtime.

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
- **Releases will publish SHA-256 hashes** of everything they contain.

## Documentation

| Document | Contents |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | Goals, scope, and every design decision with its reasoning |
| [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) | What we have learned about how the game works and why it fails on modern Windows |
| [docs/BACKEND-INTERFACE.md](docs/BACKEND-INTERFACE.md) | The game's graphics plug-in interface, which the new graphics component implements |
| [docs/BUILDING.md](docs/BUILDING.md) | How to build everything from source |
| [docs/TOOLS.md](docs/TOOLS.md) | Development tools in this repository |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute, and what must never be committed |

## Legal

This project is not affiliated with, endorsed by, or connected to Dinamic Multimedia or any current owner of the *PC Fútbol* name or of the game's content. *PC FÚTBOL* is a trademark of its respective owner. The patch is distributed without any game files and only works with a copy of the game you already own.

The source code in this repository is licensed under the [MIT License](LICENSE).
