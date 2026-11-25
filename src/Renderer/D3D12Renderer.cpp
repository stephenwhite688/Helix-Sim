#include "D3D12Renderer.h"
#include <d3dcompiler.h>
#include <cmath>
#include <random>
#include <stdexcept>
#include <array>
#include <unordered_map>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace helix::renderer {

namespace {
void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(buf);
    }
}

D3D12_HEAP_PROPERTIES UploadHeap() {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = D3D12_HEAP_TYPE_UPLOAD;
    return p;
}
D3D12_HEAP_PROPERTIES DefaultHeap() {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = D3D12_HEAP_TYPE_DEFAULT;
    return p;
}
D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
} // namespace

bool D3D12Renderer::Initialize(HWND hwnd, int width, int height) {
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;

    try {
#if defined(_DEBUG)
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
                debugController->EnableDebugLayer();
            }
        }
#endif
        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2 failed");

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
                break;
            }
        }
        ThrowIfFailed(device_ ? S_OK : E_FAIL, "No suitable DX12 adapter found");

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_)), "CreateCommandQueue failed");

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.BufferCount = kFrameCount;
        scDesc.Width = width;
        scDesc.Height = height;
        scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
            commandQueue_.Get(), hwnd, &scDesc, nullptr, nullptr, &swapChain1), "CreateSwapChainForHwnd failed");
        ThrowIfFailed(swapChain1.As(&swapChain_), "SwapChain QueryInterface failed");
        frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = kFrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed(device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)), "CreateDescriptorHeap(RTV) failed");
        rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ThrowIfFailed(device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_)), "CreateDescriptorHeap(DSV) failed");

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrameCount; ++i) {
            ThrowIfFailed(swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i])), "GetBuffer failed");
            device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += rtvDescriptorSize_;
            ThrowIfFailed(device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators_[i])), "CreateCommandAllocator failed");
        }

        // Depth buffer
        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format = DXGI_FORMAT_D32_FLOAT;
        depthClear.DepthStencil.Depth = 1.0f;

        D3D12_HEAP_PROPERTIES defaultHeap = DefaultHeap();
        ThrowIfFailed(device_->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&depthStencil_)), "CreateDepthStencil failed");
        device_->CreateDepthStencilView(depthStencil_.Get(), nullptr, dsvHeap_->GetCPUDescriptorHandleForHeapStart());

        ThrowIfFailed(device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators_[frameIndex_].Get(),
            nullptr, IID_PPV_ARGS(&commandList_)), "CreateCommandList failed");
        commandList_->Close();

        ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence failed");
        fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        // --- Mesh pipeline (opaque, lit) ---
        {
            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.Num32BitValues = sizeof(FrameConstants) / 4;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[1].Constants.ShaderRegister = 1;
            params[1].Constants.Num32BitValues = sizeof(ObjectConstants) / 4;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = 2;
            rsDesc.pParameters = params;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sig, err;
            ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err),
                          "SerializeRootSignature(mesh) failed");
            ThrowIfFailed(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                          IID_PPV_ARGS(&meshRootSignature_)), "CreateRootSignature(mesh) failed");

            ComPtr<ID3DBlob> vs = CompileShader(ShaderPath(L"mesh.hlsl").c_str(), "VSMain", "vs_5_1");
            ComPtr<ID3DBlob> ps = CompileShader(ShaderPath(L"mesh.hlsl").c_str(), "PSMain", "ps_5_1");

            D3D12_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout = { layout, _countof(layout) };
            psoDesc.pRootSignature = meshRootSignature_.Get();
            psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;
            ThrowIfFailed(device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&meshPso_)), "CreatePSO(mesh) failed");
        }

        // --- HUD line pipeline (screen-space, no depth, alpha blended) ---
        {
            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ComPtr<ID3DBlob> sig, err;
            ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err),
                          "SerializeRootSignature(line) failed");
            ThrowIfFailed(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                          IID_PPV_ARGS(&lineRootSignature_)), "CreateRootSignature(line) failed");

            ComPtr<ID3DBlob> vs = CompileShader(ShaderPath(L"line.hlsl").c_str(), "VSMain", "vs_5_1");
            ComPtr<ID3DBlob> ps = CompileShader(ShaderPath(L"line.hlsl").c_str(), "PSMain", "ps_5_1");

            D3D12_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout = { layout, _countof(layout) };
            psoDesc.pRootSignature = lineRootSignature_.Get();
            psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

            D3D12_RENDER_TARGET_BLEND_DESC blend{};
            blend.BlendEnable = TRUE;
            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_ZERO;
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[0] = blend;

            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;
            ThrowIfFailed(device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&linePso_)), "CreatePSO(line) failed");

            lineVertexBufferCapacity_ = 4096;
            D3D12_HEAP_PROPERTIES uploadHeap = UploadHeap();
            D3D12_RESOURCE_DESC bufDesc = BufferDesc(lineVertexBufferCapacity_ * sizeof(LineVertex));
            ThrowIfFailed(device_->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&lineVertexBuffer_)), "Create line VB failed");
        }

        // --- UI texture SRV heap (font atlas, sprites, menu art) ---
        {
            D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
            srvHeapDesc.NumDescriptors = kMaxUiTextures;
            srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            ThrowIfFailed(device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap_)),
                          "CreateDescriptorHeap(SRV) failed");
            srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        // --- UI quad pipeline (textured, screen-space, alpha blended) ---
        {
            D3D12_DESCRIPTOR_RANGE srvRange{};
            srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors = 1;
            srvRange.BaseShaderRegister = 0;
            srvRange.RegisterSpace = 0;
            srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.Num32BitValues = 4; // screenSize.xy + pad
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &srvRange;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ShaderRegister = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = 2;
            rsDesc.pParameters = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers = &sampler;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sig, err;
            ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err),
                          "SerializeRootSignature(ui) failed");
            ThrowIfFailed(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                          IID_PPV_ARGS(&uiRootSignature_)), "CreateRootSignature(ui) failed");

            ComPtr<ID3DBlob> vs = CompileShader(ShaderPath(L"ui.hlsl").c_str(), "VSMain", "vs_5_1");
            ComPtr<ID3DBlob> ps = CompileShader(ShaderPath(L"ui.hlsl").c_str(), "PSMain", "ps_5_1");

            D3D12_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout = { layout, _countof(layout) };
            psoDesc.pRootSignature = uiRootSignature_.Get();
            psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

            D3D12_RENDER_TARGET_BLEND_DESC blend{};
            blend.BlendEnable = TRUE;
            blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[0] = blend;

            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;
            ThrowIfFailed(device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&uiPso_)),
                          "CreatePSO(ui) failed");

            uiVertexBufferCapacity_ = 65536;
            D3D12_HEAP_PROPERTIES uploadHeap2 = UploadHeap();
            D3D12_RESOURCE_DESC bufDesc2 = BufferDesc(uiVertexBufferCapacity_ * sizeof(UiVertex));
            for (UINT i = 0; i < kFrameCount; ++i) {
                ThrowIfFailed(device_->CreateCommittedResource(
                    &uploadHeap2, D3D12_HEAP_FLAG_NONE, &bufDesc2,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&uiVertexBuffers_[i])), "Create UI VB failed");
            }
        }

        return true;
    } catch (const std::exception& e) {
        initError_ = e.what();
        return false;
    }
}

