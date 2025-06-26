#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <iostream>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

using namespace DirectX;

const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;

struct Vertex {
    XMFLOAT3 position;
    XMFLOAT2 texCoord;
};

// 顶点着色器源码
const char* vertexShaderSource = R"(
cbuffer ConstantBuffer : register(b0)
{
    matrix worldViewProj;
}

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProj);
    output.texCoord = input.texCoord;
    return output;
}
)";

// 像素着色器源码  
const char* pixelShaderSource = R"(
Texture2D mainTexture : register(t0);
SamplerState mainSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return mainTexture.Sample(mainSampler, input.texCoord);
}
)";

class D3D11TextureApp {
private:
    HWND hWnd;
    ID3D11Device* device;
    ID3D11DeviceContext* deviceContext;
    IDXGISwapChain* swapChain;
    ID3D11RenderTargetView* renderTargetView;
    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;
    ID3D11InputLayout* inputLayout;
    ID3D11Buffer* vertexBuffer;
    ID3D11Buffer* indexBuffer;
    ID3D11Buffer* constantBuffer;
    ID3D11Texture2D* texture;
    ID3D11ShaderResourceView* textureView;
    ID3D11SamplerState* sampler;
    D3D11_VIEWPORT viewport;

    // 顶点数据
    std::vector<Vertex> vertices = {
        { XMFLOAT3(-0.5f, -0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3( 0.5f, -0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3( 0.5f,  0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) }
    };

    std::vector<UINT> indices = { 0, 1, 2, 2, 3, 0 };

    struct ConstantBufferData {
        XMMATRIX worldViewProj;
    };

public:
    bool Initialize(HINSTANCE hInstance) {
        if (!CreateWindow(hInstance)) return false;
        if (!InitializeD3D()) return false;
        if (!CreateShaders()) return false;
        if (!CreateGeometry()) return false;
        if (!CreateTexture()) return false;
        return true;
    }

    void Run() {
        MSG msg = {};
        while (WM_QUIT != msg.message) {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            Render();
        }
    }

    void Cleanup() {
        if (sampler) sampler->Release();
        if (textureView) textureView->Release();
        if (texture) texture->Release();
        if (constantBuffer) constantBuffer->Release();
        if (indexBuffer) indexBuffer->Release();
        if (vertexBuffer) vertexBuffer->Release();
        if (inputLayout) inputLayout->Release();
        if (pixelShader) pixelShader->Release();
        if (vertexShader) vertexShader->Release();
        if (renderTargetView) renderTargetView->Release();
        if (swapChain) swapChain->Release();
        if (deviceContext) deviceContext->Release();
        if (device) device->Release();
    }

private:
    bool CreateWindow(HINSTANCE hInstance) {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"D3D11TextureWindow";

        if (!RegisterClassEx(&wc)) return false;

        RECT wr = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

        hWnd = CreateWindowEx(
            0,
            L"D3D11TextureWindow",
            L"DirectX 11 纹理渲染示例",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            wr.right - wr.left,
            wr.bottom - wr.top,
            nullptr, nullptr, hInstance, nullptr
        );

        if (!hWnd) return false;

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
        return true;
    }

    bool InitializeD3D() {
        // 创建设备和交换链
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 1;
        swapChainDesc.BufferDesc.Width = WINDOW_WIDTH;
        swapChainDesc.BufferDesc.Height = WINDOW_HEIGHT;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
        swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = hWnd;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_DEBUG,
            nullptr, 0,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &swapChain,
            &device,
            &featureLevel,
            &deviceContext
        );

        if (FAILED(hr)) {
            std::cout << "创建D3D11设备和交换链失败!" << std::endl;
            return false;
        }

        // 创建渲染目标视图
        ID3D11Texture2D* backBuffer;
        hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr)) return false;

        hr = device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
        backBuffer->Release();
        if (FAILED(hr)) return false;

        deviceContext->OMSetRenderTargets(1, &renderTargetView, nullptr);

        // 设置视口
        viewport.Width = (float)WINDOW_WIDTH;
        viewport.Height = (float)WINDOW_HEIGHT;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        deviceContext->RSSetViewports(1, &viewport);

