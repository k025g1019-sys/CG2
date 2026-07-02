#include "Engine/Core/Framework.h"

#include <cassert>

#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#include <wrl.h>

#include "Engine/Audio/Audio.h"
#include "Engine/Core/DirectXCore.h"
#include "Engine/Core/WinApp.h"
#include "Engine/Graphics/DescriptorHeapManager.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Graphics/ShaderCompiler.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Input/Input.h"
#include "Engine/Diagnostics/CrashHandler.h"
#include "Engine/Diagnostics/Log.h"

#ifdef USE_IMGUI
#include "Engine/Core/ImGuiManager.h"
#endif

using Microsoft::WRL::ComPtr;

void Framework::Run() {

	Initialize();

	WinApp* winApp = WinApp::GetInstance();
	DirectXCore* dxCore = DirectXCore::GetInstance();
	ID3D12GraphicsCommandList* commandList = dxCore->GetCommandList();

	// --- メインループ（ウィンドウの×ボタンが押されるまで）---
	while (winApp->ProcessMessage()) {

		// ウィンドウサイズが変わっていたら、スワップチェーン・深度バッファ・ビューポートを作り直す
		if (winApp->IsSizeChanged()) {
			dxCore->Resize(winApp->GetClientWidth(), winApp->GetClientHeight());
			winApp->ClearSizeChangedFlag();
		}

#ifdef USE_IMGUI
		ImGuiManager::GetInstance()->BeginFrame();
		DrawImGui();
		ImGuiManager::GetInstance()->Render();
#endif

		// 入力状態を更新（全キーを取得し、前フレームと比較できるようにする）
		Input::GetInstance()->Update();

		Update();

		dxCore->BeginFrame();

		Draw(commandList);

#ifdef USE_IMGUI
		ImGuiManager::GetInstance()->Draw(commandList);
#endif

		dxCore->EndFrame();
	}

	// 実行中のフレームが参照しているリソースを解放する前にGPU完了を待つ
	dxCore->WaitForGPU();

	Finalize();
}

void Framework::Initialize() {

	// COMの初期化
	[[maybe_unused]] HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// 未捕捉の例外時にクラッシュダンプを出力する関数を登録
	SetUnhandledExceptionFilter(ExportDump);

	// ログファイルを用意（以降のLogは出力ウィンドウとファイルの両方へ出る）
	InitializeLogFile();

	// --- ウィンドウ生成 ---
	WinApp* winApp = WinApp::GetInstance();
	winApp->Initialize();

	// --- DirectX12初期化 ---
	DirectXCore* dxCore = DirectXCore::GetInstance();
	dxCore->Initialize(
		winApp->GetHwnd(),
		WinApp::kClientWidth,
		WinApp::kClientHeight);

	Log("Complete create D3D12Device!!!");

	// --- 各サブシステム初期化 ---
	ShaderCompiler::GetInstance()->Initialize();
	DescriptorHeapManager::GetInstance()->Initialize(dxCore->GetDevice(), dxCore->GetSRVDescriptorHeap());
	PipelineManager::GetInstance()->Initialize(dxCore->GetDevice());
	TextureManager::GetInstance()->Initialize(dxCore->GetDevice(), dxCore->GetCommandList());
	Audio::GetInstance()->Initialize();
	Input::GetInstance()->Initialize(winApp->GetHwnd());

#ifdef USE_IMGUI
	ImGuiManager::GetInstance()->Initialize();
#endif
}

void Framework::Finalize() {

#ifdef USE_IMGUI
	// ImGui終了処理（SRVヒープ解放より前に行う）
	ImGuiManager::GetInstance()->Finalize();
#endif

	// --- 終了処理（生成と逆順で解放する）---
	Audio::GetInstance()->Finalize();
	Input::GetInstance()->Finalize();
	TextureManager::GetInstance()->Finalize();
	PipelineManager::GetInstance()->Finalize();
	ShaderCompiler::GetInstance()->Finalize();
	DirectXCore::GetInstance()->Finalize();

	CloseWindow(WinApp::GetInstance()->GetHwnd());

	// --- リソースリークチェック ---
	ComPtr<IDXGIDebug1> debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
	}

	CoUninitialize();
}
