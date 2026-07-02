#include "Game/MyGame.h"

void MyGame::Initialize() {
	// エンジン各サブシステムの初期化
	Framework::Initialize();

	// シーン初期化（テクスチャ・モデル等の読み込みもここで行われる）
	scene_ = std::make_unique<GameScene>();
	scene_->Initialize();
}

void MyGame::Finalize() {
	// シーンのGPUリソースをエンジン終了処理（リークチェック）より先に解放する
	scene_.reset();

	Framework::Finalize();
}

void MyGame::Update() {
	scene_->Update();
}

void MyGame::Draw(ID3D12GraphicsCommandList* commandList) {
	scene_->Draw(commandList);
}

#ifdef USE_IMGUI
void MyGame::DrawImGui() {
	scene_->DrawImGui();
}
#endif
