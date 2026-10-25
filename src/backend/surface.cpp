#include "surface.h"

#include "backend_log.h"
#include "memory_compat.h"

#include <algorithm>
#include <cstring>
#include <intrin.h>
#include <new>

namespace
{
constexpr size_t kSurfaceSlack = 64;  // readable bytes before and after the pixels of every surface (see Allocate)
constexpr uint64_t kAllocationGranularity = 65536;  // where Windows places views of memory sections

// The surfaces alive now (GetSurfaceStats).
volatile LONG g_liveSurfaces;
volatile LONG g_surfacesWithDC;
volatile LONG64 g_livePixelBytes;
volatile LONG64 g_liveViewBytes;

// With [memory] allocation_sites: the game code that created the surfaces alive now (LogSurfaceSites).
constexpr int kSurfaceSiteFrames = 3;  // gfxwin.dll's CGFXSurface::Create, its loader, and who asked for it
constexpr int kSurfaceSites = 4096;    // one more entry collects the rest if they run out
constexpr int kSurfaceSitesListed = 12;

struct SurfaceSite
{
    void* frames[kSurfaceSiteFrames];
    long created;
    long live;
    long listedLive;  // at the previous list
    uint64_t viewBytes;
};

SRWLOCK g_siteLock = SRWLOCK_INIT;
SurfaceSite g_surfaceSites[kSurfaceSites + 1];
int g_surfaceSiteCount;

// The site's entry, added if new. Caller holds g_siteLock.
int FindSurfaceSite(void* const* frames)
{
    for (int i = 0; i < g_surfaceSiteCount; ++i)
    {
        if (memcmp(g_surfaceSites[i].frames, frames, sizeof g_surfaceSites[i].frames) == 0)
            return i;
    }
    if (g_surfaceSiteCount == kSurfaceSites)
        return kSurfaceSites;
    memcpy(g_surfaceSites[g_surfaceSiteCount].frames, frames, sizeof g_surfaceSites[g_surfaceSiteCount].frames);
    return g_surfaceSiteCount++;
}

void LogSurfaceSite(const SurfaceSite& site)
{
    char where[kSurfaceSiteFrames * 48] = "";
    size_t used = 0;
    for (void* frame : site.frames)
    {
        if (!frame)
            break;
        char one[48];
        DescribeCode(frame, one, sizeof one);
        used += _snprintf_s(where + used, sizeof where - used, _TRUNCATE, "%s%s", used ? " < " : "", one);
    }
    BackendLog("surfaces:   %+ld; now %ld alive, %.1f MB of address space, %ld created in all; at %s\n",
               site.live - site.listedLive, site.live, site.viewBytes / (1024.0 * 1024.0), site.created,
               used ? where : "(other sites)");
}

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
    // A surface's pixels need one stretch of free address space; when there is none, the quarantine gives memory back
    // and the surface gets one more try (the game crashes on a surface it doesn't get).
    if (surface && !surface->Allocate(desc) &&
        !(RelieveMemoryPressure("a surface could not be created") && surface->Allocate(desc)))
    {
        delete surface;
        surface = nullptr;
    }
    if (surface && AllocationSitesEnabled())
    {
        void* frames[kSurfaceSiteFrames];
        CaptureGameCallers(_AddressOfReturnAddress(), frames, kSurfaceSiteFrames);
        AcquireSRWLockExclusive(&g_siteLock);
        surface->m_site = FindSurfaceSite(frames);
        surface->m_siteBytes = surface->m_viewBytes;
        SurfaceSite& site = g_surfaceSites[surface->m_site];
        ++site.created;
        ++site.live;
        site.viewBytes += surface->m_siteBytes;
        ReleaseSRWLockExclusive(&g_siteLock);
    }
    return surface;
}

