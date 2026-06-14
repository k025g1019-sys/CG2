#pragma once

#include <Windows.h>

#include <xaudio2.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// <summary>
/// XAudio2による.wavサウンドの読み込みと再生を行うシングルトン
/// </summary>
class Audio {
public:

    static Audio* GetInstance();

    // XAudio2本体とマスターボイスを生成する
    void Initialize();

    // 全サウンドとXAudio2を解放する（CoUninitializeより前に呼ぶこと）
    void Finalize();

    /// <summary>
    /// .wavファイルを読み込み、サウンドハンドルを返す
    /// </summary>
    /// <param name="filename">実行ディレクトリからの相対パス</param>
    /// <returns>Play / SetVolume に渡すハンドル</returns>
    size_t LoadWave(const std::string& filename);

    /// <summary>
    /// 再生する。既に再生中の場合は一度止めて頭から鳴らし直す（重複再生しない）
    /// </summary>
    void Play(size_t soundHandle);

    /// <summary>
    /// 音量を設定する（0.0で無音、1.0で原音、それ以上で増幅）
    /// </summary>
    void SetVolume(size_t soundHandle, float volume);

private:

    Audio() = default;

    ~Audio() = default;

    Audio(const Audio&) = delete;

    Audio& operator=(const Audio&) = delete;

private:

    // 1つのサウンドが保持するデータ
    struct SoundData {
        WAVEFORMATEX wfex{};                        // 波形フォーマット
        std::vector<BYTE> buffer;                   // 波形データ本体
        IXAudio2SourceVoice* sourceVoice = nullptr; // 再生用ボイス（使い回す）
    };

    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;

    // スピーカーへの出口。XAudio2が所有するためDestroyVoiceで破棄する
    IXAudio2MasteringVoice* masterVoice_ = nullptr;

    std::vector<SoundData> sounds_;
};
