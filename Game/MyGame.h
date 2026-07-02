#pragma once

#include <memory>

#include "Engine/Core/Framework.h"
#include "Game/Scene/GameScene.h"

/// <summary>
/// このゲームのアプリケーションクラス。
/// Frameworkのメインループから呼ばれる更新・描画をGameSceneへ委譲する。
/// </summary>
class MyGame : public Framework {
protected:

    void Initialize() override;

    void Finalize() override;

    void Update() override;

    void Draw(ID3D12GraphicsCommandList* commandList) override;

#ifdef USE_IMGUI
    void DrawImGui() override;
#endif

private:

    // シーン（Finalizeでエンジン終了処理より先に解放する）
    std::unique_ptr<GameScene> scene_;
};
