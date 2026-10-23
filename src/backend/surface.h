// Surfaces handed to the game through the device interface.
//
// gfxwin.dll does most of its 2D drawing itself: it locks a surface and writes pixels, or asks for a device context
// and draws text with GDI. So every surface is a top-down DIB section in system memory, which supports both.
// Surfaces are opaque handles to the game; only the methods of the device interface touch them.
#pragma once

#include "interface_types.h"

#include <cstdint>

class Surface
{
public:
    // Returns null if the description is invalid or memory is exhausted.
    static Surface* Create(const SurfaceDesc& desc, bool texture);
    ~Surface();

    // Recreates the pixel memory for a new description, keeping this object (the game may hold on to the pointer,
    // e.g. the back buffer across display mode changes). Returns false and leaves the surface unchanged on failure.
    bool Reset(const SurfaceDesc& desc);

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    const SurfaceDesc& Desc() const { return m_desc; }
    int Width() const { return m_desc.width; }
    int Height() const { return m_desc.height; }
    int BitsPerPixel() const { return m_desc.bitsPerPixel; }
    int Stride() const { return m_stride; }
    uint8_t* Row(int y) const { return static_cast<uint8_t*>(m_bits) + y * m_stride; }

    void SetColorKey(uint32_t colorKey) { m_desc.colorKey = colorKey; }

    bool Lock();
    void Unlock();
    bool IsLocked() const { return m_desc.pixels != nullptr; }

    HDC GetDC();
    void ReleaseDC(HDC dc);

    // Stores 256 palette entries (0x00RRGGBB); ApplyPalette makes a range of them effective for GDI drawing.
    void SetPalette(const uint32_t* entries);
    void ApplyPalette(int first, int count);
    const uint32_t* Palette() const { return m_palette; }

    // Pixel helpers used by copies, fills and presentation.
    uint32_t ReadRaw(int x, int y) const;
    void WriteRaw(int x, int y, uint32_t value);
    uint32_t ReadRgb(int x, int y) const;          // 0x00RRGGBB
    uint32_t RgbToRaw(uint32_t rgb) const;

private:
    Surface() = default;
    bool Allocate(const SurfaceDesc& desc);
    void Free();

    SurfaceDesc m_desc{};
    HANDLE m_section = nullptr;  // memory holding the pixels, with slack around them (see Allocate)
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_previousBitmap = nullptr;
    void* m_bits = nullptr;
    int m_stride = 0;
    int m_bytesPerPixel = 0;
    HDC m_dc = nullptr;
    int m_dcUsers = 0;
    uint32_t m_palette[256]{};
};

// Copies srcRect of src to dstRect of dst, stretching (nearest pixel) if the sizes differ. Null rectangles mean the
// whole surface. Rectangles are clipped to both surfaces. Honors the source transparent color if it has one.
bool CopyRect(Surface& dst, const RECT* dstRect, const Surface& src, const RECT* srcRect);

// Fills a rectangle (null = whole surface) with a raw value in the surface pixel format.
bool FillSurfaceRect(Surface& surface, const RECT* rect, uint32_t rawColor);