        return true;
    }

    bool CreateShaders() {
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;

        // 编译顶点着色器
        HRESULT hr = D3DCompile(
            vertexShaderSource,
            strlen(vertexShaderSource),
            nullptr, nullptr, nullptr,
            "main", "vs_5_0",
            D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
            0, &vsBlob, &errorBlob
        );

        if (FAILED(hr)) {
            if (errorBlob) {
                std::cout << "顶点着色器编译错误: " << (char*)errorBlob->GetBufferPointer() << std::endl;
                errorBlob->Release();
            }
            return false;
        }

        hr = device->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr, &vertexShader
        );

        if (FAILED(hr)) {
            vsBlob->Release();
            return false;
        }

        // 创建输入布局
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        hr = device->CreateInputLayout(
            layout, 2,
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            &inputLayout
        );

        vsBlob->Release();
        if (FAILED(hr)) return false;

        // 编译像素着色器
        ID3DBlob* psBlob = nullptr;
        hr = D3DCompile(
            pixelShaderSource,
            strlen(pixelShaderSource),
            nullptr, nullptr, nullptr,
            "main", "ps_5_0",
            D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
            0, &psBlob, &errorBlob
        );

        if (FAILED(hr)) {
            if (errorBlob) {
                std::cout << "像素着色器编译错误: " << (char*)errorBlob->GetBufferPointer() << std::endl;
                errorBlob->Release();
            }
            return false;
        }

        hr = device->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr, &pixelShader
        );

        psBlob->Release();
        return SUCCEEDED(hr);
    }

    bool CreateGeometry() {
        // 创建顶点缓冲区
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(Vertex) * vertices.size();
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = vertices.data();

        HRESULT hr = device->CreateBuffer(&bufferDesc, &initData, &vertexBuffer);
        if (FAILED(hr)) return false;

        // 创建索引缓冲区
        bufferDesc.ByteWidth = sizeof(UINT) * indices.size();
        bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        initData.pSysMem = indices.data();

        hr = device->CreateBuffer(&bufferDesc, &initData, &indexBuffer);
        if (FAILED(hr)) return false;

        // 创建常量缓冲区
        bufferDesc.ByteWidth = sizeof(ConstantBufferData);
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = device->CreateBuffer(&bufferDesc, nullptr, &constantBuffer);
        return SUCCEEDED(hr);
    }

    bool CreateTexture() {
        // 创建简单的棋盘纹理
        const int texWidth = 256;
        const int texHeight = 256;
        std::vector<UINT> textureData(texWidth * texHeight);

        for (int y = 0; y < texHeight; y++) {
            for (int x = 0; x < texWidth; x++) {
                bool checker = ((x / 32) + (y / 32)) % 2;
                UINT color = checker ? 0xFFFFFFFF : 0xFF000000; // 白色或黑色
                textureData[y * texWidth + x] = color;
            }
        }

        // 创建纹理
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = texWidth;
        textureDesc.Height = texHeight;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA textureSubresourceData = {};
        textureSubresourceData.pSysMem = textureData.data();
        textureSubresourceData.SysMemPitch = texWidth * sizeof(UINT);

        HRESULT hr = device->CreateTexture2D(&textureDesc, &textureSubresourceData, &texture);
        if (FAILED(hr)) return false;

        // 创建着色器资源视图
        hr = device->CreateShaderResourceView(texture, nullptr, &textureView);
        if (FAILED(hr)) return false;

        // 创建采样器
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        hr = device->CreateSamplerState(&samplerDesc, &sampler);
        return SUCCEEDED(hr);
    }

    void Render() {
        // 清屏
        float clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
        deviceContext->ClearRenderTargetView(renderTargetView, clearColor);

        // 更新常量缓冲区
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        deviceContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        ConstantBufferData* cbData = (ConstantBufferData*)mappedResource.pData;
        cbData->worldViewProj = XMMatrixIdentity(); // 使用单位矩阵
        deviceContext->Unmap(constantBuffer, 0);

        // 设置着色器和资源
        deviceContext->VSSetShader(vertexShader, nullptr, 0);
        deviceContext->PSSetShader(pixelShader, nullptr, 0);
        deviceContext->IASetInputLayout(inputLayout);
        deviceContext->VSSetConstantBuffers(0, 1, &constantBuffer);
        deviceContext->PSSetShaderResources(0, 1, &textureView);
        deviceContext->PSSetSamplers(0, 1, &sampler);

        // 设置顶点缓冲区
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        deviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 绘制
        deviceContext->DrawIndexed(indices.size(), 0, 0);

        // 呈现
        swapChain->Present(1, 0);
    }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    D3D11TextureApp app;
    
    if (!app.Initialize(hInstance)) {
        std::cout << "应用程序初始化失败!" << std::endl;
        return -1;
    }

    app.Run();
    app.Cleanup();
    return 0;
}