std::wstring D3D12Renderer::ShaderPath(const wchar_t* name) {
    if (shaderDir_.empty()) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring p(exePath);
        size_t slash = p.find_last_of(L"\\/");
        shaderDir_ = (slash == std::wstring::npos) ? std::wstring(L"")
                                                 : p.substr(0, slash + 1) + L"shaders\\";
    }
    return shaderDir_ + name;
}

ComPtr<ID3DBlob> D3D12Renderer::CompileShader(const wchar_t* path, const char* entry, const char* target) {
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompileFromFile(path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                     entry, target, flags, 0, &code, &errors);
    if (FAILED(hr)) {
        std::string msg = "Shader compile failed: ";
        if (errors) msg += static_cast<const char*>(errors->GetBufferPointer());
        throw std::runtime_error(msg);
    }
    return code;
}

void D3D12Renderer::WaitForGpu() {
    UINT64 v = ++fenceCounter_;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), v), "Signal failed");
    ThrowIfFailed(fence_->SetEventOnCompletion(v, fenceEvent_), "SetEventOnCompletion failed");
    WaitForSingleObjectEx(fenceEvent_, INFINITE, FALSE);
}

void D3D12Renderer::MoveToNextFrame() {
    // Just track which backbuffer is current; the actual GPU wait for the
    // frame we're about to reuse happens at the top of BeginFrame.
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void D3D12Renderer::BeginFrame(const Vec3& clearColor) {
    // Wait until this backbuffer's previous submission has finished on the
    // GPU — required before resetting its command allocator/list, and before
    // overwriting the per-frame UI vertex buffer.
    if (fence_->GetCompletedValue() < frameFenceValues_[frameIndex_]) {
        ThrowIfFailed(fence_->SetEventOnCompletion(frameFenceValues_[frameIndex_], fenceEvent_),
                      "SetEventOnCompletion failed");
        WaitForSingleObjectEx(fenceEvent_, INFINITE, FALSE);
    }

    commandAllocators_[frameIndex_]->Reset();
    commandList_->Reset(commandAllocators_[frameIndex_].Get(), meshPso_.Get());

    D3D12_RESOURCE_BARRIER toRT{};
    toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRT.Transition.pResource = renderTargets_[frameIndex_].Get();
    toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRT.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1, &toRT);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameIndex_) * rtvDescriptorSize_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    commandList_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    float clear[4] = { clearColor.x, clearColor.y, clearColor.z, 1.f };
    commandList_->ClearRenderTargetView(rtv, clear, 0, nullptr);
    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT viewport{ 0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_), 0.f, 1.f };
    D3D12_RECT scissor{ 0, 0, width_, height_ };
    commandList_->RSSetViewports(1, &viewport);
    commandList_->RSSetScissorRects(1, &scissor);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Descriptor tables (UI textures) resolve against bound heaps — the
    // command list resets each frame, so rebind every frame.
    ID3D12DescriptorHeap* heaps[] = { srvHeap_.Get() };
    commandList_->SetDescriptorHeaps(1, heaps);

    commandList_->SetPipelineState(meshPso_.Get());
    commandList_->SetGraphicsRootSignature(meshRootSignature_.Get());
}

