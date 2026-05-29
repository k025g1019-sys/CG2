#pragma once
#include <Windows.h>
#include <d3d12.h>

class ImGuiManager {
public:
    static ImGuiManager* GetInstance();

    void Initialize(
        HWND hwnd,
        ID3D12Device* device,
        int bufferCount,
        DXGI_FORMAT rtvFormat,
        ID3D12DescriptorHeap* srvHeap
    );

    void BeginFrame();
    void EndFrame(ID3D12GraphicsCommandList* cmd);

    void Shutdown();
};