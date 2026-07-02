#pragma once

#include <cstdint>
#include <string>

// 視線追跡（別プロセスのOpenGaze等）から共有メモリ経由で視線位置を受け取り、
// 平滑化して提供する。受信が無い場合は中央(0,0)へ戻り、IsConnected()がfalseになる。
//
// 使い方：Initialize() → 毎フレームUpdate() → GetGazeX/Y()で参照 → Finalize()。
// 値の意味はGazePacket.hを参照（gazeX: -1左/+1右, gazeY: -1下/+1上）。
class EyeTracker {
public:
    // 共有メモリを確保（無ければ作成）して読み取り可能にする。
    void Initialize();

    // 共有メモリのマッピングを解放する。
    void Finalize();

    // 最新パケットを読み取り、平滑化する（毎フレーム呼ぶ）。
    void Update();

    // 直近で送信側から更新を受け取れているか（停止/未起動ならfalse）。
    bool IsConnected() const { return connected_; }

    // 平滑化済みの正規化視線位置。
    float GetGazeX() const { return smoothX_; }  // [-1(左)..+1(右)]
    float GetGazeY() const { return smoothY_; }  // [-1(下)..+1(上)]
    float GetHeadZ() const { return smoothZ_; }  // [-1(後)..+1(前)]

    // 平滑化係数（0..1）。大きいほど追従が速い／小さいほど滑らか。ImGuiで調整する。
    void SetSmoothing(float alpha) { smoothing_ = alpha; }
    float GetSmoothing() const { return smoothing_; }

    // --- 診断用（ImGui表示で接続トラブルを切り分ける）---
    bool IsMapped() const { return view_ != nullptr; }        // 共有メモリを確保できているか
    bool IsMagicValid() const { return dbgMagicOk_; }         // 直近パケットのマジックが一致したか
    bool IsValidFlag() const { return dbgValidFlag_; }        // 直近パケットのvalidが1か（顔検出中など）
    uint64_t GetRawFrameId() const { return dbgRawFrameId_; } // 直近に読んだframeId（増えていれば受信中）
    const std::string& GetSharedPath() const { return sharedPath_; }  // 実際に開いた共有ファイルのパス

private:
    void* mapFile_ = nullptr;   // HANDLE（FileMapping）。Windows.hを表に出さないためvoid*で保持。
    void* fileHandle_ = nullptr;// HANDLE（共有ファイル本体）。
    void* view_ = nullptr;      // MapViewOfFileしたGazePacketの先頭ポインタ。
    std::string sharedPath_;    // 開いた共有ファイルのフルパス（診断表示用）

    bool connected_ = false;
    float smoothX_ = 0.0f;
    float smoothY_ = 0.0f;
    float smoothZ_ = 0.0f;
    float smoothing_ = 0.25f;

    uint64_t lastFrameId_ = 0;  // 直近に観測したframeId
    int staleCount_ = 0;        // frameIdが更新されないまま経過したUpdate回数

    // 診断用に直近パケットの生情報を保持する
    bool dbgMagicOk_ = false;
    bool dbgValidFlag_ = false;
    uint64_t dbgRawFrameId_ = 0;
};
