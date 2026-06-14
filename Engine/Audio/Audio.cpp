#include "Audio.h"

#include <cassert>
#include <cstring>
#include <fstream>

#pragma comment(lib, "xaudio2.lib")

namespace {

    // RIFFファイルの先頭ヘッダ
    struct RiffHeader {
        char id[4];      // "RIFF"
        uint32_t size;   // ファイルサイズ-8
        char type[4];    // "WAVE"
    };

    // チャンク共通のヘッダ
    struct ChunkHeader {
        char id[4];      // チャンク識別子（"fmt "や"data"など）
        uint32_t size;   // チャンクのデータサイズ
    };

} // namespace

Audio* Audio::GetInstance() {

    static Audio instance;

    return &instance;
}

void Audio::Initialize() {

    HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);

    assert(SUCCEEDED(hr));

    // スピーカーへの出口（マスターボイス）を作る
    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);

    assert(SUCCEEDED(hr));
}

void Audio::Finalize() {

    // 各サウンドの再生用ボイスを破棄し、波形データを解放する
    for (SoundData& sound : sounds_) {

        if (sound.sourceVoice) {

            sound.sourceVoice->DestroyVoice();

            sound.sourceVoice = nullptr;
        }

        sound.buffer.clear();
    }

    sounds_.clear();

    // マスターボイスを破棄する
    if (masterVoice_) {

        masterVoice_->DestroyVoice();

        masterVoice_ = nullptr;
    }

    // XAudio2本体を解放する
    xAudio2_.Reset();
}

size_t Audio::LoadWave(const std::string& filename) {

    // ファイルをバイナリモードで開く
    std::ifstream file(filename, std::ios_base::binary);

    assert(file.is_open());

    // RIFFヘッダを読み込み、WAVEファイルであることを確認する
    RiffHeader riff{};

    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));

    assert(std::strncmp(riff.id, "RIFF", 4) == 0);

    assert(std::strncmp(riff.type, "WAVE", 4) == 0);

    SoundData sound{};

    bool foundFmt = false;

    bool foundData = false;

    // "fmt "（フォーマット）と"data"（波形本体）が揃うまでチャンクを順に読む
    while (!(foundFmt && foundData)) {

        ChunkHeader chunk{};

        file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));

        // 読み切れなかった場合はチャンクが尽きたとみなす
        if (file.gcount() < static_cast<std::streamsize>(sizeof(chunk))) {

            break;
        }

        if (std::strncmp(chunk.id, "fmt ", 4) == 0) {

            // フォーマット情報を読み込む（WAVEFORMATEXに収まる分だけ）
            uint32_t readSize =
                chunk.size < sizeof(WAVEFORMATEX) ?
                chunk.size : static_cast<uint32_t>(sizeof(WAVEFORMATEX));

            file.read(reinterpret_cast<char*>(&sound.wfex), readSize);

            // 余分なバイトがあれば読み飛ばす
            if (chunk.size > readSize) {

                file.seekg(chunk.size - readSize, std::ios_base::cur);
            }

            foundFmt = true;

        } else if (std::strncmp(chunk.id, "data", 4) == 0) {

            // 波形データ本体を読み込む
            sound.buffer.resize(chunk.size);

            file.read(reinterpret_cast<char*>(sound.buffer.data()), chunk.size);

            foundData = true;

        } else {

            // 未知のチャンク（"JUNK"や"LIST"など）は読み飛ばす
            file.seekg(chunk.size, std::ios_base::cur);
        }

        // チャンクは2バイト境界に整列するため、奇数サイズなら詰め物を1バイト飛ばす
        if (chunk.size % 2 != 0) {

            file.seekg(1, std::ios_base::cur);
        }
    }

    assert(foundFmt && foundData);

    file.close();

    // フォーマットが確定したので再生用ソースボイスを生成する（以後使い回す）
    HRESULT hr = xAudio2_->CreateSourceVoice(&sound.sourceVoice, &sound.wfex);

    assert(SUCCEEDED(hr));

    sounds_.push_back(std::move(sound));

    return sounds_.size() - 1;
}

void Audio::Play(size_t soundHandle) {

    assert(soundHandle < sounds_.size());

    SoundData& sound = sounds_[soundHandle];

    assert(sound.sourceVoice);

    // 重複させないため、再生中でも一度止めてキューを空にしてから頭出しする
    sound.sourceVoice->Stop();

    sound.sourceVoice->FlushSourceBuffers();

    // 再生する波形データを指定する
    XAUDIO2_BUFFER buf{};

    buf.pAudioData = sound.buffer.data();

    buf.AudioBytes = static_cast<UINT32>(sound.buffer.size());

    buf.Flags = XAUDIO2_END_OF_STREAM;

    HRESULT hr = sound.sourceVoice->SubmitSourceBuffer(&buf);

    assert(SUCCEEDED(hr));

    hr = sound.sourceVoice->Start();

    assert(SUCCEEDED(hr));
}

void Audio::SetVolume(size_t soundHandle, float volume) {

    assert(soundHandle < sounds_.size());

    if (sounds_[soundHandle].sourceVoice) {

        sounds_[soundHandle].sourceVoice->SetVolume(volume);
    }
}
