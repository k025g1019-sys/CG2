#include "ImGuiManager.h"

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

ImGuiManager* ImGuiManager::GetInstance() {
    static ImGuiManager instance;
    return &instance;
}

void ImGuiManager::Initialize(
    HWND hwnd,
    ID3D12Device* device,
    int bufferCount,
    DXGI_FORMAT rtvFormat,
    ID3D12DescriptorHeap* srvHeap
) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_Init(
        device,
        bufferCount,
        rtvFormat,
        srvHeap,
        srvHeap->GetCPUDescriptorHandleForHeapStart(),
        srvHeap->GetGPUDescriptorHandleForHeapStart()
    );
}

void ImGuiManager::BeginFrame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::EndFrame(ID3D12GraphicsCommandList* cmd) {
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void ImGuiManager::Shutdown() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}