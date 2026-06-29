#include "FaceTracker.h"

#include <chrono>
#include <vector>
#include <cstring>
#include <combaseapi.h>

// C++/WinRTのプロジェクションヘッダ。/W4 + warnings-as-errorsに引っかからないよう警告を抑止して取り込む。
#pragma warning(push, 0)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.FaceAnalysis.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma warning(pop)

// C++/WinRTのアクティベーション/文字列/例外ヘルパ（WINRT_IMPL_Ro*等）を解決する。
#pragma comment(lib, "windowsapp.lib")

using winrt::Windows::Graphics::Imaging::BitmapBounds;
using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
using winrt::Windows::Media::FaceAnalysis::DetectedFace;
using winrt::Windows::Storage::Streams::Buffer;

namespace {
// 顔検出を回す間隔（カメラFPSより落として負荷を抑える。約15Hz）。
constexpr long long kProcessIntervalMs = 66;

// 顔未検出がこの回数を超えたら「切断」とみなし中央へ戻す（約0.5秒@60fpsのUpdate）。
constexpr int kStaleLimit = 30;

// 顔ボックス幅が画面に占める割合の基準（これ付近でheadZ=0）。大きいほど近い。
constexpr float kNominalFaceWidth = 0.20f;

float Clamp(float v, float lo, float hi) {
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}
}  // namespace

// WinRT本体とスクラッチをヘッダから隠す（pimpl）。
struct FaceTracker::Impl {
    // FaceTracker(動画追跡用)はNv12しか受け付けずGray8でE_FAILになるため、Gray8を直接扱える
    // FaceDetector(静止画用)を使う。約15Hzで1枚ずつ検出する用途では精度・速度とも十分でNV12変換も不要。
    winrt::Windows::Media::FaceAnalysis::FaceDetector detector{ nullptr };
    bool createTried = false;  // 生成を一度試したか（失敗時の再試行を避ける）
    std::chrono::steady_clock::time_point lastProcess{};
    std::vector<uint8_t> gray;  // BGRA→Gray8変換のスクラッチ
};

FaceTracker::FaceTracker() : impl_(std::make_unique<Impl>()) {}
FaceTracker::~FaceTracker() { Finalize(); }

void FaceTracker::Initialize() {
    // 実体（WinRTトラッカー）はカメラのワーカースレッド上で遅延生成する（COM初期化済みスレッドで作るため）。
    // ここではImplの確保のみ（コンストラクタで済んでいる）。
}

void FaceTracker::Finalize() {
    if (!impl_) {
        return;
    }
    // WinRTオブジェクトの解放はCOM初期化済みスレッドで行う。
    // 呼び出し側（main）のスレッドが未初期化でも安全なよう、一時的に初期化してから解放する。
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl_->detector = nullptr;
    impl_->createTried = false;
    ready_.store(false);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
}