void D3D12Renderer::SetCamera(const Mat4& view, const Mat4& proj) {
    view_ = view;
    proj_ = proj;
}

void D3D12Renderer::SetDirectionalLight(const Vec3& direction, const Vec3& color, float intensity) {
    lightDir_ = direction;
    lightColor_ = color;
    lightIntensity_ = intensity;
}

void D3D12Renderer::Draw(const DrawItem& item) {
    if (!item.mesh || item.mesh->vertexCount == 0) return;

    FrameConstants fc{};
    Mat4 viewProj = Mat4::Multiply(proj_, view_);
    for (int i = 0; i < 16; ++i) fc.viewProj[i] = viewProj.m[i];
    fc.lightDir[0] = lightDir_.x; fc.lightDir[1] = lightDir_.y; fc.lightDir[2] = lightDir_.z; fc.lightDir[3] = 0.f;
    fc.lightColorIntensity[0] = lightColor_.x; fc.lightColorIntensity[1] = lightColor_.y;
    fc.lightColorIntensity[2] = lightColor_.z; fc.lightColorIntensity[3] = lightIntensity_;

    ObjectConstants oc{};
    for (int i = 0; i < 16; ++i) oc.world[i] = item.world.m[i];

    commandList_->SetGraphicsRoot32BitConstants(0, sizeof(FrameConstants) / 4, &fc, 0);
    commandList_->SetGraphicsRoot32BitConstants(1, sizeof(ObjectConstants) / 4, &oc, 0);
    commandList_->IASetVertexBuffers(0, 1, &item.mesh->vbv);
    commandList_->DrawInstanced(item.mesh->vertexCount, 1, 0, 0);
}