bool Surface::Reset(const SurfaceDesc& desc)
{
    Surface replacement;
    if (!replacement.Allocate(desc))
        return false;
    Free();
    // Take over the new pixels; the palette is kept. A GDI mirror is made again when one is needed.
    m_desc = replacement.m_desc;
    m_block = replacement.m_block;
    m_bits = replacement.m_bits;
    m_stride = replacement.m_stride;
    m_bytesPerPixel = replacement.m_bytesPerPixel;
    m_pixelBytes = replacement.m_pixelBytes;
    m_viewBytes = replacement.m_viewBytes;
    replacement.m_block = nullptr;
    replacement.m_pixelBytes = replacement.m_viewBytes = 0;
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

    // The pixels live in a heap block with slack before and after them, rows 4-byte aligned as in a DIB. The game's
    // drawing code reads a little outside the surfaces it blends from (e.g. Utils.dll's mask fill reads the byte before
    // each row and only then checks bounds); with the original software backends the pixels were heap memory too,
    // where that was harmless.
    const int stride = ((desc.width * desc.bitsPerPixel + 31) / 32) * 4;
    const size_t pixelBytes = static_cast<size_t>(stride) * desc.height;
    void* block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, kSurfaceSlack + pixelBytes + kSurfaceSlack);
    if (!block)
        return false;
    m_block = block;
    m_bits = static_cast<uint8_t*>(block) + kSurfaceSlack;
    m_bytesPerPixel = desc.bitsPerPixel / 8;
    m_stride = stride;
    m_pixelBytes = pixelBytes;
    m_viewBytes = kSurfaceSlack + pixelBytes + kSurfaceSlack;
    InterlockedIncrement(&g_liveSurfaces);
    InterlockedAdd64(&g_livePixelBytes, static_cast<LONG64>(m_pixelBytes));
    InterlockedAdd64(&g_liveViewBytes, static_cast<LONG64>(m_viewBytes));

    desc.pitch = -1;
    desc.pixels = nullptr;
    desc.palette = m_desc.palette;  // keeps an existing palette across Reset
    m_desc = desc;
    return true;
}

// The GDI mirror: a DIB section with the surface's format, for GetDC.
bool Surface::CreateMirror()
{
    DibInfo info{};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = m_desc.width;
    info.header.biHeight = -m_desc.height;  // top-down: row 0 first, positive pitch
    info.header.biPlanes = 1;
    info.header.biBitCount = static_cast<WORD>(m_desc.bitsPerPixel);
    if (m_desc.bitsPerPixel == 16 || m_desc.bitsPerPixel == 32)
    {
        info.header.biCompression = BI_BITFIELDS;
        auto* masks = reinterpret_cast<DWORD*>(info.colors);
        masks[0] = m_desc.redMask;
        masks[1] = m_desc.greenMask;
        masks[2] = m_desc.blueMask;
    }
    else
    {
        info.header.biCompression = BI_RGB;
        if (m_desc.bitsPerPixel == 8)
            info.header.biClrUsed = 256;
    }
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(m_pixelBytes), nullptr);
    if (!section)
        return false;
    void* mirror = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &mirror, section, 0);
    if (!bitmap || !mirror)
    {
        if (bitmap)
            DeleteObject(bitmap);
        CloseHandle(section);
        return false;
    }
    m_section = section;
    m_bitmap = bitmap;
    m_mirror = mirror;
    const uint64_t view = (m_pixelBytes + kAllocationGranularity - 1) / kAllocationGranularity * kAllocationGranularity;
    m_viewBytes += view;
    InterlockedAdd64(&g_liveViewBytes, static_cast<LONG64>(view));
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
        InterlockedDecrement(&g_surfacesWithDC);
    }
    if (m_bitmap)
    {
        DeleteObject(m_bitmap);
        m_bitmap = nullptr;
        m_mirror = nullptr;
    }
    if (m_section)
    {
        CloseHandle(m_section);
        m_section = nullptr;
    }
    if (m_block)
    {
        HeapFree(GetProcessHeap(), 0, m_block);
        m_block = nullptr;
        InterlockedDecrement(&g_liveSurfaces);
        InterlockedAdd64(&g_livePixelBytes, -static_cast<LONG64>(m_pixelBytes));
        InterlockedAdd64(&g_liveViewBytes, -static_cast<LONG64>(m_viewBytes));
    }
    m_bits = nullptr;
}

