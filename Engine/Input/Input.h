#pragma once

#define DIRECTINPUT_VERSION 0x0800  // DirectInputのバージョン指定（dinput.hより前に必須）
#include <Windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <wrl.h>

#include <cstdint>

#include "Engine/Math/Vector2.h"

// マウスボタンのインデックス（IsMousePress等に渡す）
enum MouseButton {
    kMouseLeft = 0,
    kMouseRight = 1,
    kMouseMiddle = 2,
};

// ゲームパッドのボタン（IsPadPress等に渡す）。値はXINPUT_GAMEPAD_*のビットフラグ
enum PadButton {
    kPadUp            = XINPUT_GAMEPAD_DPAD_UP,
    kPadDown          = XINPUT_GAMEPAD_DPAD_DOWN,
    kPadLeft          = XINPUT_GAMEPAD_DPAD_LEFT,
    kPadRight         = XINPUT_GAMEPAD_DPAD_RIGHT,
    kPadStart         = XINPUT_GAMEPAD_START,
    kPadBack          = XINPUT_GAMEPAD_BACK,
    kPadLeftThumb     = XINPUT_GAMEPAD_LEFT_THUMB,      // 左スティック押し込み
    kPadRightThumb    = XINPUT_GAMEPAD_RIGHT_THUMB,     // 右スティック押し込み
    kPadLeftShoulder  = XINPUT_GAMEPAD_LEFT_SHOULDER,   // LB
    kPadRightShoulder = XINPUT_GAMEPAD_RIGHT_SHOULDER,  // RB
    kPadA             = XINPUT_GAMEPAD_A,
    kPadB             = XINPUT_GAMEPAD_B,
    kPadX             = XINPUT_GAMEPAD_X,
    kPadY             = XINPUT_GAMEPAD_Y,
};

/// <summary>
/// キーボード・マウス（DirectInput）とゲームパッド（XInput）の入力を管理するシングルトン。
/// 毎フレームUpdateを呼び、IsPress / IsTrigger / IsRelease で任意キーの状態を問い合わせる。
/// キーごとに個別に問い合わせられるため、複数キーの同時入力に対応している。
/// マウスはボタン状態・相対移動量・ホイール量・カーソル位置を取得できる。
/// ゲームパッドはXboxコントローラーのボタン・トリガー・スティックの取得と、振動（即時／時間指定）に対応する。
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

    // --- マウス ---

    // 押している間ずっとtrueを返す（buttonはMouseButton）
    bool IsMousePress(int button) const;

    // 押した瞬間のフレームだけtrueを返す
    bool IsMouseTrigger(int button) const;

    // 離した瞬間のフレームだけtrueを返す
    bool IsMouseRelease(int button) const;

    // 今フレームの相対移動量（x:右が正 / y:下が正）を返す
    Vector2 GetMouseMove() const;

    // 今フレームのホイール回転量を返す（奥に回すと正 / 手前に回すと負）
    float GetWheel() const;

    // カーソルのクライアント座標（左上原点・ピクセル）を返す
    Vector2 GetMousePosition() const;

    // --- ゲームパッド（XInput） ---
    // playerIndexは0〜3（XUSER_MAX_COUNT-1）。既定は0（プレイヤー1）。

    // 指定プレイヤーのコントローラーが接続されているか
    bool IsPadConnected(int playerIndex = 0) const;

    // 押している間ずっとtrueを返す（buttonはPadButton）
    bool IsPadPress(int button, int playerIndex = 0) const;

    // 押した瞬間のフレームだけtrueを返す
    bool IsPadTrigger(int button, int playerIndex = 0) const;

    // 離した瞬間のフレームだけtrueを返す
    bool IsPadRelease(int button, int playerIndex = 0) const;

    // 左/右トリガーの踏み込み量（0.0〜1.0）を返す
    float GetLeftTrigger(int playerIndex = 0) const;
    float GetRightTrigger(int playerIndex = 0) const;

    // 左/右スティックの傾き（各成分-1.0〜1.0・上が正Y）を返す。デッドゾーン処理済み
    Vector2 GetLeftStick(int playerIndex = 0) const;
    Vector2 GetRightStick(int playerIndex = 0) const;

    // --- 振動 ---

    /// <summary>
    /// 振動を即時に設定する（StopVibrationを呼ぶか別の値で上書きするまで持続）
    /// </summary>
    /// <param name="leftMotor">左モーター=低周波/重い振動（0.0〜1.0）</param>
    /// <param name="rightMotor">右モーター=高周波/軽い振動（0.0〜1.0）</param>
    void SetVibration(float leftMotor, float rightMotor, int playerIndex = 0);

    /// <summary>
    /// 振動を設定し、指定秒数後に自動停止する（残り時間はUpdateで管理）
    /// </summary>
    /// <param name="seconds">振動を継続する秒数（0以下なら即停止）</param>
    void SetVibrationForTime(float leftMotor, float rightMotor, float seconds, int playerIndex = 0);

    // 振動を停止する
    void StopVibration(int playerIndex = 0);

private:

    Input() = default;

    ~Input() = default;

    Input(const Input&) = delete;

    Input& operator=(const Input&) = delete;

    // スティックの生値にデッドゾーンを適用し、各成分-1.0〜1.0へ正規化する
    static Vector2 ApplyStickDeadzone(short x, short y, float deadzone);

    // 振動を0.0〜1.0にクランプして即時適用する（XInputSetState）
    void ApplyVibration(int playerIndex, float leftMotor, float rightMotor);

private:

    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

    HWND hwnd_ = nullptr;    // カーソル位置の変換（ScreenToClient）に使う

    BYTE key_[256] = {};     // 今フレームの全キー状態（押下でビット0x80が立つ）
    BYTE keyPre_[256] = {};  // 前フレームの全キー状態

    DIMOUSESTATE2 mouseState_ = {};     // 今フレームのマウス状態（ボタン・相対移動・ホイール）
    DIMOUSESTATE2 mouseStatePre_ = {};  // 前フレームのマウス状態

    // --- ゲームパッド（XInput） ---
    XINPUT_STATE padState_[XUSER_MAX_COUNT] = {};     // 今フレームのパッド状態（プレイヤー別）
    XINPUT_STATE padStatePre_[XUSER_MAX_COUNT] = {};  // 前フレームのパッド状態
    bool padConnected_[XUSER_MAX_COUNT] = {};         // 各プレイヤーの接続状態
    float vibrationTimer_[XUSER_MAX_COUNT] = {};      // 振動の自動停止までの残り秒数（0=自動停止なし）

    LARGE_INTEGER perfFrequency_ = {};  // 高分解能タイマーの周波数（振動の時間管理用）
    LARGE_INTEGER lastCounter_ = {};    // 前回Update時のカウンタ（デルタ時間算出用）
};
