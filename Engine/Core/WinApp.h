#pragma once

#include <Windows.h>
#include <cstdint>

class WinApp {
public:
    static constexpr int32_t kClientWidth = 1280;
    static constexpr int32_t kClientHeight = 720;

public:
    static WinApp* GetInstance();

    void Initialize();

    bool ProcessMessage();

    int32_t GetClientWidth() const { return clientWidth_; }
    int32_t GetClientHeight() const { return clientHeight_; }

    // 直近のフレームでウィンドウサイズが変化したか
    bool IsSizeChanged() const { return sizeChanged_; }
    // サイズ変化フラグを下ろす（リサイズ処理後に呼ぶ）
    void ClearSizeChangedFlag() { sizeChanged_ = false; }

    // ボーダレス全画面（枠なしでモニタ全体を覆う）の切り替え。
    // 物理パララックスバリア／レンチキュラーをネイティブ解像度で1:1整列させるために使う。
    // サイズ変更はWM_SIZE経由でsizeChanged_が立ち、既存のリサイズ処理がスワップチェーン等を作り直す。
    void SetFullscreen(bool enable);
    void ToggleFullscreen() { SetFullscreen(!fullscreen_); }
    bool IsFullscreen() const { return fullscreen_; }

    HWND GetHwnd() const {
        return hwnd_;
    }

private:
    WinApp() = default;
    ~WinApp() = default;

    WinApp(const WinApp&) = delete;
    WinApp& operator=(const WinApp&) = delete;

private:
    HWND hwnd_ = nullptr;
    WNDCLASS wc_{};

    // 現在のクライアント領域サイズ（WM_SIZEで更新される）
    int32_t clientWidth_ = kClientWidth;
    int32_t clientHeight_ = kClientHeight;
    // サイズが変化したフレームでtrueになる
    bool sizeChanged_ = false;

    // --- ボーダレス全画面の状態。復帰用に元のスタイル・矩形を保存する ---
    bool fullscreen_ = false;
    LONG_PTR savedStyle_ = 0;
    LONG_PTR savedExStyle_ = 0;
    RECT savedWindowRect_{};

private:
    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT msg,
        WPARAM wparam,
        LPARAM lparam
    );
};