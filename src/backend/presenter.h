// Shows the game's back buffer in its window with Direct3D 11.
//
// The game renders at its own resolution into a system-memory surface. Each frame is converted to 32-bit color
// (palette and gamma ramp applied), uploaded to a texture and drawn into the largest rectangle with the original
// aspect ratio that fits the window's client area, with black bars around it. The display mode is never changed.
#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
class Surface;

class Presenter
{
public:
    ~Presenter();

    bool Create(HWND window);
    void Destroy();
    bool IsCreated() const { return m_swapChain != nullptr; }

    // gamma: 3 x 256 bytes (red, green, blue) or null for none.
    bool Present(const Surface& frame, const uint8_t* gamma, bool waitForVerticalBlank);

    // Where a frame of the given size is drawn inside the client area (letterboxed).
    static RECT FitRect(int clientWidth, int clientHeight, int frameWidth, int frameHeight);

private:
    bool ResizeIfNeeded();
    bool EnsureTexture(int width, int height);
    void Upload(const Surface& frame, const uint8_t* gamma);

    HWND m_window = nullptr;
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    int m_textureWidth = 0;
    int m_textureHeight = 0;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_target = nullptr;
    ID3D11Texture2D* m_texture = nullptr;
    ID3D11ShaderResourceView* m_textureView = nullptr;
    ID3D11VertexShader* m_vertexShader = nullptr;
    ID3D11PixelShader* m_pixelShader = nullptr;
    ID3D11SamplerState* m_sampler = nullptr;
    std::vector<uint32_t> m_converted;
};
