#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <string>
#include "../Physics/Math.h"

namespace helix::renderer {

using Microsoft::WRL::ComPtr;
using helix::math::Mat4;
using helix::math::Vec3;

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
};

struct LineVertex {
    float x, y;      // NDC-ish screen space, 0..1 normalized
    float r, g, b, a;
};

// Screen-space textured quad vertex (UI text, sprites, menu art).
// pos is in PIXELS (top-left origin); uv samples a UI texture; col tints.
struct UiVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

struct MeshHandle {
    ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    UINT vertexCount = 0;
};

struct DrawItem {
    const MeshHandle* mesh = nullptr;
    Mat4 world = Mat4::Identity();
};

// A minimal, real, working DX12 forward renderer: device/swapchain/command
// queue setup, one lit PBR-ish opaque pipeline for meshes (ships, terrain),
// and one screen-space line pipeline for the cockpit HUD overlay.
class D3D12Renderer {
public:
    static constexpr UINT kFrameCount = 3;

    bool Initialize(HWND hwnd, int width, int height);
    // Human-readable reason Initialize() returned false (empty on success).
    const std::string& InitError() const { return initError_; }
    void Shutdown();
    void OnResize(int width, int height);

    void BeginFrame(const Vec3& clearColor);
    void SetCamera(const Mat4& view, const Mat4& proj);
    void SetDirectionalLight(const Vec3& direction, const Vec3& color, float intensity);
    void Draw(const DrawItem& item);
    void DrawHudLines(const std::vector<LineVertex>& lines); // GL_LINES-style pairs
    void DrawUiQuads(const std::vector<UiVertex>& quads, UINT textureIndex = 0);
    struct UiDrawRange { UINT textureIndex; UINT vertexOffset; UINT vertexCount; };
    // Uploads `verts` once, then issues one draw per range. Use this instead
    // of repeated DrawUiQuads calls — the GPU reads VB data at draw-execution
    // time, so overwriting the buffer between recorded draws corrupts them.
    void DrawUiGeometry(const std::vector<UiVertex>& verts,
                        const std::vector<UiDrawRange>& ranges);
    void EndFrame();

    // Uploads a 2D texture for the UI pipeline (font atlas, sprites, art).
    // format is the SRV format (e.g. DXGI_FORMAT_R8_UNORM for the font
    // atlas, DXGI_FORMAT_R8G8B8A8_UNORM for images). Returns the texture
    // slot index to pass to DrawUiQuads. data is tightly packed rows of
    // width*bytesPerPixel.
    UINT CreateUiTexture(int width, int height, DXGI_FORMAT format, const void* data);
    UINT UiTextureCount() const { return uiTextureCount_; }

    MeshHandle CreateMesh(const std::vector<Vertex>& vertices);
    MeshHandle CreateBoxMesh(float halfExtent);
    MeshHandle CreateTerrainPatch(int gridSize, float worldSize, float heightScale, uint32_t seed);
    MeshHandle CreateIcosphereMesh(float radius, int subdivisions);

private:
    void WaitForGpu();
    void MoveToNextFrame();
    ComPtr<ID3DBlob> CompileShader(const wchar_t* path, const char* entry, const char* target);
    std::wstring ShaderPath(const wchar_t* name); // resolves next to the exe

    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> commandQueue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    UINT rtvDescriptorSize_ = 0;

    ComPtr<ID3D12Resource> renderTargets_[kFrameCount];
    ComPtr<ID3D12Resource> depthStencil_;
    ComPtr<ID3D12CommandAllocator> commandAllocators_[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> commandList_;

    ComPtr<ID3D12RootSignature> meshRootSignature_;
    ComPtr<ID3D12PipelineState> meshPso_;

    ComPtr<ID3D12RootSignature> lineRootSignature_;
    ComPtr<ID3D12PipelineState> linePso_;
    ComPtr<ID3D12Resource> lineVertexBuffer_;
    UINT lineVertexBufferCapacity_ = 0;

    // UI textured-quad pipeline (font atlas, sprites, menu art)
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT srvDescriptorSize_ = 0;
    UINT uiTextureCount_ = 0;
    static constexpr UINT kMaxUiTextures = 16;
    ComPtr<ID3D12RootSignature> uiRootSignature_;
    ComPtr<ID3D12PipelineState> uiPso_;
    // One upload-heap VB per frame in flight — Map()ing a single shared VB
    // races with in-flight frames and causes flickering UI geometry.
    ComPtr<ID3D12Resource> uiVertexBuffers_[kFrameCount];
    UINT uiVertexBufferCapacity_ = 0;

    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    // frameFenceValues_[i] = fence value signaled for backbuffer i's last
    // submission; fenceCounter_ is a monotonic signal counter.
    UINT64 frameFenceValues_[kFrameCount] = {};
    UINT64 fenceCounter_ = 0;
    UINT frameIndex_ = 0;

    std::string initError_;
    std::wstring shaderDir_;

    struct FrameConstants {
        float viewProj[16];
        float lightDir[4];
        float lightColorIntensity[4];
    };
    struct ObjectConstants {
        float world[16];
    };

    Mat4 view_ = Mat4::Identity();
    Mat4 proj_ = Mat4::Identity();
    Vec3 lightDir_{ 0.3f, -0.8f, 0.5f };
    Vec3 lightColor_{ 1.f, 0.95f, 0.9f };
    float lightIntensity_ = 3.0f;
};

} // namespace helix::renderer
