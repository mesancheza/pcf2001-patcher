#include "presenter.h"

#include "backend_log.h"
#include "surface.h"

#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <cstring>

#include "present_ps.h"  // generated from present.hlsl at build time
#include "present_vs.h"

namespace
{
template <typename T>
void Release(T*& object)
{
    if (object)
    {
        object->Release();
        object = nullptr;
    }
}

void ClientSize(HWND window, int& width, int& height)
{
    RECT client{};
    ::GetClientRect(window, &client);
    width = std::max<LONG>(client.right - client.left, 1);
    height = std::max<LONG>(client.bottom - client.top, 1);
}
}  // namespace

Presenter::~Presenter()
{
    Destroy();
}

bool Presenter::Create(HWND window)
{
    Destroy();
    m_window = window;
    ClientSize(window, m_bufferWidth, m_bufferHeight);

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = m_bufferWidth;
    desc.BufferDesc.Height = m_bufferHeight;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;  // always: full screen is a borderless window, never an exclusive mode change
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, ARRAYSIZE(levels),
                                               D3D11_SDK_VERSION, &desc, &m_swapChain, &m_device, nullptr, &m_context);
    if (FAILED(hr))
    {
        BackendLog("Direct3D 11 hardware device unavailable (0x%08lx), using WARP\n", hr);
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, ARRAYSIZE(levels),
                                           D3D11_SDK_VERSION, &desc, &m_swapChain, &m_device, nullptr, &m_context);
    }
    if (FAILED(hr))
    {
        BackendLog("cannot create a Direct3D 11 device (0x%08lx)\n", hr);
        Destroy();
        return false;
    }

    // Which graphics card and driver level a player's log was written with.
    static bool adapterLogged;
    IDXGIDevice* dxgiDevice = nullptr;
    if (!adapterLogged && SUCCEEDED(m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))))
    {
        IDXGIAdapter* adapter = nullptr;
        DXGI_ADAPTER_DESC adapterDesc{};
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&adapterDesc)))
        {
            char name[128];
            WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, name, sizeof name, nullptr, nullptr);
            BackendLog("graphics: %s, feature level %u.%u, %u MB video memory\n", name, static_cast<unsigned>(m_device->GetFeatureLevel() >> 12),
                       static_cast<unsigned>((m_device->GetFeatureLevel() >> 8) & 0xF), static_cast<unsigned>(adapterDesc.DedicatedVideoMemory >> 20));
            adapterLogged = true;
        }
        if (adapter)
            adapter->Release();
        dxgiDevice->Release();
    }

    // The game handles Alt+Enter and window changes itself.
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(m_swapChain->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))))
    {
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }

    ID3D11Texture2D* backBuffer = nullptr;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (SUCCEEDED(hr))
    {
        hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_target);
        backBuffer->Release();
    }
    if (SUCCEEDED(hr))
        hr = m_device->CreateVertexShader(g_presentVS, sizeof g_presentVS, nullptr, &m_vertexShader);
    if (SUCCEEDED(hr))
        hr = m_device->CreatePixelShader(g_presentPS, sizeof g_presentPS, nullptr, &m_pixelShader);
    if (SUCCEEDED(hr))
    {
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        hr = m_device->CreateSamplerState(&sampler, &m_sampler);
    }
    if (FAILED(hr))
    {
        BackendLog("cannot set up the Direct3D 11 pipeline (0x%08lx)\n", hr);
        Destroy();
        return false;
    }
    BackendLog("presenter ready, %dx%d window\n", m_bufferWidth, m_bufferHeight);
    return true;
}

void Presenter::Destroy()
{
    if (m_context)
    {
        m_context->ClearState();
        m_context->Flush();
    }
    Release(m_sampler);
    Release(m_pixelShader);
    Release(m_vertexShader);
    Release(m_textureView);
    Release(m_texture);
    Release(m_target);
    Release(m_swapChain);
    Release(m_context);
    Release(m_device);
    m_textureWidth = m_textureHeight = 0;
    m_window = nullptr;
}

