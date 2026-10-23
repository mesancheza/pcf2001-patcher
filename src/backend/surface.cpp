#include "surface.h"

#include "backend_log.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace
{
constexpr DWORD kSurfaceSlack = 4096;  // readable bytes before and after the pixels of every surface

struct DibInfo
{
    BITMAPINFOHEADER header;
    RGBQUAD colors[256];  // palette for 8-bit, or the three BI_BITFIELDS masks
};

int MaskShift(uint32_t mask)
{
    if (!mask)
        return 0;
    unsigned long index;
    _BitScanForward(&index, mask);
    return static_cast<int>(index);
}

int MaskBits(uint32_t mask)
{
    return static_cast<int>(__popcnt(mask));
}

// Extracts a channel and scales it to 0..255.
uint32_t Expand(uint32_t raw, uint32_t mask)
{
    if (!mask)
        return 0;
    const uint32_t max = (1u << MaskBits(mask)) - 1;
    const uint32_t value = (raw & mask) >> MaskShift(mask);
    return (value * 255 + max / 2) / max;
}

// Scales a 0..255 channel into a mask.
uint32_t Compress(uint32_t channel, uint32_t mask)
{
    if (!mask)
        return 0;
    const uint32_t max = (1u << MaskBits(mask)) - 1;
    return ((channel * max + 127) / 255) << MaskShift(mask);
}

void DefaultMasks(SurfaceDesc& desc)
{
    if (desc.redMask || desc.greenMask || desc.blueMask)
        return;
    if (desc.bitsPerPixel == 16)
    {
        desc.redMask = 0xF800;
        desc.greenMask = 0x07E0;
        desc.blueMask = 0x001F;
    }
    else if (desc.bitsPerPixel == 24 || desc.bitsPerPixel == 32)
    {
        desc.redMask = 0xFF0000;
        desc.greenMask = 0x00FF00;
        desc.blueMask = 0x0000FF;
    }
}

// Clips a destination rectangle to the surface, returning false if nothing is left.
bool ClipToSurface(RECT& rect, const Surface& surface)
{
    rect.left = std::max<LONG>(rect.left, 0);
    rect.top = std::max<LONG>(rect.top, 0);
    rect.right = std::min<LONG>(rect.right, surface.Width());
    rect.bottom = std::min<LONG>(rect.bottom, surface.Height());
    return rect.left < rect.right && rect.top < rect.bottom;
}

bool SameFormat(const Surface& a, const Surface& b)
{
    const SurfaceDesc& x = a.Desc();
    const SurfaceDesc& y = b.Desc();
    if (x.bitsPerPixel != y.bitsPerPixel)
        return false;
    if (x.bitsPerPixel == 8)
        return true;  // indices are copied as they are, like DirectDraw does
    return x.redMask == y.redMask && x.greenMask == y.greenMask && x.blueMask == y.blueMask;
}
}  // namespace

Surface* Surface::Create(const SurfaceDesc& desc, bool texture)
{
    (void)texture;  // textures are ordinary surfaces here; the 2D backend never draws them as 3D textures
    auto* surface = new (std::nothrow) Surface();
    if (surface && !surface->Allocate(desc))
    {
        delete surface;
        surface = nullptr;
    }
    return surface;
}

bool Surface::Reset(const SurfaceDesc& desc)
{
    Surface replacement;
    if (!replacement.Allocate(desc))
        return false;
    Free();
    // Take over the new resources; the palette is kept.
    m_desc = replacement.m_desc;
    m_bitmap = replacement.m_bitmap;
    m_section = replacement.m_section;
    m_bits = replacement.m_bits;
    m_stride = replacement.m_stride;
    m_bytesPerPixel = replacement.m_bytesPerPixel;
    replacement.m_bitmap = nullptr;
    replacement.m_section = nullptr;
    return true;
}

