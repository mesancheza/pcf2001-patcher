# Graphics backend interface

PC FÚTBOL 2001's graphics library, `gfxwin.dll`, draws through interchangeable **backend DLLs**. The game ships three of them (`dcsf.dll`, `dcsfx.dll`, `dcd3d.dll`), and they all implement the same interface. The patch's new Direct3D 11 backend implements this interface too ([DESIGN.md D6](DESIGN.md#d6-replace-the-graphics-component-through-the-games-own-plug-in-interface-using-direct3d-11)).

This is a working document, filled in as each method is identified. Everything here comes from analyzing the v3.6 files and from tracing the game with [tools/backend-trace](TOOLS.md#backend-trace).

## Exports

Every backend exports four functions, by name and with these ordinals. All use the `__cdecl` calling convention.

| Ordinal | Function | Signature | Purpose |
|---|---|---|---|
| 1 | `CrearDevice` | `Device* CrearDevice(int index)` | Create the device object for adapter `index`. Returns null on failure. |
| 2 | `DescripcionDispositivo` | `const char* DescripcionDispositivo(int index)` | Display name of adapter `index`, e.g. "Primary Display Driver". |
| 3 | `DestruirDevice` | `void DestruirDevice(Device** device)` | Destroy `*device` through its virtual destructor (slot 0) and set `*device` to null. |
| 4 | `NumeroDispositivos` | `int NumeroDispositivos()` | Number of adapters this backend offers. |

`dcd3d.dll` enumerates the DirectDraw adapters when it's loaded. One adapter is the "Primary Display Driver", plus one per graphics card.

## How the game loads a backend

`CGFX_API::Load` in `gfxwin.dll` (RVA `0x471E0`):

1. Load the DLL and find the four exports. If any is missing, or `NumeroDispositivos` returns 0, reject the backend.
2. For each adapter index from 0 to `NumeroDispositivos() − 1`:
   1. Create the device with `CrearDevice(index)`.
   2. Call slot 1, `Init(want3D, 2, 0, hwnd)`, where `hwnd` is the game's main window.
   3. If `want3D` is set, also require the 3D mode list (slot 9) to have at least one entry.
   4. If either check fails, call slot 2 and destroy the device with `DestruirDevice`. Otherwise keep it.
3. Accept the backend if at least one device was kept.

The game always asks for 3D. [GAME-INTERNALS.md](GAME-INTERNALS.md#startup-failure-no-se-pudo-iniciar-directx) explains why this makes the default backend fail on modern Windows.

## Display mode lists

Slots 8 (2D) and 9 (3D) return a pointer to a two-field structure: a pointer to an array of mode entries, followed by the number of entries. Each entry is `0x20` bytes long:

| Offset | Size | Content |
|---|---|---|
| `+0x00` | 4 | Bits per pixel (16, 24 or 32) |
| `+0x14` | 4 | Width in pixels |
| `+0x18` | 4 | Height in pixels |

The meaning of the other bytes isn't known yet.

When `gfxwin.dll` looks for a mode, it compares width and height exactly. For the color depth, it accepts special request values:
- `0x21`: any of 16, 24 or 32 bits;
- `0x22`: 16 bits only;
- `0`: any depth;
- anything else: that exact bit depth.

## The device object

A C++ object with a virtual function table of **173 methods**, in the same order in all three backends. Methods use Microsoft's `__thiscall` convention: `this` in the `ECX` register, arguments on the stack, and the method removes its arguments from the stack. Each backend allocates its own object, of a different size: `0x620` bytes in `dcd3d.dll`, much larger in the software backends.

Whether `gfxwin.dll` also reads the device's data fields directly, rather than only calling its methods, is still being checked. It matters for the new backend's object layout.

### Surfaces

Most of the game's 2D drawing happens **in software**: `gfxwin.dll` asks the backend for plain memory surfaces, locks them, writes the pixels itself, and unlocks them. The backend's main 2D jobs are creating and releasing those surfaces, copying rectangles between them, and presenting the finished frame. In `dcd3d.dll` the surfaces are DirectDraw surfaces in system memory.

The usual sequence:
1. create a surface (slot 22);
2. lock it, getting its updated description (slots 26 and 27);
3. draw into its pixels directly;
4. unlock it (slot 28);
5. copy it to the back buffer (slot 35);
6. present the frame (slots 15 and 16).

A surface is described by **11 32-bit values**. The handle returned by slot 22 keeps a copy, which slot 25 returns:

| Index | Content |
|---|---|
| 0 | Width in pixels |
| 1 | Height in pixels |
| 2 | Bits per pixel. 8 means a palettized surface. |
| 3–6 | Color channel bit masks: red, green, blue, and a fourth mask (probably alpha). Not used for 8-bit surfaces. |
| 7 | Transparent color used when copying, or −1 for none |
| 8–9 | The pixel pointer and the row pitch, filled in while the surface is locked and cleared when it's unlocked. Which index holds which is still to be confirmed. |
| 10 | Unknown |

### Method table

Column meanings:
- **2D backends:** `yes` means the software-only backends (`dcsfx.dll`, `dcsf.dll`) implement the method; `3D only` means they leave it empty and only `dcd3d.dll` implements it; `trivial` means it is a few bytes long (a field getter or an empty body) everywhere.
- **Calls:** how many times the method was called during a longer play session: the main menu, Opciones, starting a new game, the team selection screens, several manager screens, and two matches in Resumen mode. Blank means not seen yet.
- **Purpose:** filled in as each method is identified.

| Slot | Offset | 2D backends | Calls | Purpose |
|---|---|---|---|---|
| 0 | `+0x000` | yes | 1 | Destructor (scalar deleting); called by `DestruirDevice` |
| 1 | `+0x004` | yes | 3 | `Init(want3D, 2, 0, hwnd)` → bool. First call after creation |
| 2 | `+0x008` | yes | 2 | Undo `Init`: called before destroying a device that failed, and at shutdown |
| 3 | `+0x00c` | yes |  |  |
| 4 | `+0x010` | yes |  |  |
| 5 | `+0x014` | trivial | 1 |  |
| 6 | `+0x018` | yes | 52 |  |
| 7 | `+0x01c` | yes | 1 |  |
| 8 | `+0x020` | trivial | 10 | 2D display mode list → pointer to `{entries, count}` |
| 9 | `+0x024` | yes | 3 | 3D display mode list → pointer to `{entries, count}` |
| 10 | `+0x028` | yes | 1 |  |
| 11 | `+0x02c` | yes |  |  |
| 12 | `+0x030` | yes | 6 |  |
| 13 | `+0x034` | yes | 6 |  |
| 14 | `+0x038` | yes | 1 |  |
| 15 | `+0x03c` | yes | 2,890 | Present a rectangle of the back buffer (in windowed mode, converts window coordinates to screen coordinates first) |
| 16 | `+0x040` | yes | 5,191 | Present the whole frame: page flip in full screen (restoring lost surfaces), copy of the changed area in windowed mode |
| 17 | `+0x044` | yes |  |  |
| 18 | `+0x048` | yes |  |  |
| 19 | `+0x04c` | yes |  |  |
| 20 | `+0x050` | yes |  |  |
| 21 | `+0x054` | 3D only | 14 |  |
| 22 | `+0x058` | yes | 270,474 | `CreateSurface(description)` → surface handle. See [Surfaces](#surfaces) |
| 23 | `+0x05c` | yes |  |  |
| 24 | `+0x060` | yes | 270,175 | `ReleaseSurface(handle)`. Called by `CGFXSurface::Destroy` |
| 25 | `+0x064` | yes | 1,618,174 | `GetSurfaceDescription(handle)` → copy of the description (returned by value) |
| 26 | `+0x068` | yes | 1,347,590 | Lock a surface, then return its updated description |
| 27 | `+0x06c` | yes | 1,347,590 | `LockSurface(handle)`: fills in the pixel pointer and row pitch |
| 28 | `+0x070` | yes | 1,347,589 | `UnlockSurface(handle)`: clears the pixel pointer and row pitch |
| 29 | `+0x074` | yes | 108 |  |
| 30 | `+0x078` | yes | 174 |  |
| 31 | `+0x07c` | trivial |  |  |
| 32 | `+0x080` | yes |  |  |
| 33 | `+0x084` | yes |  |  |
| 34 | `+0x088` | yes |  |  |
| 35 | `+0x08c` | yes | 88,695 | Copy a rectangle between surfaces, honoring the source transparent color if it has one |
| 36 | `+0x090` | yes | 96 |  |
| 37 | `+0x094` | yes |  |  |
| 38 | `+0x098` | trivial |  |  |
| 39 | `+0x09c` | yes |  |  |
| 40 | `+0x0a0` | yes |  |  |
| 41 | `+0x0a4` | yes |  |  |
| 42 | `+0x0a8` | yes |  |  |
| 43 | `+0x0ac` | yes |  |  |
| 44 | `+0x0b0` | yes |  |  |
| 45 | `+0x0b4` | yes |  |  |
| 46 | `+0x0b8` | yes |  |  |
| 47 | `+0x0bc` | yes |  |  |
| 48 | `+0x0c0` | yes |  |  |
| 49 | `+0x0c4` | yes |  |  |
| 50 | `+0x0c8` | yes |  |  |
| 51 | `+0x0cc` | yes |  |  |
| 52 | `+0x0d0` | 3D only |  |  |
| 53 | `+0x0d4` | yes |  |  |
| 54 | `+0x0d8` | yes |  |  |
| 55 | `+0x0dc` | yes |  |  |
| 56 | `+0x0e0` | trivial |  |  |
| 57 | `+0x0e4` | trivial |  |  |
| 58 | `+0x0e8` | yes |  |  |
| 59 | `+0x0ec` | yes | 4 |  |
| 60 | `+0x0f0` | yes | 52 |  |
| 61 | `+0x0f4` | 3D only |  |  |
| 62 | `+0x0f8` | 3D only |  |  |
| 63 | `+0x0fc` | yes |  |  |
| 64 | `+0x100` | trivial |  |  |
| 65 | `+0x104` | yes |  |  |
| 66 | `+0x108` | trivial |  |  |
| 67 | `+0x10c` | trivial | 52 |  |
| 68 | `+0x110` | 3D only |  |  |
| 69 | `+0x114` | yes |  |  |
| 70 | `+0x118` | 3D only |  |  |
| 71 | `+0x11c` | trivial |  |  |
| 72 | `+0x120` | yes |  |  |
| 73 | `+0x124` | yes | 66 |  |
| 74 | `+0x128` | yes | 66 |  |
| 75 | `+0x12c` | yes |  |  |
| 76 | `+0x130` | yes | 3,504 |  |
| 77 | `+0x134` | yes |  |  |
| 78 | `+0x138` | yes | 3,504 |  |
| 79 | `+0x13c` | 3D only | 4 |  |
| 80 | `+0x140` | yes |  |  |
| 81 | `+0x144` | 3D only | 14 |  |
| 82 | `+0x148` | 3D only | 14 |  |
| 83 | `+0x14c` | 3D only |  |  |
| 84 | `+0x150` | 3D only |  |  |
| 85 | `+0x154` | 3D only |  |  |
| 86 | `+0x158` | trivial |  |  |
| 87 | `+0x15c` | 3D only |  |  |
| 88 | `+0x160` | yes |  |  |
| 89 | `+0x164` | trivial |  |  |
| 90 | `+0x168` | yes |  |  |
| 91 | `+0x16c` | yes |  |  |
| 92 | `+0x170` | 3D only |  |  |
| 93 | `+0x174` | 3D only |  |  |
| 94 | `+0x178` | 3D only |  |  |
| 95 | `+0x17c` | 3D only |  |  |
| 96 | `+0x180` | 3D only |  |  |
| 97 | `+0x184` | 3D only |  |  |
| 98 | `+0x188` | 3D only |  |  |
| 99 | `+0x18c` | yes | 7 |  |
| 100 | `+0x190` | yes |  |  |
| 101 | `+0x194` | trivial |  |  |
| 102 | `+0x198` | yes |  |  |
| 103 | `+0x19c` | trivial |  |  |
| 104 | `+0x1a0` | trivial | 7 |  |
| 105 | `+0x1a4` | yes |  |  |
| 106 | `+0x1a8` | 3D only |  |  |
| 107 | `+0x1ac` | trivial |  |  |
| 108 | `+0x1b0` | yes |  |  |
| 109 | `+0x1b4` | 3D only | 2 |  |
| 110 | `+0x1b8` | trivial | 7 |  |
| 111 | `+0x1bc` | yes |  |  |
| 112 | `+0x1c0` | yes |  |  |
| 113 | `+0x1c4` | trivial | 7 |  |
| 114 | `+0x1c8` | 3D only |  |  |
| 115 | `+0x1cc` | trivial |  |  |
| 116 | `+0x1d0` | trivial | 7 |  |
| 117 | `+0x1d4` | yes |  |  |
| 118 | `+0x1d8` | yes |  |  |
| 119 | `+0x1dc` | trivial | 7 |  |
| 120 | `+0x1e0` | yes |  |  |
| 121 | `+0x1e4` | 3D only |  |  |
| 122 | `+0x1e8` | trivial |  |  |
| 123 | `+0x1ec` | 3D only |  |  |
| 124 | `+0x1f0` | trivial |  |  |
| 125 | `+0x1f4` | yes |  |  |
| 126 | `+0x1f8` | yes |  |  |
| 127 | `+0x1fc` | trivial |  |  |
| 128 | `+0x200` | 3D only |  |  |
| 129 | `+0x204` | 3D only |  |  |
| 130 | `+0x208` | 3D only |  |  |
| 131 | `+0x20c` | yes |  |  |
| 132 | `+0x210` | trivial | 7 |  |
| 133 | `+0x214` | 3D only |  |  |
| 134 | `+0x218` | trivial |  |  |
| 135 | `+0x21c` | trivial |  |  |
| 136 | `+0x220` | 3D only |  |  |
| 137 | `+0x224` | yes |  |  |
| 138 | `+0x228` | yes | 49,565 |  |
| 139 | `+0x22c` | yes |  |  |
| 140 | `+0x230` | trivial |  |  |
| 141 | `+0x234` | trivial |  |  |
| 142 | `+0x238` | 3D only |  |  |
| 143 | `+0x23c` | 3D only |  |  |
| 144 | `+0x240` | trivial |  |  |
| 145 | `+0x244` | 3D only |  |  |
| 146 | `+0x248` | 3D only |  |  |
| 147 | `+0x24c` | trivial |  |  |
| 148 | `+0x250` | 3D only |  |  |
| 149 | `+0x254` | 3D only |  |  |
| 150 | `+0x258` | 3D only |  |  |
| 151 | `+0x25c` | trivial |  |  |
| 152 | `+0x260` | trivial |  |  |
| 153 | `+0x264` | 3D only |  |  |
| 154 | `+0x268` | 3D only |  |  |
| 155 | `+0x26c` | 3D only |  |  |
| 156 | `+0x270` | trivial |  |  |
| 157 | `+0x274` | 3D only | 7 |  |
| 158 | `+0x278` | trivial |  |  |
| 159 | `+0x27c` | 3D only |  |  |
| 160 | `+0x280` | yes |  |  |
| 161 | `+0x284` | yes | 24 |  |
| 162 | `+0x288` | yes |  |  |
| 163 | `+0x28c` | yes | 6 |  |
| 164 | `+0x290` | yes |  |  |
| 165 | `+0x294` | trivial |  |  |
| 166 | `+0x298` | trivial |  |  |
| 167 | `+0x29c` | yes |  |  |
| 168 | `+0x2a0` | trivial |  |  |
| 169 | `+0x2a4` | yes |  |  |
| 170 | `+0x2a8` | yes |  |  |
| 171 | `+0x2ac` | yes |  |  |
| 172 | `+0x2b0` | yes |  |  |

### Summary so far

- **42 methods are 3D-only:** slots 21, 52, 61, 62, 68, 70, 79, 81–85, 87, 92–98, 106, 109, 114, 121, 123, 128–130, 133, 136, 142, 143, 145, 146, 148–150, 153–155, 157 and 159.
- **47 methods were called during the play session,** from 121 call sites.
- **The per-frame drawing work is concentrated in a few methods.** Slots 25–28 ran more than a million times each, slots 22 and 24 about 270,000 times each, and slot 35 about 89,000 times.
- **Slot 24 is confirmed** as "release surface handle", from analyzing a crash (see [GAME-INTERNALS.md](GAME-INTERNALS.md#crash-on-exit)).