RECT Presenter::FitRect(int clientWidth, int clientHeight, int frameWidth, int frameHeight)
{
    if (frameWidth <= 0 || frameHeight <= 0)
        return RECT{0, 0, clientWidth, clientHeight};
    // Largest scale that fits, keeping the aspect ratio. Integer math keeps the result exact.
    int width = clientWidth;
    int height = static_cast<int>(static_cast<int64_t>(clientWidth) * frameHeight / frameWidth);
    if (height > clientHeight)
    {
        height = clientHeight;
        width = static_cast<int>(static_cast<int64_t>(clientHeight) * frameWidth / frameHeight);
    }
    const int left = (clientWidth - width) / 2;
    const int top = (clientHeight - height) / 2;
    return RECT{left, top, left + width, top + height};
}

bool Presenter::ResizeIfNeeded()
{
    int width, height;
    ClientSize(m_window, width, height);
    if (width == m_bufferWidth && height == m_bufferHeight)
        return true;
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    Release(m_target);
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        return false;
    ID3D11Texture2D* backBuffer = nullptr;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (SUCCEEDED(hr))
    {
        hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_target);
        backBuffer->Release();
    }
    m_bufferWidth = width;
    m_bufferHeight = height;
    return SUCCEEDED(hr);
}

bool Presenter::EnsureTexture(int width, int height)
{
    if (m_texture && width == m_textureWidth && height == m_textureHeight)
        return true;
    Release(m_textureView);
    Release(m_texture);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_texture)) ||
        FAILED(m_device->CreateShaderResourceView(m_texture, nullptr, &m_textureView)))
    {
        Release(m_texture);
        return false;
    }
    m_textureWidth = width;
    m_textureHeight = height;
    return true;
}

void Presenter::Upload(const Surface& frame, const uint8_t* gamma)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    GdiFlush();
    const int width = frame.Width();
    const int height = frame.Height();
    const SurfaceDesc& desc = frame.Desc();
    const bool directCopy = !gamma && frame.BitsPerPixel() == 32 && desc.redMask == 0xFF0000 &&
                            desc.greenMask == 0x00FF00 && desc.blueMask == 0x0000FF;
    for (int y = 0; y < height; ++y)
    {
        auto* out = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch);
        if (directCopy)
        {
            memcpy(out, frame.Row(y), static_cast<size_t>(width) * 4);  // alpha is ignored by the pixel shader
            continue;
        }
        for (int x = 0; x < width; ++x)
        {
            uint32_t rgb = frame.ReadRgb(x, y);
            if (gamma)
                rgb = (gamma[(rgb >> 16) & 0xFF] << 16) | (gamma[256 + ((rgb >> 8) & 0xFF)] << 8) | gamma[512 + (rgb & 0xFF)];
            out[x] = 0xFF000000u | rgb;
        }
    }
    m_context->Unmap(m_texture, 0);
}

bool Presenter::Present(const Surface& frame, const uint8_t* gamma, bool waitForVerticalBlank)
{
    if (!m_swapChain || !ResizeIfNeeded() || !EnsureTexture(frame.Width(), frame.Height()))
        return false;
    Upload(frame, gamma);

    const float black[4] = {0, 0, 0, 1};
    m_context->OMSetRenderTargets(1, &m_target, nullptr);
    m_context->ClearRenderTargetView(m_target, black);

    const RECT fit = FitRect(m_bufferWidth, m_bufferHeight, frame.Width(), frame.Height());
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = static_cast<float>(fit.left);
    viewport.TopLeftY = static_cast<float>(fit.top);
    viewport.Width = static_cast<float>(fit.right - fit.left);
    viewport.Height = static_cast<float>(fit.bottom - fit.top);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->IASetInputLayout(nullptr);
    m_context->VSSetShader(m_vertexShader, nullptr, 0);
    m_context->PSSetShader(m_pixelShader, nullptr, 0);
    m_context->PSSetShaderResources(0, 1, &m_textureView);
    m_context->PSSetSamplers(0, 1, &m_sampler);
    m_context->Draw(3, 0);

    const HRESULT hr = m_swapChain->Present(waitForVerticalBlank ? 1 : 0, 0);
    return SUCCEEDED(hr);
}