void FaceTracker::ProcessFrameBGRA(const uint8_t* bgra, int w, int h) {
    if (bgra == nullptr || w <= 0 || h <= 0 || !impl_) {
        return;
    }

    // 時間で間引く（カメラFPSのままだと検出が重い）。
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - impl_->lastProcess).count();
    if (elapsed < kProcessIntervalMs) {
        return;
    }
    impl_->lastProcess = now;

    try {
        // WinRTトラッカーを遅延生成（このワーカースレッドはCameraCaptureがCOM(MTA)初期化済み）。
        if (!impl_->createTried) {
            impl_->createTried = true;
            impl_->detector = winrt::Windows::Media::FaceAnalysis::FaceDetector::CreateAsync().get();
            ready_.store(impl_->detector != nullptr);
        }
        if (impl_->detector == nullptr) {
            return;
        }

        // --- BGRA32 → Gray8（BT.601輝度）---
        const int pixels = w * h;
        impl_->gray.resize(static_cast<size_t>(pixels));
        uint8_t* dst = impl_->gray.data();
        for (int i = 0; i < pixels; ++i) {
            const uint8_t* p = bgra + static_cast<size_t>(i) * 4;  // B,G,R,A
            const int b = p[0];
            const int g = p[1];
            const int r = p[2];
            dst[i] = static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
        }

        // --- Gray8のSoftwareBitmap → VideoFrame → 顔検出 ---
        Buffer buffer(static_cast<uint32_t>(pixels));
        buffer.Length(static_cast<uint32_t>(pixels));
        std::memcpy(buffer.data(), impl_->gray.data(), static_cast<size_t>(pixels));

        SoftwareBitmap bitmap =
            SoftwareBitmap::CreateCopyFromBuffer(buffer, BitmapPixelFormat::Gray8, w, h);

        // FaceDetectorはGray8を直接サポートする（FaceTrackerと違いNv12変換やVideoFrameが不要）。
        auto faces = impl_->detector.DetectFacesAsync(bitmap).get();
        const int count = static_cast<int>(faces.Size());
        faceCount_.store(count);

        if (count == 0) {
            rawValid_.store(false);
            return;
        }

        // 最も大きい顔（最も手前/中心とみなす）を採用する。
        BitmapBounds best{};
        uint64_t bestArea = 0;
        for (auto const& face : faces) {
            const BitmapBounds box = face.FaceBox();
            const uint64_t area = static_cast<uint64_t>(box.Width) * box.Height;
            if (area > bestArea) {
                bestArea = area;
                best = box;
            }
        }

        const float cx = best.X + best.Width * 0.5f;
        const float cy = best.Y + best.Height * 0.5f;
        const float nx = cx / static_cast<float>(w);  // 0..1（画像内）
        const float ny = cy / static_cast<float>(h);  // 0..1（画像内、下方向+）
        const float nz = static_cast<float>(best.Width) / static_cast<float>(w);

        // 内蔵カメラは鏡像。頭を自分の右へ動かすと画像内では左へ動くので (0.5 - nx) で「右が+」。
        // 縦は画像yが下向きなので (0.5 - ny) で「上が+」。
        const float gx = Clamp((0.5f - nx) * 2.0f * gain_, -1.0f, 1.0f);
        const float gy = Clamp((0.5f - ny) * 2.0f * gain_, -1.0f, 1.0f);
        const float gz = Clamp((nz - kNominalFaceWidth) * 5.0f, -1.0f, 1.0f);

        rawX_.store(gx);
        rawY_.store(gy);
        rawZ_.store(gz);
        rawValid_.store(true);
    } catch (winrt::hresult_error const&) {
        // 検出失敗（フォーマット非対応など）。今フレームは無効扱いにして継続する。
        rawValid_.store(false);
    } catch (...) {
        rawValid_.store(false);
    }
}

void FaceTracker::Update() {
    const bool valid = rawValid_.load();
    if (valid) {
        staleCount_ = 0;
    } else if (staleCount_ < kStaleLimit) {
        ++staleCount_;
    }
    connected_ = (staleCount_ < kStaleLimit);

    // 目標値：検出中は最新値、短時間の見失い中はその場で保持、切断時は中央へ戻す。
    float tx;
    float ty;
    float tz;
    if (!connected_) {
        tx = 0.0f;
        ty = 0.0f;
        tz = 0.0f;
    } else if (valid) {
        tx = rawX_.load();
        ty = rawY_.load();
        tz = rawZ_.load();
    } else {
        tx = smoothX_;
        ty = smoothY_;
        tz = smoothZ_;
    }

    // ローパスで平滑化（カメラ検出は約15Hzだが、ここはメインのフレームレートで滑らかに補間する）。
    smoothX_ += (tx - smoothX_) * smoothing_;
    smoothY_ += (ty - smoothY_) * smoothing_;
    smoothZ_ += (tz - smoothZ_) * smoothing_;
}
