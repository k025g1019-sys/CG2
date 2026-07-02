#include "Game/MyGame.h"

#include "Engine/Core/DirectXCore.h"
#include "Engine/Core/WinApp.h"
#include "Engine/Graphics/StereoRenderer.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void MyGame::Initialize() {
	// エンジン各サブシステムの初期化
	Framework::Initialize();

	// シーン初期化（テクスチャ・モデル等の読み込みもここで行われる）
	scene_ = std::make_unique<GameScene>();
	scene_->Initialize();

	// --- 視線追跡（別プロセスのOpenGaze等から共有メモリ経由で受信）---
	eyeTracker_.Initialize();

	// --- Webカメラとアプリ内顔検出（既定の視線ソース）---
	// カメラのワーカーが各フレームを顔検出へ渡す（ワーカー起動前に連携先を設定しておく）。
	// カメラ自体はトグルがONになるまで開かれない。
	camera_.Initialize(DirectXCore::GetInstance()->GetDevice());
	faceTracker_.Initialize();
	camera_.SetFaceTracker(&faceTracker_);
}

void MyGame::Finalize() {
	// 実カメラのワーカースレッドを停止し、Media Foundation・GPUリソースを解放する。
	camera_.Finalize();
	// 顔検出のWinRTを解放する（カメラのワーカー停止後＝もうProcessFrameBGRAが呼ばれない状態で行う）。
	faceTracker_.Finalize();
	// 視線追跡の共有メモリを解放する。
	eyeTracker_.Finalize();

	// シーンのGPUリソースをエンジン終了処理（リークチェック）より先に解放する
	scene_.reset();

	Framework::Finalize();
}

void MyGame::Update() {
	// 視線追跡を更新し、ゲーム内カメラ（頭連動オフアクシス）へ反映する。
	// 視線追跡ONかつ受信できているときだけ効かせる（未起動/停止時は中央＝従来描画）。
	eyeTracker_.Update();
	faceTracker_.Update();

	// 視線の取得元を選択（0=アプリ内顔検出 / 1=外部共有メモリ）。
	const bool faceSource = (gazeSource_ == 0);

	// カメラのワーカーは「映像表示」か「アプリ内顔検出」に使うときだけ動かす。
	// どちらにも使わないときは停止し、非使用時のカメラ・CPU占有をなくす。
	const bool needCamera = showCamera_ || (useEyeTracking_ && faceSource);
	if (needCamera) {
		camera_.StartCapture();
	} else {
		camera_.StopCapture();
	}

	const bool gazeActive =
		useEyeTracking_ && (faceSource ? faceTracker_.IsConnected() : eyeTracker_.IsConnected());
	scene_->SetEyeTracking(
		gazeActive,
		faceSource ? faceTracker_.GetGazeX() : eyeTracker_.GetGazeX(),
		faceSource ? faceTracker_.GetGazeY() : eyeTracker_.GetGazeY(),
		faceSource ? faceTracker_.GetHeadZ() : eyeTracker_.GetHeadZ());

	// 実カメラ表示が有効でカメラが使えるなら左右分割（ゲームは左半分）。
	// 分割時はゲームの投影アスペクトも合わせる（縦伸び防止）。
	const bool splitActive = showCamera_ && camera_.IsAvailable();
	SetSplitScreen(splitActive);
	scene_->SetRenderAspectScale(splitActive ? 0.5f : 1.0f);

	scene_->Update();
}

void MyGame::Draw(ID3D12GraphicsCommandList* commandList, uint32_t viewIndex) {
	scene_->Draw(commandList, viewIndex);
}

void MyGame::PreDraw(ID3D12GraphicsCommandList* commandList) {
	// 実カメラ表示ONなら、最新カメラフレームをGPUテクスチャへ反映しておく（描画コマンドの前に）。
	if (showCamera_) {
		camera_.UpdateTexture(commandList);
	}
}

void MyGame::DrawSubView(
	ID3D12GraphicsCommandList* commandList,
	const D3D12_VIEWPORT& viewport,
	const D3D12_RECT& scissorRect) {
	// 画面分割時の右半分へWebカメラ映像をレターボックスで描く
	camera_.Draw(commandList, DirectXCore::GetInstance()->GetCurrentRTVHandle(), viewport, scissorRect);
}

#ifdef USE_IMGUI
void MyGame::DrawImGui() {
	scene_->DrawImGui();

	// 立体視の方式切り替え・キャリブレーション
	StereoRenderer::GetInstance()->DrawImGui();

	// 全画面トグル（物理シート整列用）
	ImGui::Begin("Display");
	{
		WinApp* winApp = WinApp::GetInstance();
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
		ImGui::Checkbox("Eye Tracking (gaze-linked camera)", &useEyeTracking_);
		ImGui::SameLine();
		ImGui::TextDisabled(
			(gazeSource_ == 0 ? faceTracker_.IsConnected() : eyeTracker_.IsConnected())
				? "[connected]" : "[no signal]");
		// 視線の取得元：アプリ内顔検出（既定）または外部の共有メモリ。
		ImGui::RadioButton("In-app face (webcam)", &gazeSource_, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Shared memory (external)", &gazeSource_, 1);
		ImGui::Text("In-app Diag: ready=%s  face=%s  count=%d",
			faceTracker_.IsReady() ? "yes" : "no",
			faceTracker_.IsFaceValid() ? "1" : "0",
			faceTracker_.GetFaceCount());
		ImGui::TextWrapped(
			"Run the external gaze sender (tools/gaze_sender.py) to drive the in-game camera from your gaze.");
		// 接続診断：送信側からデータが届いているかを切り分ける
		//   shm=no            → 共有メモリ確保に失敗
		//   frameId が増えない → 送信プログラムが動いていない/別プロセスと繋がっていない
		//   magic=-- 　　　　 → 別データ。パケット形式の不一致
		//   valid=0 　　　　　→ 送信中だが顔未検出など（mouseモードで切り分け可）
		ImGui::Text("Diag: shm=%s  magic=%s  valid=%s  frameId=%llu",
			eyeTracker_.IsMapped() ? "yes" : "no",
			eyeTracker_.IsMagicValid() ? "OK" : "--",
			eyeTracker_.IsValidFlag() ? "1" : "0",
			static_cast<unsigned long long>(eyeTracker_.GetRawFrameId()));
		// 送信側(gaze_sender.py)が表示するパスと一致しているか確認する
		ImGui::TextWrapped("path: %s", eyeTracker_.GetSharedPath().c_str());

		ImGui::Separator();

		ImGui::Checkbox("Show Real Camera (split screen)", &showCamera_);
		ImGui::SameLine();
		ImGui::TextDisabled(camera_.IsAvailable() ? "[camera ready]" : "[no camera]");
		ImGui::TextWrapped(
			"When ON, the game renders to the left half and the webcam to the right half.");

		if (useEyeTracking_) {
			float smoothing = (gazeSource_ == 0) ? faceTracker_.GetSmoothing() : eyeTracker_.GetSmoothing();
			if (ImGui::SliderFloat("Gaze Smoothing", &smoothing, 0.02f, 1.0f)) {
				if (gazeSource_ == 0) { faceTracker_.SetSmoothing(smoothing); }
				else { eyeTracker_.SetSmoothing(smoothing); }
			}
			ImGui::Text("Gaze: (%.2f, %.2f)",
				(gazeSource_ == 0) ? faceTracker_.GetGazeX() : eyeTracker_.GetGazeX(),
				(gazeSource_ == 0) ? faceTracker_.GetGazeY() : eyeTracker_.GetGazeY());
		}
	}
	ImGui::End();
}
#endif