void D3D12Renderer::DrawHudLines(const std::vector<LineVertex>& lines) {
    if (lines.empty()) return;
    UINT count = static_cast<UINT>(lines.size());
    if (count > lineVertexBufferCapacity_) count = lineVertexBufferCapacity_; // clamp; HUD never exceeds this in practice

    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    if (SUCCEEDED(lineVertexBuffer_->Map(0, &readRange, &mapped))) {
        memcpy(mapped, lines.data(), count * sizeof(LineVertex));
        lineVertexBuffer_->Unmap(0, nullptr);
    }

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = lineVertexBuffer_->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(LineVertex);
    vbv.SizeInBytes = count * sizeof(LineVertex);

    commandList_->SetPipelineState(linePso_.Get());
    commandList_->SetGraphicsRootSignature(lineRootSignature_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(count, 1, 0, 0);
}

UINT D3D12Renderer::CreateUiTexture(int width, int height, DXGI_FORMAT format, const void* data) {
    if (uiTextureCount_ >= kMaxUiTextures || width <= 0 || height <= 0 || !data) return UINT_MAX;
    UINT index = uiTextureCount_++;

    UINT bpp = 0;
    switch (format) {
        case DXGI_FORMAT_R8_UNORM: bpp = 1; break;
        case DXGI_FORMAT_R8G8B8A8_UNORM: bpp = 4; break;
        default: bpp = 4; break;
    }
    UINT rowPitch = width * bpp;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ComPtr<ID3D12Resource> upload;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSize = 0, uploadSize = 0;
    device_->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = UploadHeap();
    D3D12_RESOURCE_DESC bufDesc = BufferDesc(uploadSize);
    device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));

    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    upload->Map(0, &readRange, &mapped);
    BYTE* dst = static_cast<BYTE*>(mapped) + footprint.Offset;
    for (UINT row = 0; row < numRows; ++row) {
        memcpy(dst + row * footprint.Footprint.RowPitch,
               static_cast<const BYTE*>(data) + row * rowPitch,
               rowPitch);
    }
    upload->Unmap(0, nullptr);

    D3D12_HEAP_PROPERTIES defaultHeap = DefaultHeap();
    ComPtr<ID3D12Resource> texture;
    device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));

    // Dedicated command list executed immediately — safe to call outside
    // BeginFrame/EndFrame (init time); queue ordering guarantees the copy
    // completes before any later frame samples the texture.
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list));
    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = texture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = upload.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;
    list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
    list->Close();

    ID3D12CommandList* lists[] = { list.Get() };
    commandQueue_->ExecuteCommandLists(1, lists);
    // The ad-hoc allocator/list are destroyed when this function returns —
    // wait for the GPU so they aren't released mid-execution.
    WaitForGpu();

    // Store the texture alive alongside the SRV by leaking into a static
    // vector — UI textures live for the program's lifetime anyway.
    static std::vector<ComPtr<ID3D12Resource>> uiTextureStore;
    uiTextureStore.push_back(texture);
    uiTextureStore.push_back(upload);

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(index) * srvDescriptorSize_;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(texture.Get(), &srvDesc, cpu);

    return index;
}

void D3D12Renderer::DrawUiQuads(const std::vector<UiVertex>& quads, UINT textureIndex) {
    if (quads.empty() || textureIndex >= uiTextureCount_) return;
    UINT count = static_cast<UINT>(quads.size());
    if (count > uiVertexBufferCapacity_) count = uiVertexBufferCapacity_;

    auto* vb = uiVertexBuffers_[frameIndex_].Get();
    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    if (SUCCEEDED(vb->Map(0, &readRange, &mapped))) {
        memcpy(mapped, quads.data(), count * sizeof(UiVertex));
        vb->Unmap(0, nullptr);
    }

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = vb->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(UiVertex);
    vbv.SizeInBytes = count * sizeof(UiVertex);

    float uiConstants[4] = { static_cast<float>(width_), static_cast<float>(height_), 0.f, 0.f };
    commandList_->SetPipelineState(uiPso_.Get());
    commandList_->SetGraphicsRootSignature(uiRootSignature_.Get());
    commandList_->SetGraphicsRoot32BitConstants(0, 4, uiConstants, 0);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(textureIndex) * srvDescriptorSize_;
    commandList_->SetGraphicsRootDescriptorTable(1, gpu);

    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(count, 1, 0, 0);
}

