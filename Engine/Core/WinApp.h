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

    int32_t GetClientWidth() const { return kClientWidth; }
    int32_t GetClientHeight() const { return kClientHeight; }

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

private:
    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT msg,
        WPARAM wparam,
        LPARAM lparam
    );
};