#include "Input.h"

#include <cassert>
#include <cstring>  // std::memcpy

// DirectInput関連ライブラリ（dxguid.libはGUID_SysKeyboard等のため）
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

Input* Input::GetInstance() {
    static Input instance;
    return &instance;
}

void Input::Initialize(HWND hwnd) {
    HRESULT hr;

    hwnd_ = hwnd;  // カーソル座標の変換で使う

    // DirectInput本体を生成
    hr = DirectInput8Create(
        GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void**>(directInput_.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    // キーボードデバイスを生成
    hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboard_.GetAddressOf(), nullptr);
    assert(SUCCEEDED(hr));

    // 入力データ形式を標準キーボードに設定
    hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    // 排他制御レベルを設定（フォアグラウンド時のみ・非排他）
    hr = keyboard_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));

    // マウスデバイスを生成
    hr = directInput_->CreateDevice(GUID_SysMouse, mouse_.GetAddressOf(), nullptr);
    assert(SUCCEEDED(hr));

    // 入力データ形式をマウス（ボタン8個・ホイール対応）に設定
    hr = mouse_->SetDataFormat(&c_dfDIMouse2);
    assert(SUCCEEDED(hr));

    // 排他制御レベルを設定（フォアグラウンド時のみ・非排他）
    hr = mouse_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));
}

void Input::Finalize() {
    if (mouse_) {
        mouse_->Unacquire();
    }
    mouse_.Reset();
    if (keyboard_) {
        keyboard_->Unacquire();
    }
    keyboard_.Reset();
    directInput_.Reset();
}

void Input::Update() {
    // 前フレームの状態を退避（エッジ検出用）
    std::memcpy(keyPre_, key_, sizeof(key_));
    mouseStatePre_ = mouseState_;

    // キーボードの使用を開始（フォーカス喪失からの復帰のため毎フレーム呼ぶ）
    keyboard_->Acquire();

    // 全キーの入力状態をまとめて取得
    keyboard_->GetDeviceState(static_cast<DWORD>(sizeof(key_)), key_);

    // マウスの状態を取得（フォーカス喪失時は移動量が残らないようゼロクリアする）
    if (FAILED(mouse_->Acquire()) ||
        FAILED(mouse_->GetDeviceState(static_cast<DWORD>(sizeof(DIMOUSESTATE2)), &mouseState_))) {
        mouseState_ = {};
    }
}

bool Input::IsPress(uint8_t keyNumber) const {
    return (key_[keyNumber] & 0x80) != 0;
}

bool Input::IsTrigger(uint8_t keyNumber) const {
    // 今フレームは押下、前フレームは非押下
    return (key_[keyNumber] & 0x80) != 0 && (keyPre_[keyNumber] & 0x80) == 0;
}

bool Input::IsRelease(uint8_t keyNumber) const {
    // 今フレームは非押下、前フレームは押下
    return (key_[keyNumber] & 0x80) == 0 && (keyPre_[keyNumber] & 0x80) != 0;
}

bool Input::IsMousePress(int button) const {
    return (mouseState_.rgbButtons[button] & 0x80) != 0;
}

bool Input::IsMouseTrigger(int button) const {
    // 今フレームは押下、前フレームは非押下
    return (mouseState_.rgbButtons[button] & 0x80) != 0 && (mouseStatePre_.rgbButtons[button] & 0x80) == 0;
}

bool Input::IsMouseRelease(int button) const {
    // 今フレームは非押下、前フレームは押下
    return (mouseState_.rgbButtons[button] & 0x80) == 0 && (mouseStatePre_.rgbButtons[button] & 0x80) != 0;
}

Vector2 Input::GetMouseMove() const {
    return { static_cast<float>(mouseState_.lX), static_cast<float>(mouseState_.lY) };
}

float Input::GetWheel() const {
    return static_cast<float>(mouseState_.lZ);
}

Vector2 Input::GetMousePosition() const {
    POINT point = {};
    GetCursorPos(&point);
    ScreenToClient(hwnd_, &point);
    return { static_cast<float>(point.x), static_cast<float>(point.y) };
}
