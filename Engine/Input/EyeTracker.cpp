#include "EyeTracker.h"

#include <Windows.h>
#include <cstring>

#include "GazePacket.h"

namespace {
// frameIdの更新が止まってからこの回数を超えたら「切断」とみなす（約0.5秒@60fps）。
constexpr int kStaleLimit = 30;

// std::clampはWindows.hのmin/maxマクロと無関係だが、依存を増やさず明示比較でクランプする。
float Clamp(float v, float lo, float hi) {
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

// 共有ファイルのフルパスを決める：環境変数があれば優先、無ければ %TEMP%\DirectXGameGaze.bin。
std::string ResolveSharedPath() {
    char env[1024];
    DWORD n = GetEnvironmentVariableA(gaze::kSharedFileEnvVar, env, sizeof(env));
    if (n > 0 && n < sizeof(env)) {
        return std::string(env, n);
    }
    char temp[MAX_PATH];
    DWORD t = GetTempPathA(MAX_PATH, temp);  // 末尾はバックスラッシュ付き
    std::string dir = (t > 0 && t < MAX_PATH) ? std::string(temp, t) : std::string();
    return dir + gaze::kSharedFileName;
}
}  // namespace

void EyeTracker::Initialize() {
    sharedPath_ = ResolveSharedPath();

    // 共有ファイルを開く（無ければ作成）。送信側と同じファイルを介して値を受け渡す。
    HANDLE file = CreateFileA(
        sharedPath_.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,  // 送信側と同時オープンを許可
        nullptr,
        OPEN_ALWAYS,                          // 無ければ作成、あれば開く
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    // ファイルが規定サイズ未満（新規作成含む）なら拡張する。新規分はゼロで埋まる。
    LARGE_INTEGER size{};
    bool created = false;
    if (GetFileSizeEx(file, &size)) {
        if (size.QuadPart < gaze::kSharedMemorySize) {
            LARGE_INTEGER newSize{};
            newSize.QuadPart = gaze::kSharedMemorySize;
            SetFilePointerEx(file, newSize, nullptr, FILE_BEGIN);
            SetEndOfFile(file);
            created = true;
        }
    }

    // ファイルbackedの共有メモリを作る（名前なし。ファイル経由で共有される）。
    HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READWRITE, 0, gaze::kSharedMemorySize, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        return;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, gaze::kSharedMemorySize);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        return;
    }

    // 新規作成したファイルは無効状態でゼロ初期化する（既存データがあれば触らない）。
    if (created) {
        std::memset(view, 0, gaze::kSharedMemorySize);
    }

    fileHandle_ = file;
    mapFile_ = mapping;
    view_ = view;
}

void EyeTracker::Finalize() {
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapFile_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mapFile_));
        mapFile_ = nullptr;
    }
    if (fileHandle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(fileHandle_));
        fileHandle_ = nullptr;
    }
}

void EyeTracker::Update() {
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetZ = 0.0f;

    if (view_ != nullptr) {
        // 共有メモリからローカルへコピーしてから判定する（書き込みと多少競合しても破綻しない）。
        gaze::GazePacket packet{};
        std::memcpy(&packet, view_, sizeof(packet));

        // 診断用に生情報を記録する（ImGuiで接続トラブルを切り分けるため）。
        dbgMagicOk_ = (packet.magic == gaze::kMagic);
        dbgValidFlag_ = (packet.valid != 0);
        dbgRawFrameId_ = packet.frameId;

        if (packet.magic == gaze::kMagic && packet.valid != 0) {
            if (packet.frameId != lastFrameId_) {
                lastFrameId_ = packet.frameId;
                staleCount_ = 0;
            } else {
                ++staleCount_;
            }
            connected_ = (staleCount_ < kStaleLimit);

            if (connected_) {
                targetX = Clamp(packet.gazeX, -1.0f, 1.0f);
                targetY = Clamp(packet.gazeY, -1.0f, 1.0f);
                targetZ = Clamp(packet.headZ, -1.0f, 1.0f);
            }
        } else {
            // 無効パケット：切断扱いにして中央へ戻す。
            staleCount_ = kStaleLimit;
            connected_ = false;
        }
    } else {
        connected_ = false;
    }

    // ローパスで平滑化（切断時はtarget=0なので滑らかに中央へ復帰する）。
    smoothX_ += (targetX - smoothX_) * smoothing_;
    smoothY_ += (targetY - smoothY_) * smoothing_;
    smoothZ_ += (targetZ - smoothZ_) * smoothing_;
}