bool Surface::Allocate(const SurfaceDesc& requested)
{
    SurfaceDesc desc = requested;
    if (desc.width <= 0 || desc.height <= 0 || desc.width > 16384 || desc.height > 16384)
        return false;
    if (desc.bitsPerPixel != 8 && desc.bitsPerPixel != 16 && desc.bitsPerPixel != 24 && desc.bitsPerPixel != 32)
        return false;
    DefaultMasks(desc);

    DibInfo info{};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = desc.width;
    info.header.biHeight = -desc.height;  // top-down: row 0 first, positive pitch
    info.header.biPlanes = 1;
    info.header.biBitCount = static_cast<WORD>(desc.bitsPerPixel);
    if (desc.bitsPerPixel == 16 || desc.bitsPerPixel == 32)
    {
        info.header.biCompression = BI_BITFIELDS;
        auto* masks = reinterpret_cast<DWORD*>(info.colors);
        masks[0] = desc.redMask;
        masks[1] = desc.greenMask;
        masks[2] = desc.blueMask;
    }
    else
    {
        info.header.biCompression = BI_RGB;
        if (desc.bitsPerPixel == 8)
            info.header.biClrUsed = 256;
    }

    // The pixels live in a memory section with slack before and after them. The game's drawing code reads a little
    // outside the surfaces it blends from (e.g. Utils.dll's mask fill reads the byte before each row and only then
    // checks bounds). With dcsfx the pixels were heap memory, where that was harmless; a bare DIB section starts on
    // a page boundary and the byte before it may not be mapped.
    const int stride = ((desc.width * desc.bitsPerPixel + 31) / 32) * 4;
    const uint64_t sectionSize = kSurfaceSlack + static_cast<uint64_t>(stride) * desc.height + kSurfaceSlack;
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(sectionSize >> 32),
                                        static_cast<DWORD>(sectionSize), nullptr);
    if (!section)
        return false;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &bits, section, kSurfaceSlack);
    if (!bitmap || !bits)
    {
        if (bitmap)
            DeleteObject(bitmap);
        CloseHandle(section);
        return false;
    }
    static bool checked;
    MEMORY_BASIC_INFORMATION before{};
    if (!checked && VirtualQuery(static_cast<uint8_t*>(bits) - 1, &before, sizeof before) && before.State != MEM_COMMIT)
        BackendLog("surface: the memory before the pixels is not readable; the game's over-reads may fault\n");
    checked = true;
    m_section = section;
    m_bitmap = bitmap;
    m_bits = bits;
    m_bytesPerPixel = desc.bitsPerPixel / 8;
    m_stride = stride;
    memset(m_bits, 0, static_cast<size_t>(m_stride) * desc.height);

    desc.pitch = -1;
    desc.pixels = nullptr;
    desc.palette = m_desc.palette;  // keeps an existing palette across Reset
    m_desc = desc;
    return true;
}

void Surface::Free()
{
    if (m_dc)
    {
        SelectObject(m_dc, m_previousBitmap);
        DeleteDC(m_dc);
        m_dc = nullptr;
        m_dcUsers = 0;
    }
    if (m_bitmap)
    {
        DeleteObject(m_bitmap);
        m_bitmap = nullptr;
    }
    if (m_section)
    {
        CloseHandle(m_section);
        m_section = nullptr;
    }
    m_bits = nullptr;
}

Surface::~Surface()
{
    Free();
}

bool Surface::Lock()
{
    if (!IsLocked())
    {
        GdiFlush();  // GDI may still be drawing into the DIB
        m_desc.pitch = m_stride;
        m_desc.pixels = m_bits;
    }
    return true;
}

void Surface::Unlock()
{
    m_desc.pitch = -1;
    m_desc.pixels = nullptr;
}

HDC Surface::GetDC()
{
    if (!m_dc)
    {
        m_dc = CreateCompatibleDC(nullptr);
        if (!m_dc)
            return nullptr;
        m_previousBitmap = SelectObject(m_dc, m_bitmap);
        if (m_bytesPerPixel == 1)
            ApplyPalette(0, 256);
    }
    ++m_dcUsers;
    return m_dc;
}

void Surface::ReleaseDC(HDC dc)
{
    if (dc == m_dc && m_dcUsers > 0)
        --m_dcUsers;
    GdiFlush();
}

void Surface::SetPalette(const uint32_t* entries)
{
    if (entries)
        memcpy(m_palette, entries, sizeof m_palette);
    m_desc.palette = m_palette;
    ApplyPalette(0, 256);
}

void Surface::ApplyPalette(int first, int count)
{
    if (m_bytesPerPixel != 1 || !m_dc)
        return;  // without a DC the palette is applied when one is created
    first = std::clamp(first, 0, 256);
    count = std::clamp(count, 0, 256 - first);
    // 0x00RRGGBB has the same byte order as RGBQUAD.
    SetDIBColorTable(m_dc, first, count, reinterpret_cast<const RGBQUAD*>(m_palette + first));
}

uint32_t Surface::ReadRaw(int x, int y) const
{
    const uint8_t* p = Row(y) + x * m_bytesPerPixel;
    switch (m_bytesPerPixel)
    {
    case 1: return p[0];
    case 2: return *reinterpret_cast<const uint16_t*>(p);
    case 3: return p[0] | (p[1] << 8) | (p[2] << 16);
    default: return *reinterpret_cast<const uint32_t*>(p);
    }
}

void Surface::WriteRaw(int x, int y, uint32_t value)
{
    uint8_t* p = Row(y) + x * m_bytesPerPixel;
    switch (m_bytesPerPixel)
    {
    case 1: p[0] = static_cast<uint8_t>(value); break;
    case 2: *reinterpret_cast<uint16_t*>(p) = static_cast<uint16_t>(value); break;
    case 3:
        p[0] = static_cast<uint8_t>(value);
        p[1] = static_cast<uint8_t>(value >> 8);
        p[2] = static_cast<uint8_t>(value >> 16);
        break;
    default: *reinterpret_cast<uint32_t*>(p) = value; break;
    }
}