Surface::~Surface()
{
    if (m_site >= 0)
    {
        AcquireSRWLockExclusive(&g_siteLock);
        --g_surfaceSites[m_site].live;
        g_surfaceSites[m_site].viewBytes -= m_siteBytes;
        ReleaseSRWLockExclusive(&g_siteLock);
    }
    Free();
}

SurfaceStats GetSurfaceStats()
{
    return SurfaceStats{g_liveSurfaces, g_surfacesWithDC, static_cast<uint64_t>(g_livePixelBytes),
                        static_cast<uint64_t>(g_liveViewBytes)};
}

void LogSurfaceSites(bool holders)
{
    AcquireSRWLockExclusive(&g_siteLock);
    int order[kSurfaceSites + 1];
    int count = 0;
    for (int i = 0; i <= kSurfaceSites; ++i)
    {
        if (g_surfaceSites[i].created)
            order[count++] = i;
    }
    const auto growth = [](int i) { return g_surfaceSites[i].live - g_surfaceSites[i].listedLive; };
    std::sort(order, order + count, [&](int a, int b) { return growth(a) > growth(b); });
    BackendLog("surfaces: %d creation sites; grown the most:\n", count);
    for (int k = 0; k < count && k < kSurfaceSitesListed && growth(order[k]) > 0; ++k)
        LogSurfaceSite(g_surfaceSites[order[k]]);
    if (holders)
    {
        std::sort(order, order + count, [](int a, int b) { return g_surfaceSites[a].live > g_surfaceSites[b].live; });
        BackendLog("surfaces: creation sites holding the most:\n");
        for (int k = 0; k < count && k < kSurfaceSitesListed; ++k)
            LogSurfaceSite(g_surfaceSites[order[k]]);
    }
    for (int k = 0; k < count; ++k)
        g_surfaceSites[order[k]].listedLive = g_surfaceSites[order[k]].live;
    ReleaseSRWLockExclusive(&g_siteLock);
}

bool Surface::Lock()
{
    if (!IsLocked())
    {
        // Locked while the game still holds a device context: take what GDI drew so far.
        if (m_dcUsers > 0)
        {
            GdiFlush();
            memcpy(m_bits, m_mirror, static_cast<size_t>(m_pixelBytes));
        }
        m_desc.pitch = m_stride;
        m_desc.pixels = m_bits;
    }
    return true;
}

void Surface::Unlock()
{
    if (IsLocked() && m_dcUsers > 0)
        memcpy(m_mirror, m_bits, static_cast<size_t>(m_pixelBytes));  // and GDI goes on from what the game wrote
    m_desc.pitch = -1;
    m_desc.pixels = nullptr;
}

HDC Surface::GetDC()
{
    if (!m_dc)
    {
        if (!m_bitmap && !CreateMirror())
            return nullptr;
        m_dc = CreateCompatibleDC(nullptr);
        if (!m_dc)
            return nullptr;
        InterlockedIncrement(&g_surfacesWithDC);
        m_previousBitmap = SelectObject(m_dc, m_bitmap);
        if (m_bytesPerPixel == 1)
            ApplyPalette(0, 256);
    }
    // GDI draws into the mirror: it starts from the pixels as they are, and they're copied back on release.
    if (m_dcUsers++ == 0)
        memcpy(m_mirror, m_bits, static_cast<size_t>(m_pixelBytes));
    return m_dc;
}

void Surface::ReleaseDC(HDC dc)
{
    GdiFlush();
    if (dc == m_dc && m_dcUsers > 0 && --m_dcUsers == 0)
        memcpy(m_bits, m_mirror, static_cast<size_t>(m_pixelBytes));
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
