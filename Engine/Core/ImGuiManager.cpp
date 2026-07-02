#ifdef USE_IMGUI

#include "Engine/Core/ImGuiManager.h"

#include "Engine/Core/DirectXCore.h"
#include "Engine/Core/WinApp.h"
#include "Engine/Graphics/DescriptorHeapManager.h"

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

ImGuiManager* ImGuiManager::GetInstance() {
	static ImGuiManager instance;
	return &instance;
}

void ImGuiManager::Initialize() {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(WinApp::GetInstance()->GetHwnd());

	// フォント用にSRVヒープのスロットを1つ確保して使う
	DescriptorHeapManager* heapManager = DescriptorHeapManager::GetInstance();
	uint32_t srvIndex = heapManager->AllocateSrv();
	ImGui_ImplDX12_Init(
		DirectXCore::GetInstance()->GetDevice(),
		DirectXCore::GetSwapChainBufferCount(),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		DirectXCore::GetInstance()->GetSRVDescriptorHeap(),
		heapManager->GetSrvCPUHandle(srvIndex),
		heapManager->GetSrvGPUHandle(srvIndex));

	ImGui::GetIO().Fonts->Build();
}

void ImGuiManager::BeginFrame() {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImGuiManager::Render() {
	ImGui::Render();
}

void ImGuiManager::Draw(ID3D12GraphicsCommandList* commandList) {
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

void ImGuiManager::Finalize() {
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

#endif  // USE_IMGUI