uint32_t Surface::ReadRgb(int x, int y) const
{
    const uint32_t raw = ReadRaw(x, y);
    if (m_bytesPerPixel == 1)
        return m_palette[raw] & 0xFFFFFF;
    return (Expand(raw, m_desc.redMask) << 16) | (Expand(raw, m_desc.greenMask) << 8) | Expand(raw, m_desc.blueMask);
}

uint32_t Surface::RgbToRaw(uint32_t rgb) const
{
    if (m_bytesPerPixel == 1)
    {
        // Rare (copies into a palettized surface from a true-color one): nearest palette entry.
        int best = 0;
        uint32_t bestDistance = UINT32_MAX;
        for (int i = 0; i < 256; ++i)
        {
            const int dr = static_cast<int>((m_palette[i] >> 16) & 0xFF) - static_cast<int>((rgb >> 16) & 0xFF);
            const int dg = static_cast<int>((m_palette[i] >> 8) & 0xFF) - static_cast<int>((rgb >> 8) & 0xFF);
            const int db = static_cast<int>(m_palette[i] & 0xFF) - static_cast<int>(rgb & 0xFF);
            const uint32_t distance = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return static_cast<uint32_t>(best);
    }
    return Compress((rgb >> 16) & 0xFF, m_desc.redMask) | Compress((rgb >> 8) & 0xFF, m_desc.greenMask) |
           Compress(rgb & 0xFF, m_desc.blueMask) | m_desc.alphaMask;
}

bool CopyRect(Surface& dst, const RECT* dstRectIn, const Surface& src, const RECT* srcRectIn)
{
    RECT srcRect = srcRectIn ? *srcRectIn : RECT{0, 0, src.Width(), src.Height()};
    RECT dstRect = dstRectIn ? *dstRectIn : RECT{0, 0, dst.Width(), dst.Height()};
    const int srcW = srcRect.right - srcRect.left;
    const int srcH = srcRect.bottom - srcRect.top;
    const int dstW = dstRect.right - dstRect.left;
    const int dstH = dstRect.bottom - dstRect.top;
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return false;

    GdiFlush();
    const bool keyed = src.Desc().colorKey != kNoColorKey;
    const uint32_t key = src.Desc().colorKey;
    const bool sameFormat = SameFormat(dst, src);
    const bool stretch = srcW != dstW || srcH != dstH;

    RECT clipped = dstRect;
    if (!ClipToSurface(clipped, dst))
        return true;

    if (!stretch && sameFormat && !keyed)
    {
        // Fast path: row copies. Also clip against the source.
        const int offsetX = srcRect.left - dstRect.left;
        const int offsetY = srcRect.top - dstRect.top;
        clipped.left = std::max<LONG>(clipped.left, -offsetX);
        clipped.top = std::max<LONG>(clipped.top, -offsetY);
        clipped.right = std::min<LONG>(clipped.right, src.Width() - offsetX);
        clipped.bottom = std::min<LONG>(clipped.bottom, src.Height() - offsetY);
        if (clipped.left >= clipped.right || clipped.top >= clipped.bottom)
            return true;
        const int bpp = dst.BitsPerPixel() / 8;
        const size_t bytes = static_cast<size_t>(clipped.right - clipped.left) * bpp;
        for (LONG y = clipped.top; y < clipped.bottom; ++y)
        {
            memmove(dst.Row(y) + clipped.left * bpp, src.Row(y + offsetY) + (clipped.left + offsetX) * bpp, bytes);
        }
        return true;
    }

    for (LONG y = clipped.top; y < clipped.bottom; ++y)
    {
        const int sy = srcRect.top + static_cast<int>((static_cast<int64_t>(y - dstRect.top) * srcH) / dstH);
        if (sy < 0 || sy >= src.Height())
            continue;
        for (LONG x = clipped.left; x < clipped.right; ++x)
        {
            const int sx = srcRect.left + static_cast<int>((static_cast<int64_t>(x - dstRect.left) * srcW) / dstW);
            if (sx < 0 || sx >= src.Width())
                continue;
            const uint32_t raw = src.ReadRaw(sx, sy);
            if (keyed && raw == key)
                continue;
            dst.WriteRaw(x, y, sameFormat ? raw : dst.RgbToRaw(src.ReadRgb(sx, sy)));
        }
    }
    return true;
}

bool FillSurfaceRect(Surface& surface, const RECT* rectIn, uint32_t rawColor)
{
    RECT rect = rectIn ? *rectIn : RECT{0, 0, surface.Width(), surface.Height()};
    if (!ClipToSurface(rect, surface))
        return true;
    GdiFlush();
    if (surface.BitsPerPixel() == 32)
    {
        for (LONG y = rect.top; y < rect.bottom; ++y)
            std::fill_n(reinterpret_cast<uint32_t*>(surface.Row(y)) + rect.left, rect.right - rect.left, rawColor);
        return true;
    }
    for (LONG y = rect.top; y < rect.bottom; ++y)
        for (LONG x = rect.left; x < rect.right; ++x)
            surface.WriteRaw(x, y, rawColor);
    return true;
}
