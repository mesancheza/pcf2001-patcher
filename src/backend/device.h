// The graphics device: our implementation of the game's backend device interface.
//
// It reproduces what the game's original 2D backend (dcsfx.dll, the default) does, without DirectDraw: surfaces live
// in system memory (surface.h), and frames are shown with Direct3D 11 (presenter.h). 3D methods behave as in
// dcsfx.dll, where they are empty: 3D matches run on the game's Direct3D backend (dcd3d.dll), which gfxwin.dll
// loads separately as its 3D backend. See docs/BACKEND-INTERFACE.md.
#pragma once

#include "device_interface.h"
#include "presenter.h"

#include <cstdint>
#include <vector>

class Device final : public DeviceInterface
{
public:
    Device();
    ~Device() override;

    // After DestruirDevice the object is kept as a "zombie" instead of being freed: the game still calls into it
    // while shutting down (see docs/GAME-INTERNALS.md, "Crash on exit"). Zombies do nothing.
    void MakeZombie();

    // --- Device interface (see device_interface.h for the slot numbers) ---
    bool Init(uint32_t want3D, uint32_t backBufferCount, uint32_t fullScreen, HWND window) override;
    void Shutdown() override;
    void UpdateWindow(HWND window) override;
    void ReleaseDisplay() override;
    bool IsDisplayReady() override;
    uint32_t GetVideoMemory(uint32_t free) override;
    void SetWindow(HWND window) override;
    ModeList* GetModes2D() override;
    ModeList* GetModes3D() override;
    bool SetMode(const ModeEntry* mode) override;
    bool SetModeByIndex(int32_t index) override;
    bool SetFullScreen(uint32_t fullScreen) override;
    bool SwitchDisplay(HWND window, uint32_t fullScreen, const ModeEntry* mode) override;
    bool GetDesktopMode(ModeEntry* mode) override;
    bool PresentRect(const RECT* rect) override;
    bool Present() override;
    bool UseWindowClipper(uint32_t acquire) override;
    HDC GetSurfaceDC(Surface* surface) override;
    void ReleaseSurfaceDC(Surface* surface, HDC dc) override;
    bool ConsumeSurfacesLost() override;
    Surface* GetBackBuffer() override;
    Surface* CreateSurface(const SurfaceDesc* desc) override;
    bool SetRenderTarget(Surface* surface) override;
    void ReleaseSurface(Surface* surface) override;
    SurfaceDesc* GetSurfaceDesc(SurfaceDesc* result, Surface* surface) override;
    bool LockAndDescribe(Surface* surface, SurfaceDesc* desc) override;
    bool LockSurface(Surface* surface) override;
    void UnlockSurface(Surface* surface) override;
    void SetSurfacePalette(Surface* surface, const uint32_t* entries) override;
    void ApplySurfacePalette(Surface* surface, int32_t first, int32_t count) override;
    bool Blit(Surface* dst, const RECT* dstRect, Surface* src, const RECT* srcRect, uint32_t reserved) override;
    bool StretchBlit(Surface* dst, const RECT* dstRect, Surface* src, const RECT* srcRect, uint32_t reserved) override;
    bool SetFlag37(uint32_t value) override;
    bool GetFlag37() override;
    void* GetBlock42(uint32_t unused) override;
    void SetFlag56() override;
    bool ClearFlag56() override;
    bool GetFlag56() override;
    uint32_t GetValue58() override;
    bool SetValue58(const uint32_t* value) override;
    TextureFormatList* GetTextureFormats() override;
    Surface* CreateTexture(const SurfaceDesc* desc, uint32_t reserved1, uint32_t reserved2) override;
    bool SetTexture(Surface* texture) override;
    Surface* GetTexture() override;
    VertexBuffer* CreateVertexBuffer(const VertexBufferDesc* desc, uint32_t reserved) override;
    bool ReleaseVertexBuffer(VertexBuffer* buffer) override;
    void LockVertexBuffer(VertexBuffer* buffer, VertexBufferDesc* desc) override;
    void UnlockVertexBuffer(VertexBuffer* buffer) override;
    bool SetFlag89(uint32_t value) override;
    bool GetFlag89() override;
    bool SetViewport(const uint32_t* viewport) override;
    bool GetFlag101() override;
    bool SetFlag101(uint32_t value) override;
    bool GetFlag104() override;
    bool SetFlag104(uint32_t value) override;
    bool GetFlag107() override;
    uint32_t GetValue110() override;
    bool SetValue110(uint32_t value) override;
    uint32_t GetValue116() override;
    bool SetValue116(uint32_t value) override;
    uint32_t GetValue119() override;
    bool SetValue119(uint32_t value) override;
    uint32_t GetValue124() override;
    bool SetValue124(uint32_t value) override;
    uint32_t GetValue135() override;
    bool FillRect(Surface* surface, const RECT* rect, uint32_t color) override;
    void SetFlag140(uint32_t value) override;
    bool GetFlag140() override;
    bool SupportsGamma() override;
    bool SupportsGammaCalibration() override;
    bool SetGammaRamp(const GammaRamp* ramp, uint32_t calibrate) override;
    void* GetBlock165() override;
    void* GetBlock166() override;
    bool GetFlag168() override;
    void SetFlag168(uint32_t value) override;

#include "device_slots.inc"

private:
    // Logs the first call of a method that has no implementation yet, and returns 0.
    uint32_t Unimplemented(int slot, int argCount, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);

    bool ConfigureDisplay(const ModeEntry& mode);
    void EnterBorderlessFullScreen();
    void LeaveBorderlessFullScreen();
    bool ShowFrame();

    bool m_zombie = false;
    bool m_want3D = false;
    bool m_fullScreen = false;
    bool m_displayReady = false;
    uint32_t m_backBufferCount = 0;
    HWND m_window = nullptr;

    ModeEntry m_currentMode{};
    std::vector<ModeEntry> m_modes;
    ModeList m_modeList{};
    TextureFormat m_textureFormat{};
    TextureFormatList m_textureFormats{};

    Surface* m_backBuffer = nullptr;
    Surface* m_renderTarget = nullptr;
    Presenter m_presenter;

    // Window placement saved while showing full screen as a borderless window.
    bool m_borderless = false;
    LONG_PTR m_savedStyle = 0;
    LONG_PTR m_savedExStyle = 0;
    RECT m_savedRect{};

    bool m_gammaActive = false;
    uint8_t m_gamma[3 * 256]{};

    int m_clipperUsers = 0;
    unsigned m_presentsSinceReport = 0;
    DWORD m_lastPresentReport = GetTickCount();

    // Values stored and read back by 3D-state methods. Defaults are those of the original backend.
    bool m_flag37 = false;
    bool m_flag56 = true;
    uint32_t m_value58 = 0;
    Surface* m_texture = nullptr;
    bool m_flag89 = false;
    bool m_flag101 = true;
    bool m_flag104 = false;
    uint32_t m_value110 = 0;
    uint32_t m_value116 = 2;
    uint32_t m_value119 = 0;
    uint32_t m_value124 = 0;
    bool m_flag140 = false;
    bool m_flag168 = true;
    uint32_t m_viewport[12]{};
    uint8_t m_block42[256]{};
    uint8_t m_block165[64]{};
    uint8_t m_block166[64]{};
    bool m_logged[173]{};
};

// Vertex buffers: the 2D path fills them but never draws them, so plain memory is enough.
class VertexBuffer
{
public:
    VertexBufferDesc desc{};
    std::vector<uint8_t> data;
};