void D3D12Renderer::DrawUiGeometry(const std::vector<UiVertex>& verts,
                                   const std::vector<UiDrawRange>& ranges) {
    if (verts.empty() || verts.size() > uiVertexBufferCapacity_ || ranges.empty()) return;
    {
        static bool logged = false;
        if (!logged) {
            logged = true;
            char buf[160];
            snprintf(buf, sizeof(buf), "DrawUiGeometry: %zu verts, %zu ranges", verts.size(), ranges.size());
            OutputDebugStringA(buf);
            OutputDebugStringA("\n");
        }
    }

    auto* vb = uiVertexBuffers_[frameIndex_].Get();
    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    if (SUCCEEDED(vb->Map(0, &readRange, &mapped))) {
        memcpy(mapped, verts.data(), verts.size() * sizeof(UiVertex));
        vb->Unmap(0, nullptr);
    }

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = vb->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(UiVertex);
    vbv.SizeInBytes = static_cast<UINT>(verts.size() * sizeof(UiVertex));

    float uiConstants[4] = { static_cast<float>(width_), static_cast<float>(height_), 0.f, 0.f };
    commandList_->SetPipelineState(uiPso_.Get());
    commandList_->SetGraphicsRootSignature(uiRootSignature_.Get());
    commandList_->SetGraphicsRoot32BitConstants(0, 4, uiConstants, 0);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &vbv);

    for (const auto& r : ranges) {
        if (r.vertexCount == 0 || r.textureIndex >= uiTextureCount_) continue;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = srvHeap_->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(r.textureIndex) * srvDescriptorSize_;
        commandList_->SetGraphicsRootDescriptorTable(1, gpu);
        commandList_->DrawInstanced(r.vertexCount, 1, r.vertexOffset, 0);
    }
}

void D3D12Renderer::EndFrame() {
    D3D12_RESOURCE_BARRIER toPresent{};
    toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toPresent.Transition.pResource = renderTargets_[frameIndex_].Get();
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1, &toPresent);

    commandList_->Close();
    ID3D12CommandList* lists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, lists);

    // Signal the fence for this backbuffer's submission — BeginFrame waits
    // on it before reusing this frame's allocator/vertex buffers.
    frameFenceValues_[frameIndex_] = ++fenceCounter_;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), frameFenceValues_[frameIndex_]), "Signal failed");

    swapChain_->Present(1, 0);
    MoveToNextFrame();
}

void D3D12Renderer::OnResize(int width, int height) {
    if (width <= 0 || height <= 0 || (width == width_ && height == height_)) return;
    WaitForGpu();

    for (UINT i = 0; i < kFrameCount; ++i) renderTargets_[i].Reset();
    depthStencil_.Reset();

    swapChain_->ResizeBuffers(kFrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    width_ = width;
    height_ = height;
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrameCount; ++i) {
        swapChain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i]));
        device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize_;
    }

    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    D3D12_HEAP_PROPERTIES defaultHeap = DefaultHeap();
    device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&depthStencil_));
    device_->CreateDepthStencilView(depthStencil_.Get(), nullptr, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

void D3D12Renderer::Shutdown() {
    if (device_) WaitForGpu();
    if (fenceEvent_) CloseHandle(fenceEvent_);
}

MeshHandle D3D12Renderer::CreateMesh(const std::vector<Vertex>& vertices) {
    MeshHandle mesh;
    mesh.vertexCount = static_cast<UINT>(vertices.size());
    UINT64 bufferSize = vertices.size() * sizeof(Vertex);

    D3D12_HEAP_PROPERTIES uploadHeap = UploadHeap();
    D3D12_RESOURCE_DESC bufDesc = BufferDesc(bufferSize);
    device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.vertexBuffer));

    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    mesh.vertexBuffer->Map(0, &readRange, &mapped);
    memcpy(mapped, vertices.data(), bufferSize);
    mesh.vertexBuffer->Unmap(0, nullptr);

    mesh.vbv.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.vbv.StrideInBytes = sizeof(Vertex);
    mesh.vbv.SizeInBytes = static_cast<UINT>(bufferSize);
    return mesh;
}

