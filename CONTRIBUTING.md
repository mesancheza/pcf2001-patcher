# Contributing

Thanks for helping keep PC FÚTBOL 2001 playable. Please read this first.

## Never commit

This repository must stay free of anything that belongs to the game's rights holders. Never commit:

- **The game's files,** original or modified: executables (`.exe`), libraries (`.dll`), data (`.PAK`, `.FDI`, `.DBC` and others), graphics, sound, music or video.
- **Disc images** (`.iso` and similar).
- **Decompiled or disassembled game code,** including output from Ghidra, IDA or similar tools, and **reverse-engineering databases** such as Ghidra projects.
- **Memory dumps** of the game (`.dmp`). They contain the game's code and data.

`.gitignore` blocks the common file types, but it can't catch everything. Please check what you add.

What **is** welcome:

- **Original code.**
- **Descriptions, in your own words, of how the game behaves:** file formats, interfaces, addresses, and the original and replacement bytes of a documented patch.
- **Documentation.**

## Building

See [docs/BUILDING.md](docs/BUILDING.md). Everything builds with one script and free Microsoft tools.

## Design decisions

The rules the patch follows are in [docs/DESIGN.md](docs/DESIGN.md): v3.6 only, standalone, no Dinamic files, fixes at the root cause, and documented changes. If you think a decision should change, open an issue to discuss it first.

## Code style

- C++20, Microsoft compiler, 32-bit, warning level 4 with no warnings.
- Code, comments and documentation in English. The patcher's user interface is in Spanish.
- Comment the *why*, especially for anything that relies on how the original game works. Reference the relevant section of [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) or [docs/BACKEND-INTERFACE.md](docs/BACKEND-INTERFACE.md).

## Changes to the game's files

Every byte change the patch makes to a game file must be documented with:

- the file and its v3.6 SHA-256;
- the address;
- the original bytes;
- the new bytes;
- the reason.

No undocumented changes.
