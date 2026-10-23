// Data structures exchanged with gfxwin.dll through the device interface (see docs/BACKEND-INTERFACE.md).
// Their sizes and field order are fixed by the game; the static_asserts below guard them.
#pragma once

#include <windows.h>

#include <cstdint>

// Display mode, as listed by GetModes2D/GetModes3D and passed to SetMode. 32 bytes.
struct ModeEntry
{
    uint32_t bitsPerPixel;
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
    uint32_t alphaMask;
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
};

struct ModeList
{
    ModeEntry* entries;
    int32_t count;
};

// Description of a surface: what CreateSurface receives and GetSurfaceDesc returns. 11 values.
struct SurfaceDesc
{
    int32_t width;
    int32_t height;
    int32_t bitsPerPixel;      // 8 means palettized
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
    uint32_t alphaMask;
    uint32_t colorKey;         // transparent color for copies, kNoColorKey for none
    int32_t pitch;             // bytes per row while locked, -1 otherwise
    void* pixels;              // while locked, null otherwise
    uint32_t* palette;         // 256 entries 0x00RRGGBB for 8-bit surfaces that have one
};

constexpr uint32_t kNoColorKey = 0xFFFFFFFFu;

// Pixel format entry of GetTextureFormats. 20 bytes.
struct TextureFormat
{
    uint32_t bitsPerPixel;
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
    uint32_t alphaMask;
};

struct TextureFormatList
{
    TextureFormat* entries;
    int32_t count;
};

// Vertex buffer description: what CreateVertexBuffer receives and LockVertexBuffer returns. 5 values.
struct VertexBufferDesc
{
    uint32_t value0;
    uint32_t usage;
    uint32_t vertexCount;
    void* data;                // while locked
    uint32_t format;           // vertex layout flags
};

// Same layout as DirectDraw's DDGAMMARAMP.
struct GammaRamp
{
    uint16_t red[256];
    uint16_t green[256];
    uint16_t blue[256];
};

class Surface;
class VertexBuffer;

static_assert(sizeof(ModeEntry) == 0x20, "ModeEntry layout is fixed by the game");
static_assert(sizeof(SurfaceDesc) == 0x2C, "SurfaceDesc layout is fixed by the game");
static_assert(sizeof(TextureFormat) == 0x14, "TextureFormat layout is fixed by the game");
static_assert(sizeof(VertexBufferDesc) == 0x14, "VertexBufferDesc layout is fixed by the game");
static_assert(sizeof(GammaRamp) == 0x600, "GammaRamp layout is fixed by the game");
