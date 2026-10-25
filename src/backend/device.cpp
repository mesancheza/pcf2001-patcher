#include "device.h"

#include "backend_log.h"
#include "diagnostics.h"
#include "input_mapping.h"
#include "memory_compat.h"
#include "surface.h"

#include <cstring>
#include <new>

namespace
{
constexpr uint32_t kVideoMemory = 128u * 1024 * 1024;

// Frame sizes offered to the game. Its 2D screens use 800x600; the others exist for options that may ask for them.
constexpr int kFrameSizes[][2] = {{640, 480}, {800, 600}, {1024, 768}};

ModeEntry MakeMode(int width, int height, int bitsPerPixel)
{
    ModeEntry mode{};
    mode.bitsPerPixel = bitsPerPixel;
    if (bitsPerPixel == 16)
    {
        mode.redMask = 0xF800;
        mode.greenMask = 0x07E0;
        mode.blueMask = 0x001F;
    }
    else
    {
        mode.redMask = 0xFF0000;
        mode.greenMask = 0x00FF00;
        mode.blueMask = 0x0000FF;
    }
    mode.width = width;
    mode.height = height;
    return mode;
}

SurfaceDesc DescForMode(const ModeEntry& mode)
{
    SurfaceDesc desc{};
    desc.width = static_cast<int32_t>(mode.width);
    desc.height = static_cast<int32_t>(mode.height);
    desc.bitsPerPixel = static_cast<int32_t>(mode.bitsPerPixel);
    desc.redMask = mode.redMask;
    desc.greenMask = mode.greenMask;
    desc.blueMask = mode.blueMask;
    desc.alphaMask = 0;
    desc.colorKey = kNoColorKey;
    return desc;
}
}  // namespace

Device::Device()
{
    // 32-bit modes first: gfxwin takes the first entry that matches a request for "any of 16/24/32 bits".
    for (int bits : {32, 16})
        for (const auto& size : kFrameSizes)
            m_modes.push_back(MakeMode(size[0], size[1], bits));
    m_modeList.entries = m_modes.data();
    m_modeList.count = static_cast<int32_t>(m_modes.size());

    // Like the original backend, offer only the display format, without alpha.
    m_textureFormat = TextureFormat{32, 0xFF0000, 0x00FF00, 0x0000FF, 0};
    m_textureFormats.entries = &m_textureFormat;
    m_textureFormats.count = 1;
}

Device::~Device()
{
    Shutdown();
    delete m_backBuffer;
}

void Device::MakeZombie()
{
    Shutdown();
    m_zombie = true;
}

uint32_t Device::Unimplemented(int slot, int argCount, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    if (slot >= 0 && slot < 173 && !m_logged[slot])
    {
        m_logged[slot] = true;
        BackendLog("slot %d (+0x%03x) called but not implemented, %d args: %08x %08x %08x %08x\n", slot, slot * 4,
                   argCount, a1, a2, a3, a4);
    }
    return 0;
}

// --- Setup and display ---

bool Device::Init(uint32_t want3D, uint32_t backBufferCount, uint32_t fullScreen, HWND window)
{
    m_want3D = (want3D & 0xFF) != 0;
    m_backBufferCount = backBufferCount;
    m_fullScreen = (fullScreen & 0xFF) != 0;
    m_window = window;
    BackendLog("Init: want3D=%d backBuffers=%u fullScreen=%d window=%p\n", m_want3D, backBufferCount, m_fullScreen, window);
    return !m_zombie;
}

void Device::Shutdown()
{
    ReleaseDisplay();
    m_renderTarget = nullptr;
    m_texture = nullptr;
}

void Device::UpdateWindow(HWND window)
{
    if (!m_fullScreen && window)
        m_window = window;  // the presenter follows the client size on its own
}

void Device::ReleaseDisplay()
{
    // Called when the 3D backend takes over the window for a match, and on shutdown: let go of the window entirely.
    if (m_displayReady)
        BackendLog("ReleaseDisplay\n");
    m_presenter.Destroy();
    SetInputFrameSize(0, 0);
    UninstallInputMapping();
    LeaveBorderlessFullScreen();
    m_displayReady = false;
}

bool Device::IsDisplayReady()
{
    return m_displayReady;
}

uint32_t Device::GetVideoMemory(uint32_t free)
{
    (void)free;
    return kVideoMemory;
}

void Device::SetWindow(HWND window)
{
    m_window = window;
}

ModeList* Device::GetModes2D()
{
    return &m_modeList;
}

ModeList* Device::GetModes3D()
{
    // gfxwin only accepts a backend that lists 3D modes; 3D drawing itself is done by the game's 3D backend.
    return &m_modeList;
}

