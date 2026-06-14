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
}

void Input::Finalize() {
    if (keyboard_) {
        keyboard_->Unacquire();
    }
    keyboard_.Reset();
    directInput_.Reset();
}

void Input::Update() {
    // 前フレームのキー状態を退避（エッジ検出用）
    std::memcpy(keyPre_, key_, sizeof(key_));

    // キーボードの使用を開始（フォーカス喪失からの復帰のため毎フレーム呼ぶ）
    keyboard_->Acquire();

    // 全キーの入力状態をまとめて取得
    keyboard_->GetDeviceState(static_cast<DWORD>(sizeof(key_)), key_);
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
