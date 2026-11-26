#pragma once
// D3D11 texture loader using stb_image (header-only, PNG/JPG from memory)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop

#include <d3d11.h>

// Load an RGBA texture from raw PNG/JPG bytes in memory.
// Returns nullptr on failure. Caller must Release() when done.
inline ID3D11ShaderResourceView* LoadTextureFromMemory(
    ID3D11Device* device,
    const unsigned char* data,
    int dataLen,
    int* outW = nullptr,
    int* outH = nullptr)
{
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(data, dataLen, &w, &h, &channels, 4);
    if (!pixels) return nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = static_cast<UINT>(w);
    desc.Height           = static_cast<UINT>(h);
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem     = pixels;
    initData.SysMemPitch = static_cast<UINT>(w * 4);

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &initData, &tex);
    stbi_image_free(pixels);
    if (FAILED(hr) || !tex) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = desc.Format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(tex, &srvDesc, &srv);
    tex->Release();
    if (FAILED(hr)) return nullptr;

    if (outW) *outW = w;
    if (outH) *outH = h;
    return srv;
}
