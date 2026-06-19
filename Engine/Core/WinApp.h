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

private:
    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT msg,
        WPARAM wparam,
        LPARAM lparam
    );
};