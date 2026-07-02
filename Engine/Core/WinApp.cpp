#include "Engine/Core/WinApp.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM LParam);
#endif

LRESULT CALLBACK WinApp::WindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wparam,
    LPARAM lparam
) {
#ifdef USE_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return TRUE;
    }
#endif

    switch (msg) {

    case WM_SIZE:
    // 最小化時はサイズが0になるため無視する
    if (wparam != SIZE_MINIMIZED) {
        int32_t width = LOWORD(lparam);
        int32_t height = HIWORD(lparam);
        WinApp* app = GetInstance();
        // 実際にサイズが変わったときだけフラグを立てる
        if (width != app->clientWidth_ || height != app->clientHeight_) {
            app->clientWidth_ = width;
            app->clientHeight_ = height;
            app->sizeChanged_ = true;
        }
    }
    break;

    case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

WinApp* WinApp::GetInstance() {
    static WinApp instance;
    return &instance;
}

void WinApp::Initialize() {

    wc_.lpfnWndProc = WindowProc;
    wc_.lpszClassName = L"WindowClass";
    wc_.hInstance = GetModuleHandle(nullptr);
    wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClass(&wc_);

    RECT wrc = {
        0,
        0,
        kClientWidth,
        kClientHeight
    };

    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

    hwnd_ = CreateWindow(
        wc_.lpszClassName,
        L"Window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wrc.right - wrc.left,
        wrc.bottom - wrc.top,
        nullptr,
        nullptr,
        wc_.hInstance,
        nullptr
    );

    ShowWindow(hwnd_, SW_SHOW);
}

bool WinApp::ProcessMessage() {

    MSG msg{};

    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {

        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT) {
            return false;
        }
    }

    return true;
}