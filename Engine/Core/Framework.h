#pragma once

#include <d3d12.h>

/// <summary>
/// アプリケーション全体の骨組み。
/// エンジン各サブシステムの初期化〜メインループ〜終了処理を担当する。
/// ゲーム側はこのクラスを継承し、Update / Draw（/ DrawImGui）を実装する。
/// </summary>
class Framework {
public:

    virtual ~Framework() = default;

    // 初期化 → メインループ → 終了処理 まで一括で実行する
    void Run();

protected:

    // エンジン各サブシステムの初期化。
    // 派生クラスはこれを呼んだ後に自身の初期化（シーン生成など）を行う。
    virtual void Initialize();

    // 終了処理。派生クラスは自身のリソースを解放してからこれを呼ぶ。
    virtual void Finalize();

    // 毎フレームの更新処理
    virtual void Update() = 0;

    // 描画コマンドの発行
    virtual void Draw(ID3D12GraphicsCommandList* commandList) = 0;

#ifdef USE_IMGUI
    // 開発用ImGuiウィンドウの構築（Debugビルドのみ呼ばれる）
    virtual void DrawImGui() = 0;
#endif
};
