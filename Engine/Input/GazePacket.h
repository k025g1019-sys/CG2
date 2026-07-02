#pragma once

#include <cstdint>

// 視線追跡プロセス（OpenGaze等）→ ゲーム本体 のIPCで受け渡すパケット定義。
//
// 別プロセスで動く視線推定プログラムが「共有ファイルのメモリマップ」へこの構造体を書き込み、
// ゲーム本体（EyeTracker）が毎フレーム読み取る。送信側と受信側で必ず一致させること。
//
// 共有先はファイル（既定: %TEMP%\DirectXGameGaze.bin）。名前付きカーネルオブジェクトと違い、
// セッションや権限の違いによる名前空間の食い違いが起きにくく、パスを目視確認できる。
// 環境変数 DIRECTXGAME_GAZE_FILE で送信側・受信側ともにパスを上書きできる。
//
// 値は「画面に対する頭/視線の正規化位置」。原点(0,0)=正面中央。
//   gazeX: -1=左いっぱい  / +1=右いっぱい
//   gazeY: -1=下いっぱい  / +1=上いっぱい
//   headZ: -1=後ろ        / +1=前（任意。未使用なら0）
namespace gaze {

// 共有ファイル名（%TEMP%配下に置く既定名）と、パス上書き用の環境変数名。
inline constexpr char kSharedFileName[] = "DirectXGameGaze.bin";
inline constexpr char kSharedFileEnvVar[] = "DIRECTXGAME_GAZE_FILE";

// 共有メモリの確保サイズ（将来の拡張余地を含めた固定長）。
inline constexpr uint32_t kSharedMemorySize = 64;

// パケットの妥当性チェック用マジック（'GAZE'）。
inline constexpr uint32_t kMagic = 0x47415A45u;

#pragma pack(push, 1)
struct GazePacket {
    uint32_t magic;    // kMagic。一致しなければ無効データとみなす。
    uint32_t valid;    // 0=無効（顔未検出など） / 1=有効
    uint64_t frameId;  // 送信側が更新ごとに+1する。停止検知（接続判定）に使う。
    float gazeX;       // [-1..+1] 水平。-1=左 / +1=右
    float gazeY;       // [-1..+1] 垂直。-1=下 / +1=上
    float headZ;       // [-1..+1] 奥行き（任意）。0=既定距離
};
#pragma pack(pop)

static_assert(sizeof(GazePacket) <= kSharedMemorySize, "GazePacket exceeds shared memory size");

}  // namespace gaze
