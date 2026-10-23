// Device interface of PC FÃšTBOL 2001's graphics backends (see docs/BACKEND-INTERFACE.md).
//
// gfxwin.dll calls these methods through the vtable, by slot number, with the __thiscall convention: `this` in ECX,
// arguments on the stack, removed by the callee. Every declaration below therefore matches the original backends
// exactly in slot order and in stack argument size. Do not reorder, overload or change parameter counts.
// Methods named SlotNNN have no known purpose yet; their placeholders only preserve the stack size.
//
// Generated from the slot table in docs/BACKEND-INTERFACE.md; edit names and signatures, never the slot order.
#pragma once

#include "interface_types.h"

// clang-format off
class DeviceInterface
{
public:
    /*   0 +0x000 */ virtual ~DeviceInterface() = default;  // scalar deleting destructor
    /*   1 +0x004 */ virtual bool Init(uint32_t want3D, uint32_t backBufferCount, uint32_t fullScreen, HWND window) = 0;  // First call after creation; only stores its arguments
    /*   2 +0x008 */ virtual void Shutdown() = 0;  // Release everything
    /*   3 +0x00c */ virtual void UpdateWindow(HWND window) = 0;  // Windowed mode: the window moved or changed
    /*   4 +0x010 */ virtual void ReleaseDisplay() = 0;  // Let go of the display, e.g. so the 3D backend can take over for a match
    /*   5 +0x014 */ virtual bool IsDisplayReady() = 0;  // True between SetMode and ReleaseDisplay
    /*   6 +0x018 */ virtual uint32_t GetVideoMemory(uint32_t free) = 0;  // Total (free = 0) or free video memory, in bytes
    /*   7 +0x01c */ virtual void SetWindow(HWND window) = 0;
    /*   8 +0x020 */ virtual ModeList* GetModes2D() = 0;
    /*   9 +0x024 */ virtual ModeList* GetModes3D() = 0;  // gfxwin rejects a backend whose 3D list is empty
    /*  10 +0x028 */ virtual bool SetMode(const ModeEntry* mode) = 0;  // Set up the display and the back buffer
    /*  11 +0x02c */ virtual bool SetModeByIndex(int32_t index) = 0;  // SetMode with an entry of the 2D or 3D list
    /*  12 +0x030 */ virtual bool SetFullScreen(uint32_t fullScreen) = 0;
    /*  13 +0x034 */ virtual bool SwitchDisplay(HWND window, uint32_t fullScreen, const ModeEntry* mode) = 0;  // Change between windowed and full screen (Alt+Enter)
    /*  14 +0x038 */ virtual bool GetDesktopMode(ModeEntry* mode) = 0;
    /*  15 +0x03c */ virtual bool PresentRect(const RECT* rect) = 0;  // Show part of the back buffer
    /*  16 +0x040 */ virtual bool Present() = 0;  // Show the back buffer
    /*  17 +0x044 */ virtual bool UseWindowClipper(uint32_t acquire) = 0;  // Reference count for a windowed-mode clipper
    /*  18 +0x048 */ virtual HDC GetSurfaceDC(Surface* surface) = 0;  // GDI drawing (text) into a surface
    /*  19 +0x04c */ virtual void ReleaseSurfaceDC(Surface* surface, HDC dc) = 0;
    /*  20 +0x050 */ virtual bool ConsumeSurfacesLost() = 0;  // True once after surfaces were lost
    /*  21 +0x054 */ virtual Surface* GetBackBuffer() = 0;  // The surface the game draws each frame into
    /*  22 +0x058 */ virtual Surface* CreateSurface(const SurfaceDesc* desc) = 0;
    /*  23 +0x05c */ virtual bool SetRenderTarget(Surface* surface) = 0;  // Point the current drawing target at another surface
    /*  24 +0x060 */ virtual void ReleaseSurface(Surface* surface) = 0;
    /*  25 +0x064 */ virtual SurfaceDesc* GetSurfaceDesc(SurfaceDesc* result, Surface* surface) = 0;  // Returns the description by value (hidden result pointer)
    /*  26 +0x068 */ virtual bool LockAndDescribe(Surface* surface, SurfaceDesc* desc) = 0;
    /*  27 +0x06c */ virtual bool LockSurface(Surface* surface) = 0;
    /*  28 +0x070 */ virtual void UnlockSurface(Surface* surface) = 0;
    /*  29 +0x074 */ virtual void SetSurfacePalette(Surface* surface, const uint32_t* entries) = 0;  // 256 entries, 0x00RRGGBB
    /*  30 +0x078 */ virtual void ApplySurfacePalette(Surface* surface, int32_t first, int32_t count) = 0;
    /*  31 +0x07c */ virtual uint32_t Slot031(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) = 0;
    /*  32 +0x080 */ virtual uint32_t Slot032(uint32_t a1) = 0;
    /*  33 +0x084 */ virtual uint32_t Slot033(uint32_t a1) = 0;
    /*  34 +0x088 */ virtual uint32_t Slot034(uint32_t a1) = 0;
    /*  35 +0x08c */ virtual bool Blit(Surface* dst, const RECT* dstRect, Surface* src, const RECT* srcRect, uint32_t reserved) = 0;  // Same-size copy, honoring the source transparent color
    /*  36 +0x090 */ virtual bool StretchBlit(Surface* dst, const RECT* dstRect, Surface* src, const RECT* srcRect, uint32_t reserved) = 0;
    /*  37 +0x094 */ virtual bool SetFlag37(uint32_t value) = 0;  // Read back by slot 38
    /*  38 +0x098 */ virtual bool GetFlag37() = 0;
    /*  39 +0x09c */ virtual uint32_t Slot039() = 0;
    /*  40 +0x0a0 */ virtual uint32_t Slot040(uint32_t a1, uint32_t a2) = 0;
    /*  41 +0x0a4 */ virtual uint32_t Slot041(uint32_t a1, uint32_t a2) = 0;
    /*  42 +0x0a8 */ virtual void* GetBlock42(uint32_t unused) = 0;  // Pointer to a storage block (3D state)
    /*  43 +0x0ac */ virtual uint32_t Slot043(uint32_t a1) = 0;
    /*  44 +0x0b0 */ virtual uint32_t Slot044() = 0;
    /*  45 +0x0b4 */ virtual uint32_t Slot045(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) = 0;
    /*  46 +0x0b8 */ virtual uint32_t Slot046(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) = 0;
    /*  47 +0x0bc */ virtual uint32_t Slot047(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) = 0;
    /*  48 +0x0c0 */ virtual uint32_t Slot048(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) = 0;
    /*  49 +0x0c4 */ virtual uint32_t Slot049() = 0;
    /*  50 +0x0c8 */ virtual uint32_t Slot050() = 0;
    /*  51 +0x0cc */ virtual uint32_t Slot051() = 0;
    /*  52 +0x0d0 */ virtual uint32_t Slot052() = 0;
    /*  53 +0x0d4 */ virtual uint32_t Slot053(uint32_t a1) = 0;
    /*  54 +0x0d8 */ virtual void SetFlag56() = 0;  // Read back by slot 56
    /*  55 +0x0dc */ virtual bool ClearFlag56() = 0;
    /*  56 +0x0e0 */ virtual bool GetFlag56() = 0;
    /*  57 +0x0e4 */ virtual uint32_t GetValue58() = 0;  // Set by slot 58
    /*  58 +0x0e8 */ virtual bool SetValue58(const uint32_t* value) = 0;
    /*  59 +0x0ec */ virtual TextureFormatList* GetTextureFormats() = 0;
    /*  60 +0x0f0 */ virtual Surface* CreateTexture(const SurfaceDesc* desc, uint32_t reserved1, uint32_t reserved2) = 0;
    /*  61 +0x0f4 */ virtual uint32_t Slot061(uint32_t a1, uint32_t a2, uint32_t a3) = 0;
    /*  62 +0x0f8 */ virtual uint32_t Slot062(uint32_t a1) = 0;
    /*  63 +0x0fc */ virtual bool SetTexture(Surface* texture) = 0;  // Read back by slot 66
    /*  64 +0x100 */ virtual uint32_t Slot064(uint32_t a1) = 0;
    /*  65 +0x104 */ virtual uint32_t Slot065(uint32_t a1, uint32_t a2) = 0;
    /*  66 +0x108 */ virtual Surface* GetTexture() = 0;
    /*  67 +0x10c */ virtual uint32_t Slot067() = 0;
    /*  68 +0x110 */ virtual uint32_t Slot068(uint32_t a1) = 0;
    /*  69 +0x114 */ virtual uint32_t Slot069(uint32_t a1) = 0;
    /*  70 +0x118 */ virtual uint32_t Slot070(uint32_t a1) = 0;
    /*  71 +0x11c */ virtual uint32_t Slot071() = 0;
    /*  72 +0x120 */ virtual uint32_t Slot072(uint32_t a1, uint32_t a2) = 0;
    /*  73 +0x124 */ virtual VertexBuffer* CreateVertexBuffer(const VertexBufferDesc* desc, uint32_t reserved) = 0;
    /*  74 +0x128 */ virtual bool ReleaseVertexBuffer(VertexBuffer* buffer) = 0;
    /*  75 +0x12c */ virtual uint32_t Slot075(uint32_t a1, uint32_t a2) = 0;
    /*  76 +0x130 */ virtual void LockVertexBuffer(VertexBuffer* buffer, VertexBufferDesc* desc) = 0;  // desc receives the description with the data pointer
    /*  77 +0x134 */ virtual uint32_t Slot077(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) = 0;
    /*  78 +0x138 */ virtual void UnlockVertexBuffer(VertexBuffer* buffer) = 0;
    /*  79 +0x13c */ virtual uint32_t Slot079(uint32_t a1) = 0;
    /*  80 +0x140 */ virtual uint32_t Slot080(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) = 0;
    /*  81 +0x144 */ virtual uint32_t Slot081(uint32_t a1) = 0;
    /*  82 +0x148 */ virtual uint32_t Slot082(uint32_t a1) = 0;
    /*  83 +0x14c */ virtual uint32_t Slot083(uint32_t a1, uint32_t a2) = 0;
    /*  84 +0x150 */ virtual uint32_t Slot084(uint32_t a1, uint32_t a2) = 0;
    /*  85 +0x154 */ virtual uint32_t Slot085(uint32_t a1) = 0;
    /*  86 +0x158 */ virtual uint32_t Slot086() = 0;
    /*  87 +0x15c */ virtual uint32_t Slot087() = 0;
    /*  88 +0x160 */ virtual bool SetFlag89(uint32_t value) = 0;  // Read back by slot 89
    /*  89 +0x164 */ virtual bool GetFlag89() = 0;
    /*  90 +0x168 */ virtual uint32_t Slot090(uint32_t a1, uint32_t a2, uint32_t a3) = 0;
    /*  91 +0x16c */ virtual uint32_t Slot091(uint32_t a1) = 0;
    /*  92 +0x170 */ virtual uint32_t Slot092(uint32_t a1, uint32_t a2) = 0;
    /*  93 +0x174 */ virtual uint32_t Slot093(uint32_t a1) = 0;
    /*  94 +0x178 */ virtual uint32_t Slot094(uint32_t a1, uint32_t a2) = 0;
    /*  95 +0x17c */ virtual uint32_t Slot095(uint32_t a1, uint32_t a2) = 0;
    /*  96 +0x180 */ virtual uint32_t Slot096(uint32_t a1, uint32_t a2) = 0;
    /*  97 +0x184 */ virtual uint32_t Slot097(uint32_t a1) = 0;
    /*  98 +0x188 */ virtual uint32_t Slot098(uint32_t a1) = 0;
    /*  99 +0x18c */ virtual bool SetViewport(const uint32_t* viewport) = 0;  // 12 values
    /* 100 +0x190 */ virtual uint32_t Slot100(uint32_t a1) = 0;
    /* 101 +0x194 */ virtual bool GetFlag101() = 0;  // Set by slot 102
    /* 102 +0x198 */ virtual bool SetFlag101(uint32_t value) = 0;
    /* 103 +0x19c */ virtual uint32_t Slot103() = 0;
    /* 104 +0x1a0 */ virtual bool GetFlag104() = 0;  // Set by slot 105
    /* 105 +0x1a4 */ virtual bool SetFlag104(uint32_t value) = 0;
    /* 106 +0x1a8 */ virtual uint32_t Slot106() = 0;
    /* 107 +0x1ac */ virtual bool GetFlag107() = 0;
    /* 108 +0x1b0 */ virtual uint32_t Slot108(uint32_t a1) = 0;
    /* 109 +0x1b4 */ virtual uint32_t Slot109() = 0;
    /* 110 +0x1b8 */ virtual uint32_t GetValue110() = 0;  // Set by slot 111
    /* 111 +0x1bc */ virtual bool SetValue110(uint32_t value) = 0;
    /* 112 +0x1c0 */ virtual uint32_t Slot112(uint32_t a1) = 0;
    /* 113 +0x1c4 */ virtual uint32_t Slot113() = 0;
    /* 114 +0x1c8 */ virtual uint32_t Slot114(uint32_t a1) = 0;
    /* 115 +0x1cc */ virtual uint32_t Slot115() = 0;
    /* 116 +0x1d0 */ virtual uint32_t GetValue116() = 0;  // Set by slot 117
    /* 117 +0x1d4 */ virtual bool SetValue116(uint32_t value) = 0;
    /* 118 +0x1d8 */ virtual uint32_t Slot118(uint32_t a1) = 0;
    /* 119 +0x1dc */ virtual uint32_t GetValue119() = 0;  // Set by slot 120
    /* 120 +0x1e0 */ virtual bool SetValue119(uint32_t value) = 0;
    /* 121 +0x1e4 */ virtual uint32_t Slot121(uint32_t a1) = 0;
    /* 122 +0x1e8 */ virtual uint32_t Slot122() = 0;
    /* 123 +0x1ec */ virtual uint32_t Slot123(uint32_t a1) = 0;
    /* 124 +0x1f0 */ virtual uint32_t GetValue124() = 0;  // Set by slot 125
    /* 125 +0x1f4 */ virtual bool SetValue124(uint32_t value) = 0;
    /* 126 +0x1f8 */ virtual uint32_t Slot126(uint32_t a1) = 0;
    /* 127 +0x1fc */ virtual uint32_t Slot127() = 0;
    /* 128 +0x200 */ virtual uint32_t Slot128(uint32_t a1) = 0;
    /* 129 +0x204 */ virtual uint32_t Slot129() = 0;
    /* 130 +0x208 */ virtual uint32_t Slot130(uint32_t a1) = 0;
    /* 131 +0x20c */ virtual uint32_t Slot131(uint32_t a1) = 0;
    /* 132 +0x210 */ virtual uint32_t Slot132() = 0;
    /* 133 +0x214 */ virtual uint32_t Slot133(uint32_t a1) = 0;
    /* 134 +0x218 */ virtual uint32_t Slot134() = 0;
    /* 135 +0x21c */ virtual uint32_t GetValue135() = 0;
    /* 136 +0x220 */ virtual uint32_t Slot136(uint32_t a1) = 0;
    /* 137 +0x224 */ virtual uint32_t Slot137(uint32_t a1) = 0;
    /* 138 +0x228 */ virtual bool FillRect(Surface* surface, const RECT* rect, uint32_t color) = 0;  // color is a raw value in the surface pixel format
    /* 139 +0x22c */ virtual void SetFlag140(uint32_t value) = 0;  // Read back by slot 140
    /* 140 +0x230 */ virtual bool GetFlag140() = 0;
    /* 141 +0x234 */ virtual uint32_t Slot141() = 0;
    /* 142 +0x238 */ virtual uint32_t Slot142(uint32_t a1) = 0;
    /* 143 +0x23c */ virtual uint32_t Slot143() = 0;
    /* 144 +0x240 */ virtual uint32_t Slot144() = 0;
    /* 145 +0x244 */ virtual uint32_t Slot145(uint32_t a1) = 0;
    /* 146 +0x248 */ virtual uint32_t Slot146(uint32_t a1) = 0;
    /* 147 +0x24c */ virtual uint32_t Slot147() = 0;
    /* 148 +0x250 */ virtual uint32_t Slot148(uint32_t a1) = 0;
    /* 149 +0x254 */ virtual uint32_t Slot149(uint32_t a1, uint32_t a2) = 0;
    /* 150 +0x258 */ virtual uint32_t Slot150(uint32_t a1, uint32_t a2) = 0;
    /* 151 +0x25c */ virtual uint32_t Slot151() = 0;
    /* 152 +0x260 */ virtual uint32_t Slot152() = 0;
    /* 153 +0x264 */ virtual uint32_t Slot153(uint32_t a1, uint32_t a2) = 0;
    /* 154 +0x268 */ virtual uint32_t Slot154(uint32_t a1, uint32_t a2) = 0;
    /* 155 +0x26c */ virtual uint32_t Slot155(uint32_t a1) = 0;
    /* 156 +0x270 */ virtual uint32_t Slot156() = 0;
    /* 157 +0x274 */ virtual uint32_t Slot157() = 0;
    /* 158 +0x278 */ virtual uint32_t Slot158() = 0;
    /* 159 +0x27c */ virtual uint32_t Slot159(uint32_t a1) = 0;
    /* 160 +0x280 */ virtual uint32_t Slot160(uint32_t a1) = 0;
    /* 161 +0x284 */ virtual bool SupportsGamma() = 0;
    /* 162 +0x288 */ virtual bool SupportsGammaCalibration() = 0;
    /* 163 +0x28c */ virtual bool SetGammaRamp(const GammaRamp* ramp, uint32_t calibrate) = 0;
    /* 164 +0x290 */ virtual uint32_t Slot164(uint32_t a1) = 0;
    /* 165 +0x294 */ virtual void* GetBlock165() = 0;  // Pointer to a storage block (3D state)
    /* 166 +0x298 */ virtual void* GetBlock166() = 0;  // Pointer to a storage block (3D state)
    /* 167 +0x29c */ virtual uint32_t Slot167() = 0;
    /* 168 +0x2a0 */ virtual bool GetFlag168() = 0;  // Set by slot 169
    /* 169 +0x2a4 */ virtual void SetFlag168(uint32_t value) = 0;
    /* 170 +0x2a8 */ virtual uint32_t Slot170() = 0;
    /* 171 +0x2ac */ virtual uint32_t Slot171(uint32_t a1) = 0;
    /* 172 +0x2b0 */ virtual uint32_t Slot172(uint32_t a1) = 0;
};
// clang-format on

static_assert(sizeof(void*) == 4, "the backend interface is 32-bit only");
