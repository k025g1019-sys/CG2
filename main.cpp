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
#include "Engine/Graphics/StereoRenderer.h"
#include "Engine/Graphics/CameraCapture.h"
#include "Engine/Audio/Audio.h"
#include "Engine/Input/Input.h"
#include "Engine/Input/EyeTracker.h"
#include "Engine/Input/FaceTracker.h"

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

		// --- シーン初期化（立体視の視点数ぶん、視点ごとのビュー射影CBufferを確保する）---
		GameScene scene;
		scene.Initialize(device, rootSignature.Get(), vertexShaderBlob.Get(), pixelShaderBlob.Get(), StereoRenderer::GetViewCount());

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

		// --- 立体視レンダラ初期化 ---
		// ビューSRVは共有SRVヒープの「ImGui(0) + テクスチャ数」の次から確保する
		StereoRenderer stereo;
		stereo.Initialize(
			device,
			winApp->GetClientWidth(),
			winApp->GetClientHeight(),
			srvDescriptorHeap,
			static_cast<uint32_t>(textures.size()) + 1);

		// --- 視線追跡（別プロセスのOpenGaze等から共有メモリ経由で受信）---
		EyeTracker eyeTracker;
		eyeTracker.Initialize();

		// --- 実カメラ映像（Media Foundation）。SRVはステレオビューSRVの次のindexを使う ---
		uint32_t webcamSrvIndex =
			static_cast<uint32_t>(textures.size()) + 1 + StereoRenderer::GetViewCount();
		CameraCapture camera;
		camera.Initialize(device, srvDescriptorHeap, webcamSrvIndex);
			// --- Face tracking (in-app: webcam + Windows FaceTracker). Default gaze source. ---
			// The camera worker pushes each frame to the detector (set before the worker starts).
			FaceTracker faceTracker;
			faceTracker.Initialize();
			camera.SetFaceTracker(&faceTracker);
			// Gaze source: 0 = in-app face detection (default) / 1 = shared memory (external OpenGaze etc.)
			int gazeSource = 0;

		// --- ImGuiトグル（どちらも初期値OFF）---
		bool useEyeTracking = false;  // 視線追跡でゲーム内カメラを連動させる
		bool showCamera = false;      // 実カメラを画面右半分に映す（ON時は画面分割）

		// --- メインループ（ウィンドウの×ボタンが押されるまで）---
		while (winApp->ProcessMessage()) {
			// ウィンドウサイズが変わっていたら、スワップチェーン・深度バッファ・ビューポートを作り直す
			if (winApp->IsSizeChanged()) {
				DirectXCore::GetInstance()->Resize(
					winApp->GetClientWidth(), winApp->GetClientHeight());
				// DirectXCore::ResizeでGPU完了待ち済みなので、続けてオフスクリーンも作り直す
				stereo.Resize(
					winApp->GetClientWidth(), winApp->GetClientHeight());
				winApp->ClearSizeChangedFlag();
			}
#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			scene.DrawImGui(device);
			stereo.DrawImGui();
			// 全画面トグル（物理シート整列用）
			ImGui::Begin("Display");
			{
				bool fullscreen = winApp->IsFullscreen();
				if (ImGui::Checkbox("Borderless Fullscreen (F11)", &fullscreen)) {
					winApp->SetFullscreen(fullscreen);
				}
				ImGui::Text("Resolution: %d x %d", winApp->GetClientWidth(), winApp->GetClientHeight());
			}
			ImGui::End();

			// 視線追跡＆実カメラのトグル（どちらも初期値OFF）
			ImGui::Begin("Eye Tracking & Camera");
			{
				ImGui::Checkbox("Eye Tracking (gaze-linked camera)", &useEyeTracking);
				ImGui::SameLine();
				ImGui::TextDisabled(
					(gazeSource == 0 ? faceTracker.IsConnected() : eyeTracker.IsConnected())
						? "[connected]" : "[no signal]");
				// Gaze source: in-app face detection (default) or external shared memory.
				ImGui::RadioButton("In-app face (webcam)", &gazeSource, 0);
				ImGui::SameLine();
				ImGui::RadioButton("Shared memory (external)", &gazeSource, 1);
				ImGui::Text("In-app Diag: ready=%s  face=%s  count=%d",
					faceTracker.IsReady() ? "yes" : "no",
					faceTracker.IsFaceValid() ? "1" : "0",
					faceTracker.GetFaceCount());
				ImGui::TextWrapped(
					"Run the external gaze sender (tools/gaze_sender.py) to drive the in-game camera from your gaze.");
				// 接続診断：送信側からデータが届いているかを切り分ける
				//   shm=no            → 共有メモリ確保に失敗
				//   frameId が増えない → 送信プログラムが動いていない/別プロセスと繋がっていない
				//   magic=-- 　　　　 → 別データ。パケット形式の不一致
				//   valid=0 　　　　　→ 送信中だが顔未検出など（mouseモードで切り分け可）
				ImGui::Text("Diag: shm=%s  magic=%s  valid=%s  frameId=%llu",
					eyeTracker.IsMapped() ? "yes" : "no",
					eyeTracker.IsMagicValid() ? "OK" : "--",
					eyeTracker.IsValidFlag() ? "1" : "0",
					static_cast<unsigned long long>(eyeTracker.GetRawFrameId()));
				// 送信側(gaze_sender.py)が表示するパスと一致しているか確認する
				ImGui::TextWrapped("path: %s", eyeTracker.GetSharedPath().c_str());

				ImGui::Separator();

				ImGui::Checkbox("Show Real Camera (split screen)", &showCamera);
				ImGui::SameLine();
				ImGui::TextDisabled(camera.IsAvailable() ? "[camera ready]" : "[no camera]");
				ImGui::TextWrapped(
					"When ON, the game renders to the left half and the webcam to the right half.");

				if (useEyeTracking) {
					float smoothing = (gazeSource == 0) ? faceTracker.GetSmoothing() : eyeTracker.GetSmoothing();
					if (ImGui::SliderFloat("Gaze Smoothing", &smoothing, 0.02f, 1.0f)) {
						if (gazeSource == 0) { faceTracker.SetSmoothing(smoothing); }
						else { eyeTracker.SetSmoothing(smoothing); }
					}
					ImGui::Text("Gaze: (%.2f, %.2f)",
						(gazeSource == 0) ? faceTracker.GetGazeX() : eyeTracker.GetGazeX(),
						(gazeSource == 0) ? faceTracker.GetGazeY() : eyeTracker.GetGazeY());
				}
			}
			ImGui::End();
			ImGui::Render();
#endif

			// 入力状態を更新（全キーを取得し、前フレームと比較できるようにする）
			Input::GetInstance()->Update();

			// F11でボーダレス全画面を切り替え（物理シート整列用。サイズ変更は次フレーム冒頭で反映される）
			if (Input::GetInstance()->IsTrigger(DIK_F11)) {
				winApp->ToggleFullscreen();
			}

			// 視線追跡を更新し、ゲーム内カメラ（頭連動オフアクシス）へ反映する。
			// 視線追跡ONかつ受信できているときだけ効かせる（未起動/停止時は中央＝従来描画）。
			eyeTracker.Update();
			faceTracker.Update();
			// 視線の取得元を選択（0=アプリ内顔検出 / 1=外部共有メモリ）。
			const bool faceSrc = (gazeSource == 0);
			// アプリ内顔検出は映像非表示でもフレームが要るので、カメラのワーカーを起動しておく。
			if (useEyeTracking && faceSrc) {
				camera.StartCapture();
			}
			bool gazeActive = useEyeTracking && (faceSrc ? faceTracker.IsConnected() : eyeTracker.IsConnected());
			scene.SetEyeTracking(
				gazeActive,
				faceSrc ? faceTracker.GetGazeX() : eyeTracker.GetGazeX(),
				faceSrc ? faceTracker.GetGazeY() : eyeTracker.GetGazeY(),
				faceSrc ? faceTracker.GetHeadZ() : eyeTracker.GetHeadZ());

			// 実カメラ表示が有効でカメラが使えるなら左右分割（ゲームは左半分）。
			// 分割の有無をここで確定し、投影アスペクト補正と描画分割で同じ判定を使う。
			bool splitActive = showCamera && camera.IsAvailable();
			// 分割時はゲームが左半分（横0.5）に表示されるので投影アスペクトを合わせる（縦伸び防止）。
			scene.SetRenderAspectScale(splitActive ? 0.5f : 1.0f);

			scene.Update();

			DirectXCore::GetInstance()->BeginFrame();

			// 実カメラ表示ONなら、最新カメラフレームをGPUテクスチャへ反映しておく（描画コマンドの前に）。
			if (showCamera) {
				camera.UpdateTexture(commandList);
			}

			// --- 各ビューをオフスクリーンへ描画 ---
			for (uint32_t view = 0; view < StereoRenderer::GetViewCount(); ++view) {
				stereo.BeginView(commandList, view);
				scene.Draw(commandList, rootSignature.Get(), graphicsPipelineState.Get(), srvDescriptorHeap, textureSrvHandleGPU.data(), view);
			}

			// --- バックバッファへ合成 ---
			D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV = DirectXCore::GetInstance()->GetCurrentRTVHandle();
			D3D12_VIEWPORT fullViewport = DirectXCore::GetInstance()->GetViewport();
			D3D12_RECT fullScissor = DirectXCore::GetInstance()->GetScissorRect();

			if (splitActive) {
				// 画面分割：ゲーム描画を左半分へ、実カメラ映像を右半分へ。
				float halfWidth = fullViewport.Width * 0.5f;
				LONG midX = (fullScissor.left + fullScissor.right) / 2;

				D3D12_VIEWPORT leftViewport = fullViewport;
				leftViewport.Width = halfWidth;
				D3D12_RECT leftScissor = fullScissor;
				leftScissor.right = midX;

				D3D12_VIEWPORT rightViewport = fullViewport;
				rightViewport.TopLeftX = halfWidth;
				rightViewport.Width = fullViewport.Width - halfWidth;
				D3D12_RECT rightScissor = fullScissor;
				rightScissor.left = midX;

				// 立体視合成を左半分のビューポートへ（フルスクリーン三角形が左半分に収まる）
				stereo.Composite(commandList, backBufferRTV, leftViewport, leftScissor);
				// 実カメラ映像を右半分へ
				camera.Draw(commandList, backBufferRTV, rightViewport, rightScissor);
			} else {
				// 通常：画面全体へ立体視合成
				stereo.Composite(commandList, backBufferRTV, fullViewport, fullScissor);
			}

#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

			DirectXCore::GetInstance()->EndFrame();
		}

		// 実カメラのワーカースレッドを停止し、Media Foundation・GPUリソースを解放する。
		camera.Finalize();
		// 顔検出のWinRTを解放する（カメラのワーカー停止後＝もうProcessFrameBGRAが呼ばれない状態で行う）。
		faceTracker.Finalize();
		// 視線追跡の共有メモリを解放する。
		eyeTracker.Finalize();

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