MeshHandle D3D12Renderer::CreateBoxMesh(float h) {
    // 6 faces * 2 tris * 3 verts, each face's own normal/color for a simple
    // readable ship placeholder silhouette (nose cone tint differs from hull).
    Vec3 faceColors[6] = {
        {0.75f,0.78f,0.82f}, {0.75f,0.78f,0.82f}, {0.55f,0.6f,0.65f},
        {0.55f,0.6f,0.65f}, {0.9f,0.5f,0.2f}, {0.35f,0.4f,0.45f}
    };
    Vec3 normals[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
    std::vector<Vertex> verts;
    auto addFace = [&](Vec3 n, Vec3 c, Vec3 a, Vec3 b, Vec3 cc, Vec3 d) {
        Vertex v[6] = {
            {a.x,a.y,a.z,n.x,n.y,n.z,c.x,c.y,c.z}, {b.x,b.y,b.z,n.x,n.y,n.z,c.x,c.y,c.z}, {cc.x,cc.y,cc.z,n.x,n.y,n.z,c.x,c.y,c.z},
            {a.x,a.y,a.z,n.x,n.y,n.z,c.x,c.y,c.z}, {cc.x,cc.y,cc.z,n.x,n.y,n.z,c.x,c.y,c.z}, {d.x,d.y,d.z,n.x,n.y,n.z,c.x,c.y,c.z}
        };
        for (auto& vv : v) verts.push_back(vv);
    };
    addFace(normals[0], faceColors[0], {h,-h,-h},{h,h,-h},{h,h,h},{h,-h,h});
    addFace(normals[1], faceColors[1], {-h,-h,h},{-h,h,h},{-h,h,-h},{-h,-h,-h});
    addFace(normals[2], faceColors[2], {-h,h,-h},{-h,h,h},{h,h,h},{h,h,-h});
    addFace(normals[3], faceColors[3], {-h,-h,h},{-h,-h,-h},{h,-h,-h},{h,-h,h});
    addFace(normals[4], faceColors[4], {-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}); // +Z = nose
    addFace(normals[5], faceColors[5], {h,-h,-h},{-h,-h,-h},{-h,h,-h},{h,h,-h});
    return CreateMesh(verts);
}

MeshHandle D3D12Renderer::CreateTerrainPatch(int gridSize, float worldSize, float heightScale, uint32_t seed) {
    // Simple value-noise heightfield: real fractal/hydraulic erosion terrain
    // generation is the natural next pass on top of this working patch.
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.f, 1.f);

    int res = gridSize + 1;
    std::vector<float> noiseGrid(res * res);
    for (auto& v : noiseGrid) v = dist(rng);

    auto sample = [&](float u, float v) {
        u = std::clamp(u, 0.f, 0.999f) * gridSize;
        v = std::clamp(v, 0.f, 0.999f) * gridSize;
        int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
        float fx = u - x0, fy = v - y0;
        auto at = [&](int xi, int yi) { return noiseGrid[(yi) * res + (xi)]; };
        float h00 = at(x0, y0), h10 = at(x0 + 1, y0), h01 = at(x0, y0 + 1), h11 = at(x0 + 1, y0 + 1);
        float top = h00 * (1 - fx) + h10 * fx;
        float bot = h01 * (1 - fx) + h11 * fx;
        return top * (1 - fy) + bot * fy;
    };

    std::vector<Vertex> verts;
    float step = worldSize / gridSize;
    for (int gy = 0; gy < gridSize; ++gy) {
        for (int gx = 0; gx < gridSize; ++gx) {
            float x0 = -worldSize * 0.5f + gx * step, x1 = x0 + step;
            float z0 = -worldSize * 0.5f + gy * step, z1 = z0 + step;
            float u0 = static_cast<float>(gx) / gridSize, u1 = static_cast<float>(gx + 1) / gridSize;
            float v0 = static_cast<float>(gy) / gridSize, v1 = static_cast<float>(gy + 1) / gridSize;

            float y00 = sample(u0, v0) * heightScale, y10 = sample(u1, v0) * heightScale;
            float y01 = sample(u0, v1) * heightScale, y11 = sample(u1, v1) * heightScale;

            Vec3 p00{ x0, y00, z0 }, p10{ x1, y10, z0 }, p01{ x0, y01, z1 }, p11{ x1, y11, z1 };
            Vec3 n1 = Vec3::Cross(p10 - p00, p01 - p00).Normalized();
            Vec3 n2 = Vec3::Cross(p11 - p10, p01 - p10).Normalized();

            float heightNorm = (y00 + y10 + y01 + y11) * 0.25f / std::max(0.001f, heightScale);
            Vec3 low{ 0.25f, 0.35f, 0.15f }, high{ 0.55f, 0.5f, 0.45f };
            Vec3 c{ low.x + (high.x - low.x) * heightNorm, low.y + (high.y - low.y) * heightNorm, low.z + (high.z - low.z) * heightNorm };

            verts.push_back({ p00.x,p00.y,p00.z, n1.x,n1.y,n1.z, c.x,c.y,c.z });
            verts.push_back({ p10.x,p10.y,p10.z, n1.x,n1.y,n1.z, c.x,c.y,c.z });
            verts.push_back({ p01.x,p01.y,p01.z, n1.x,n1.y,n1.z, c.x,c.y,c.z });

            verts.push_back({ p10.x,p10.y,p10.z, n2.x,n2.y,n2.z, c.x,c.y,c.z });
            verts.push_back({ p11.x,p11.y,p11.z, n2.x,n2.y,n2.z, c.x,c.y,c.z });
            verts.push_back({ p01.x,p01.y,p01.z, n2.x,n2.y,n2.z, c.x,c.y,c.z });
        }
    }
    return CreateMesh(verts);
}

