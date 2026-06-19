#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>     // ログファイル書き込み
#include <filesystem>  // ディレクトリ作成
#include <chrono>      // 時刻取得
#include <format>      // 文字列整形

#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#include <wrl.h>

#include "externals/DirectXTex/DirectXTex.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

#include "log.h"
#include "CrashHandle.h"

#include "Engine/Core/WinApp.h"
#include "Engine/Core/DirectXCore.h"
#include "Engine/Graphics/ShaderCompiler.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/DescriptorHeapManager.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Graphics/GpuResource.h"
#include "Engine/Audio/Audio.h"
#include "Engine/Input/Input.h"

#include "Game/Scene/GameScene.h"

using Microsoft::WRL::ComPtr;

// Windowsアプリのエントリーポイント
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// COMの初期化
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// 未捕捉の例外時にクラッシュダンプを出力する関数を登録
	SetUnhandledExceptionFilter(ExportDump);

	// --- ログファイルを用意 ---
	std::filesystem::create_directory("logs");
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	std::string dataString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	std::string logFilePath = std::string("logs/") + dataString + ".log";
	std::ofstream logStream(logFilePath);
	logStream.flush();

	// --- ウィンドウ生成 ---
	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize();

	// --- DirectX12初期化 ---
	DirectXCore::GetInstance()->Initialize(
		winApp->GetHwnd(),
		WinApp::kClientWidth,
		WinApp::kClientHeight);

	// シェーダコンパイラ初期化
	ShaderCompiler::GetInstance()->Initialize();

	// サウンド初期化（XAudio2）
	Audio::GetInstance()->Initialize();

	// 入力初期化（DirectInput）
	Input::GetInstance()->Initialize(winApp->GetHwnd());

	// よく使うDirectXオブジェクトを取得
	ID3D12GraphicsCommandList* commandList = DirectXCore::GetInstance()->GetCommandList();
	ID3D12Device* device = DirectXCore::GetInstance()->GetDevice();
	ID3D12DescriptorHeap* srvDescriptorHeap = DirectXCore::GetInstance()->GetSRVDescriptorHeap();

	Log("Complete create D3D12Device!!!\n");

#ifdef _DEBUG
	// 危険なエラーで停止し、既知の偽エラーは抑制する
	{
		ComPtr<ID3D12InfoQueue> infoQueue;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
			// Windows11のDXGI/DX12デバッグレイヤー相互作用バグによるエラーを抑制
			// https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID denyIds[] = {
				D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
			};
			D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
			D3D12_INFO_QUEUE_FILTER filter{};
			filter.DenyList.NumIDs = _countof(denyIds);
			filter.DenyList.pIDList = denyIds;
			filter.DenyList.NumSeverities = _countof(severities);
			filter.DenyList.pSeverityList = severities;
			infoQueue->PushStorageFilter(&filter);
		}
	}
#endif

	// リソースを保持するComPtrはこのスコープを抜けると自動開放される。
	// （リソースリークチェックより前に解放させるため、明示的にスコープで囲む）
	{
		// 深度バッファ（DepthStencil）はDirectXCoreが生成・所有し、リサイズ時に作り直す

		// --- RootSignatureとPSOを作る ---
		ComPtr<ID3D12RootSignature> rootSignature = PipelineManager::CreateRootSignature(device);

		ComPtr<IDxcBlob> vertexShaderBlob = ShaderCompiler::GetInstance()->Compile(L"Shaders/Object3D.VS.hlsl", L"vs_6_0");
		assert(vertexShaderBlob != nullptr);
		ComPtr<IDxcBlob> pixelShaderBlob = ShaderCompiler::GetInstance()->Compile(L"Shaders/Object3D.PS.hlsl", L"ps_6_0");
		assert(pixelShaderBlob != nullptr);

		ComPtr<ID3D12PipelineState> graphicsPipelineState = PipelineManager::CreateStandardPipeline(
			device, rootSignature.Get(), vertexShaderBlob.Get(), pixelShaderBlob.Get());

		// --- テクスチャを読み込んでGPUへ転送 ---
		std::vector<std::string> texturePaths = {
			"resources/uvChecker.png",
			"resources/monsterBall.png",
			"resources/sky_sphere.png"
		};
		std::vector<TextureData> textures(texturePaths.size());
		std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> textureSrvHandleGPU(textures.size());
		for (size_t i = 0; i < textures.size(); ++i) {
			textures[i] = TextureManager::LoadAndUpload(texturePaths[i], device, commandList);
		}

		// --- シーン初期化 ---
		GameScene scene;
		scene.Initialize(device, rootSignature.Get(), vertexShaderBlob.Get(), pixelShaderBlob.Get());

#ifdef USE_IMGUI
		// --- ImGui初期化（SRVヒープのindex0を使用）---
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		ImGui_ImplWin32_Init(winApp->GetHwnd());
		ImGui_ImplDX12_Init(
			device,
			DirectXCore::GetSwapChainBufferCount(),
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			srvDescriptorHeap,
			srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		ImGui::GetIO().Fonts->Build();
#endif

		// --- 各テクスチャのSRVを生成（index1から。0はImGuiが使用）---
		for (size_t i = 0; i < textures.size(); ++i) {
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = textures[i].metadata.format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = UINT(textures[i].metadata.mipLevels);

			uint32_t srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = DescriptorHeapManager::GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, static_cast<int>(i) + 1);
			textureSrvHandleGPU[i] = DescriptorHeapManager::GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, static_cast<int>(i) + 1);
			device->CreateShaderResourceView(textures[i].textureResource.Get(), &srvDesc, textureSrvHandleCPU);
		}

		// --- メインループ（ウィンドウの×ボタンが押されるまで）---
		while (winApp->ProcessMessage()) {
			// ウィンドウサイズが変わっていたら、スワップチェーン・深度バッファ・ビューポートを作り直す
			if (winApp->IsSizeChanged()) {
				DirectXCore::GetInstance()->Resize(
					winApp->GetClientWidth(), winApp->GetClientHeight());
				winApp->ClearSizeChangedFlag();
			}
#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			scene.DrawImGui(device);
			ImGui::Render();
#endif

			// 入力状態を更新（全キーを取得し、前フレームと比較できるようにする）
			Input::GetInstance()->Update();

			scene.Update();

			DirectXCore::GetInstance()->BeginFrame();

			scene.Draw(commandList, rootSignature.Get(), graphicsPipelineState.Get(), srvDescriptorHeap, textureSrvHandleGPU.data());

#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

			DirectXCore::GetInstance()->EndFrame();
		}

#ifdef USE_IMGUI
		// ImGui終了処理（SRVヒープ解放より前に行う）
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
#endif
	}

	// --- 終了処理（各リソースは上のスコープでComPtrにより解放済み）---
	Audio::GetInstance()->Finalize();
	Input::GetInstance()->Finalize();
	ShaderCompiler::GetInstance()->Finalize();
	DirectXCore::GetInstance()->Finalize();

	CloseWindow(winApp->GetHwnd());

	// --- リソースリークチェック ---
	ComPtr<IDXGIDebug1> debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
	}

	CoUninitialize();
	return 0;
}