bool Device::SetMode(const ModeEntry* mode)
{
    if (m_zombie || !mode)
        return false;
    return ConfigureDisplay(*mode);
}

bool Device::SetModeByIndex(int32_t index)
{
    if (index < 0 || index >= m_modeList.count)
        return false;
    return SetMode(&m_modeList.entries[index]);
}

bool Device::SetFullScreen(uint32_t fullScreen)
{
    m_fullScreen = (fullScreen & 0xFF) != 0;
    return true;
}

bool Device::SwitchDisplay(HWND window, uint32_t fullScreen, const ModeEntry* mode)
{
    if (m_zombie)
        return false;
    if (window && window != m_window)
    {
        m_presenter.Destroy();
        UninstallInputMapping();
        m_window = window;
    }
    m_fullScreen = (fullScreen & 0xFF) != 0;
    return ConfigureDisplay(mode ? *mode : m_currentMode);
}

bool Device::GetDesktopMode(ModeEntry* mode)
{
    if (!mode)
        return false;
    *mode = MakeMode(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 32);
    return true;
}

bool Device::ConfigureDisplay(const ModeEntry& mode)
{
    if (!m_window || mode.width == 0 || mode.height == 0)
        return false;
    m_currentMode = mode;
    InstallMemoryCompat();  // covers the game's 3D backend, which is loaded after this DLL

    const SurfaceDesc desc = DescForMode(mode);
    if (!m_backBuffer)
        m_backBuffer = Surface::Create(desc, false);
    else if (m_backBuffer->Width() != desc.width || m_backBuffer->Height() != desc.height ||
             m_backBuffer->BitsPerPixel() != desc.bitsPerPixel)
        m_backBuffer->Reset(desc);
    if (!m_backBuffer)
    {
        BackendLog("SetMode: cannot create a %ux%u back buffer\n", mode.width, mode.height);
        return false;
    }

    if (m_fullScreen)
        EnterBorderlessFullScreen();
    else
        LeaveBorderlessFullScreen();

    if (!m_presenter.IsCreated() && !m_presenter.Create(m_window))
        return false;
    InstallInputMapping(m_window);
    SetInputFrameSize(static_cast<int>(mode.width), static_cast<int>(mode.height));

    m_displayReady = true;
    BackendLog("SetMode: %ux%u, %u bits, %s\n", mode.width, mode.height, mode.bitsPerPixel,
               m_fullScreen ? "full screen (borderless window)" : "windowed");
    return true;
}

