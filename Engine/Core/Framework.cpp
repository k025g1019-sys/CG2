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
#include "Engine/Graphics/StereoRenderer.h"
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

	StereoRenderer* stereo = StereoRenderer::GetInstance();

	// --- メインループ（ウィンドウの×ボタンが押されるまで）---
	while (winApp->ProcessMessage()) {

		// ウィンドウサイズが変わっていたら、スワップチェーン・深度バッファ・ビューポートを作り直す
		if (winApp->IsSizeChanged()) {
			dxCore->Resize(winApp->GetClientWidth(), winApp->GetClientHeight());
			// DirectXCore::ResizeでGPU完了待ち済みなので、続けて立体視のオフスクリーンも作り直す
			stereo->Resize(winApp->GetClientWidth(), winApp->GetClientHeight());
			winApp->ClearSizeChangedFlag();
		}

#ifdef USE_IMGUI
		ImGuiManager::GetInstance()->BeginFrame();
		DrawImGui();
		ImGuiManager::GetInstance()->Render();
#endif

		// 入力状態を更新（全キーを取得し、前フレームと比較できるようにする）
		Input::GetInstance()->Update();

		// F11でボーダレス全画面を切り替え（物理シート整列用。サイズ変更は次フレーム冒頭で反映される）
		if (Input::GetInstance()->IsTrigger(DIK_F11)) {
			winApp->ToggleFullscreen();
		}

		Update();

		dxCore->BeginFrame();

		// シーン描画前のフック（Webカメラテクスチャの転送コマンド発行など）
		PreDraw(commandList);

		// ゲームの表示先（画面分割時は左半分。立体視合成／通常描画の両方で使う）
		const D3D12_VIEWPORT fullViewport = dxCore->GetViewport();
		const D3D12_RECT fullScissor = dxCore->GetScissorRect();
		D3D12_VIEWPORT gameViewport = fullViewport;
		D3D12_RECT gameScissor = fullScissor;
		D3D12_VIEWPORT subViewport = fullViewport;
		D3D12_RECT subScissor = fullScissor;
		if (splitScreen_) {
			const float halfWidth = fullViewport.Width * 0.5f;
			const LONG midX = (fullScissor.left + fullScissor.right) / 2;
			gameViewport.Width = halfWidth;
			gameScissor.right = midX;
			subViewport.TopLeftX = fullViewport.TopLeftX + halfWidth;
			subViewport.Width = fullViewport.Width - halfWidth;
			subScissor.left = midX;
		}

		if (stereo->IsEnabled()) {
			// --- 立体視：各視点をオフスクリーンへ描画し、バックバッファへ合成する ---
			const uint32_t viewCount = stereo->GetActiveViewCount();
			for (uint32_t view = 0; view < viewCount; ++view) {
				stereo->BeginView(commandList, view);
				Draw(commandList, view);
			}
			stereo->Composite(
				commandList, dxCore->GetCurrentRTVHandle(), gameViewport, gameScissor);
		} else {
			// --- 通常描画：バックバッファへ直接1回描画（オフスクリーン・合成のコストなし）---
			if (splitScreen_) {
				// 分割時はゲームの描画先を左半分へ絞る（RT/クリアはBeginFrame済み）
				commandList->RSSetViewports(1, &gameViewport);
				commandList->RSSetScissorRects(1, &gameScissor);
			}
			Draw(commandList, 0);
		}

		// 画面分割時の右半分（Webカメラ表示など）
		if (splitScreen_) {
			DrawSubView(commandList, subViewport, subScissor);
		}

#ifdef USE_IMGUI
		// ImGuiは画面全体へ描く（合成・分割の後）
		{
			D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV = dxCore->GetCurrentRTVHandle();
			commandList->OMSetRenderTargets(1, &backBufferRTV, false, nullptr);
			commandList->RSSetViewports(1, &fullViewport);
			commandList->RSSetScissorRects(1, &fullScissor);
		}
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
	StereoRenderer::GetInstance()->Initialize(
		dxCore->GetDevice(), winApp->GetClientWidth(), winApp->GetClientHeight());
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
	StereoRenderer::GetInstance()->Finalize();
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
