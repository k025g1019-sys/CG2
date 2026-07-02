#pragma once

#ifdef USE_IMGUI

#include <d3d12.h>

/// <summary>
/// ImGuiの初期化・フレーム処理・描画・終了処理をまとめたシングルトン。
/// Debugビルド限定（USE_IMGUI）。Releaseではこのクラスごとビルドから除外される。
/// </summary>
class ImGuiManager {
public:

    static ImGuiManager* GetInstance();

    // ImGuiコンテキストとWin32/DX12バックエンドを初期化する
    // （WinApp・DirectXCore・DescriptorHeapManagerの初期化後に呼ぶ）
    void Initialize();

    // フレーム開始（この後にImGui::Begin等でUIを構築する）
    void BeginFrame();

    // UI構築の終了（描画データを確定する）
    void Render();

    // 確定済みの描画データをコマンドリストへ積む（シーン描画の後に呼ぶ）
    void Draw(ID3D12GraphicsCommandList* commandList);

    // 終了処理（SRVヒープ解放より前に呼ぶ）
    void Finalize();

private:

    ImGuiManager() = default;

    ~ImGuiManager() = default;

    ImGuiManager(const ImGuiManager&) = delete;

    ImGuiManager& operator=(const ImGuiManager&) = delete;
};

#endif  // USE_IMGUI