MeshHandle D3D12Renderer::CreateIcosphereMesh(float radius, int subdivisions) {
    // Base icosahedron
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<Vec3> pos = {
        {-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},
        {0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},
        {t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}
    };
    for (auto& p : pos) p = p.Normalized();

    std::vector<std::array<int,3>> faces = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };

    for (int s = 0; s < subdivisions; ++s) {
        std::vector<std::array<int,3>> newFaces;
        std::unordered_map<uint64_t, int> midpointCache;
        auto midpoint = [&](int a, int b) -> int {
            uint64_t key = (static_cast<uint64_t>(std::min(a,b)) << 32) | static_cast<uint32_t>(std::max(a,b));
            auto it = midpointCache.find(key);
            if (it != midpointCache.end()) return it->second;
            Vec3 mid = ((pos[a] + pos[b]) * 0.5f).Normalized();
            pos.push_back(mid);
            int idx = static_cast<int>(pos.size()) - 1;
            midpointCache[key] = idx;
            return idx;
        };
        for (auto& f : faces) {
            int a = midpoint(f[0], f[1]);
            int b = midpoint(f[1], f[2]);
            int c = midpoint(f[2], f[0]);
            newFaces.push_back({f[0], a, c});
            newFaces.push_back({f[1], b, a});
            newFaces.push_back({f[2], c, b});
            newFaces.push_back({a, b, c});
        }
        faces = newFaces;
    }

    std::vector<Vertex> verts;
    Vec3 color{ 0.6f, 0.65f, 0.7f };
    for (auto& f : faces) {
        Vec3 a = pos[f[0]] * radius, b = pos[f[1]] * radius, c = pos[f[2]] * radius;
        Vec3 n = Vec3::Cross(b - a, c - a).Normalized();
        verts.push_back({ a.x,a.y,a.z, n.x,n.y,n.z, color.x,color.y,color.z });
        verts.push_back({ b.x,b.y,b.z, n.x,n.y,n.z, color.x,color.y,color.z });
        verts.push_back({ c.x,c.y,c.z, n.x,n.y,n.z, color.x,color.y,color.z });
    }
    return CreateMesh(verts);
}

} // namespace helix::renderer