void Device::EnterBorderlessFullScreen()
{
    if (!m_borderless)
    {
        m_savedStyle = GetWindowLongPtrA(m_window, GWL_STYLE);
        m_savedExStyle = GetWindowLongPtrA(m_window, GWL_EXSTYLE);
        GetWindowRect(m_window, &m_savedRect);
        m_borderless = true;
    }
    MONITORINFO monitor{sizeof monitor};
    GetMonitorInfoA(MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& r = monitor.rcMonitor;
    SetWindowLongPtrA(m_window, GWL_STYLE, (m_savedStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME)) | WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrA(m_window, GWL_EXSTYLE, m_savedExStyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));
    SetForcedWindowRect(&r);  // the game would otherwise move the window back to its mode size
    // Not "always on top" (gfxwin asks for it in full screen), or the game would cover every other window even
    // after the player switches away from it.
    SetWindowPos(m_window, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void Device::LeaveBorderlessFullScreen()
{
    if (!m_borderless || !m_window)
        return;
    SetForcedWindowRect(nullptr);
    SetWindowLongPtrA(m_window, GWL_STYLE, m_savedStyle);
    SetWindowLongPtrA(m_window, GWL_EXSTYLE, m_savedExStyle);
    SetWindowPos(m_window, nullptr, m_savedRect.left, m_savedRect.top, m_savedRect.right - m_savedRect.left,
                 m_savedRect.bottom - m_savedRect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
    m_borderless = false;
}

bool Device::ShowFrame()
{
    if (m_zombie || !m_displayReady || !m_backBuffer)
        return false;

    // Presents per interval in the log help tell a busy game (e.g. simulating a whole match day) from a hung one.
    ++m_presentsSinceReport;
    const DWORD now = GetTickCount();
    if (now - m_lastPresentReport >= 30000)
    {
        BackendLog("%u frames presented in the last %.0f s\n", m_presentsSinceReport, (now - m_lastPresentReport) / 1000.0);
        LogMemoryCompatStats();
        LogProcessMemory();
        m_presentsSinceReport = 0;
        m_lastPresentReport = now;
    }

    // Safety net for the window procedure's check (input_mapping.cpp): whatever made the window topmost, undo it.
    if (m_borderless && (GetWindowLongPtrA(m_window, GWL_EXSTYLE) & WS_EX_TOPMOST))
    {
        if (!m_loggedTopmost)
            BackendLog("window was made topmost; making it a normal window again\n");
        m_loggedTopmost = true;
        SetWindowPos(m_window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // Never wait for the vertical blank: the game redraws progress screens while it computes (e.g. after each simulated
    // match), and waiting there would slow those loops down to the monitor's refresh rate.
    return m_presenter.Present(*m_backBuffer, m_gammaActive ? m_gamma : nullptr, false);
}

bool Device::PresentRect(const RECT* rect)
{
    (void)rect;  // the whole frame is uploaded every time
    return ShowFrame();
}

bool Device::Present()
{
    return ShowFrame();
}

bool Device::UseWindowClipper(uint32_t acquire)
{
    m_clipperUsers += (acquire & 0xFF) ? 1 : -1;
    return true;
}

bool Device::ConsumeSurfacesLost()
{
    return false;  // system-memory surfaces are never lost
}

// --- Surfaces ---

HDC Device::GetSurfaceDC(Surface* surface)
{
    return surface ? surface->GetDC() : nullptr;
}

void Device::ReleaseSurfaceDC(Surface* surface, HDC dc)
{
    if (surface)
        surface->ReleaseDC(dc);
}

Surface* Device::GetBackBuffer()
{
    return m_backBuffer;
}

Surface* Device::CreateSurface(const SurfaceDesc* desc)
{
    if (m_zombie || !desc)
        return nullptr;
    Surface* surface = Surface::Create(*desc, false);
    if (!surface)
        BackendLog("CreateSurface failed: %dx%d, %d bits\n", desc->width, desc->height, desc->bitsPerPixel);
    return surface;
}

bool Device::SetRenderTarget(Surface* surface)
{
    m_renderTarget = surface;
    return true;
}

void Device::ReleaseSurface(Surface* surface)
{
    if (!surface || surface == m_backBuffer)
        return;  // the back buffer belongs to the device
    if (surface == m_renderTarget)
        m_renderTarget = nullptr;
    if (surface == m_texture)
        m_texture = nullptr;
    delete surface;
}

SurfaceDesc* Device::GetSurfaceDesc(SurfaceDesc* result, Surface* surface)
{
    if (result)
    {
        if (surface)
            *result = surface->Desc();
        else
            memset(result, 0, sizeof *result);
    }
    return result;
}

bool Device::LockAndDescribe(Surface* surface, SurfaceDesc* desc)
{
    if (!surface)
        return true;
    const bool locked = surface->Lock();
    if (desc)
        *desc = surface->Desc();
    return locked;
}

bool Device::LockSurface(Surface* surface)
{
    return surface ? surface->Lock() : true;
}

void Device::UnlockSurface(Surface* surface)
{
    if (surface)
        surface->Unlock();
}

void Device::SetSurfacePalette(Surface* surface, const uint32_t* entries)
{
    if (surface)
        surface->SetPalette(entries);
}

void Device::ApplySurfacePalette(Surface* surface, int32_t first, int32_t count)
{
    if (surface)
        surface->ApplyPalette(first, count);
}

bool Device::Blit(Surface* dst, const RECT* dstRect, Surface* src, const RECT* srcRect, uint32_t reserved)
{
    (void)reserved;
    return dst && src && CopyRect(*dst, dstRect, *src, srcRect);
}

bool Device::StretchBlit(Surface* dst, const RECT* dstRect, Surface* src, const RECT* srcRect, uint32_t reserved)
{
    (void)reserved;
    return dst && src && CopyRect(*dst, dstRect, *src, srcRect);
}

bool Device::FillRect(Surface* surface, const RECT* rect, uint32_t color)
{
    return surface && FillSurfaceRect(*surface, rect, color);
}

// --- Textures and vertex buffers (filled by the game, never drawn by the 2D backend) ---

TextureFormatList* Device::GetTextureFormats()
{
    return &m_textureFormats;
}

Surface* Device::CreateTexture(const SurfaceDesc* desc, uint32_t reserved1, uint32_t reserved2)
{
    (void)reserved1;
    (void)reserved2;
    if (m_zombie || !desc)
        return nullptr;
    return Surface::Create(*desc, true);
}

bool Device::SetTexture(Surface* texture)
{
    m_texture = texture;
    return true;
}

bool Device::SetSurfaceColorKey(Surface* surface, uint32_t colorKey)
{
    if (!surface)
        return false;
    surface->SetColorKey(colorKey);
    return true;
}

Surface* Device::GetTexture()
{
    return m_texture;
}

VertexBuffer* Device::CreateVertexBuffer(const VertexBufferDesc* desc, uint32_t reserved)
{
    (void)reserved;
    if (m_zombie || !desc)
        return nullptr;
    auto* buffer = new (std::nothrow) VertexBuffer();
    if (!buffer)
        return nullptr;
    buffer->desc = *desc;
    buffer->desc.data = nullptr;
    // 64 bytes per vertex covers every vertex layout the game uses (the largest is under 48 bytes).
    buffer->data.assign(static_cast<size_t>(desc->vertexCount) * 64 + 64, 0);
    return buffer;
}

bool Device::ReleaseVertexBuffer(VertexBuffer* buffer)
{
    delete buffer;
    return true;
}

void Device::LockVertexBuffer(VertexBuffer* buffer, VertexBufferDesc* desc)
{
    if (!buffer)
        return;
    buffer->desc.data = buffer->data.data();
    if (desc)
        *desc = buffer->desc;
}

void Device::UnlockVertexBuffer(VertexBuffer* buffer)
{
    if (buffer)
        buffer->desc.data = nullptr;
}

bool Device::SetViewport(const uint32_t* viewport)
{
    if (viewport)
        memcpy(m_viewport, viewport, sizeof m_viewport);
    return true;
}

// --- Gamma (used for fades) ---

bool Device::SupportsGamma()
{
    return m_fullScreen;  // as the original: gamma ramps were a full-screen feature
}

bool Device::SupportsGammaCalibration()
{
    return false;
}

bool Device::SetGammaRamp(const GammaRamp* ramp, uint32_t calibrate)
{
    (void)calibrate;
    if (!ramp)
        return false;
    bool identity = true;
    for (int i = 0; i < 256; ++i)
    {
        m_gamma[i] = static_cast<uint8_t>(ramp->red[i] >> 8);
        m_gamma[256 + i] = static_cast<uint8_t>(ramp->green[i] >> 8);
        m_gamma[512 + i] = static_cast<uint8_t>(ramp->blue[i] >> 8);
        identity = identity && m_gamma[i] == i && m_gamma[256 + i] == i && m_gamma[512 + i] == i;
    }
    m_gammaActive = !identity;
    return true;
}

// --- Stored values (3D state the 2D path only stores and reads back) ---

bool Device::SetFlag37(uint32_t value)
{
    m_flag37 = (value & 0xFF) != 0;
    return true;
}
bool Device::GetFlag37() { return m_flag37; }
void* Device::GetBlock42(uint32_t unused)
{
    (void)unused;
    return m_block42;
}
void Device::SetFlag56() { m_flag56 = true; }
bool Device::ClearFlag56()
{
    m_flag56 = false;
    return true;
}
bool Device::GetFlag56() { return m_flag56; }
uint32_t Device::GetValue58() { return m_value58; }
bool Device::SetValue58(const uint32_t* value)
{
    if (value)
        m_value58 = *value;
    return true;
}
bool Device::SetFlag89(uint32_t value)
{
    m_flag89 = (value & 0xFF) != 0;
    return true;
}
bool Device::GetFlag89() { return m_flag89; }
bool Device::GetFlag101() { return m_flag101; }
bool Device::SetFlag101(uint32_t value)
{
    m_flag101 = (value & 0xFF) != 0;
    return true;
}
bool Device::GetFlag104() { return m_flag104; }
bool Device::SetFlag104(uint32_t value)
{
    m_flag104 = (value & 0xFF) != 0;
    return true;
}
bool Device::GetFlag107() { return false; }
uint32_t Device::GetValue110() { return m_value110; }
bool Device::SetValue110(uint32_t value)
{
    m_value110 = value;
    return true;
}
uint32_t Device::GetValue116() { return m_value116; }
bool Device::SetValue116(uint32_t value)
{
    m_value116 = value;
    return true;
}
uint32_t Device::GetValue119() { return m_value119; }
bool Device::SetValue119(uint32_t value)
{
    m_value119 = value;
    return true;
}
uint32_t Device::GetValue124() { return m_value124; }
bool Device::SetValue124(uint32_t value)
{
    m_value124 = value;
    return true;
}
uint32_t Device::GetValue135() { return 0; }
void Device::SetFlag140(uint32_t value) { m_flag140 = (value & 0xFF) != 0; }
bool Device::GetFlag140() { return m_flag140; }
void* Device::GetBlock165() { return m_block165; }
void* Device::GetBlock166() { return m_block166; }
bool Device::GetFlag168() { return m_flag168; }
void Device::SetFlag168(uint32_t value)
{
    if (m_fullScreen)
        m_flag168 = (value & 0xFF) != 0;
}
