#pragma once

#define DIRECTINPUT_VERSION 0x0800  // DirectInputのバージョン指定（dinput.hより前に必須）
#include <Windows.h>
#include <dinput.h>
#include <wrl.h>

#include <cstdint>

/// <summary>
/// DirectInputによるキーボード入力を管理するシングルトン。
/// 毎フレームUpdateを呼び、IsPress / IsTrigger / IsRelease で任意キーの状態を問い合わせる。
/// キーごとに個別に問い合わせられるため、複数キーの同時入力に対応している。
/// </summary>
class Input {
public:

    static Input* GetInstance();

    // DirectInput本体とキーボードデバイスを生成する
    void Initialize(HWND hwnd);

    // キーボードデバイスを解放する（CoUninitializeより前に呼ぶこと）
    void Finalize();

    // 毎フレーム1回呼ぶ。前フレームの状態を退避し、最新の全キー状態を取得する
    void Update();

    /// <summary>
    /// 押している間ずっとtrueを返す
    /// </summary>
    /// <param name="keyNumber">DIK_*のキーコード（例: DIK_RETURN, DIK_SPACE）</param>
    bool IsPress(uint8_t keyNumber) const;

    /// <summary>
    /// 押した瞬間のフレームだけtrueを返す（押しっぱなしでは反応しない）
    /// </summary>
    /// <param name="keyNumber">DIK_*のキーコード</param>
    bool IsTrigger(uint8_t keyNumber) const;

    /// <summary>
    /// 離した瞬間のフレームだけtrueを返す
    /// </summary>
    /// <param name="keyNumber">DIK_*のキーコード</param>
    bool IsRelease(uint8_t keyNumber) const;

private:

    Input() = default;

    ~Input() = default;

    Input(const Input&) = delete;

    Input& operator=(const Input&) = delete;

private:

    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;

    BYTE key_[256] = {};     // 今フレームの全キー状態（押下でビット0x80が立つ）
    BYTE keyPre_[256] = {};  // 前フレームの全キー状態
};
