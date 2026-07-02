#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

#include "Engine/Rendering/ConstantBuffer.h"

/// <summary>
/// 立体視レンダラ（シングルトン）。シーンをオフスクリーンの「ビュー用ターゲット」へ視点数ぶん描画し、
/// 最後にフルスクリーンの合成パスでバックバッファへ出力する。
///
/// 非使用時のコストをほぼゼロにするため、次の設計にしている。
/// ・DisplayMode::kOff（既定）では合成もオフスクリーン描画も行わない（Frameworkが通常描画へバイパスする）
/// ・オフスクリーンターゲットはOff以外へ初めて切り替えたときに遅延確保し、Offへ戻すと解放する
/// ・描画する視点数は方式ごとの必要数だけ（アナグリフ/バリア/左右単独=2、レンチキュラー=kMaxViewCount）
/// </summary>
class StereoRenderer {
public:
    // レンチキュラーで使う最大視点数（コンパイル時定数）
    static constexpr uint32_t kMaxViewCount = 8;

    // 合成方式。ImGuiで切り替える。kOff以外の値はシェーダーのgModeと一致させる。
    enum class DisplayMode : int32_t {
        kOff = -1,             // 立体視無効（通常描画。オフスクリーン・合成パスを完全にバイパス）
        kAnaglyph = 0,         // 左→赤チャンネル / 右→緑青チャンネル
        kLeftEyeOnly = 1,      // 左視点のみ（デバッグ）
        kRightEyeOnly = 2,     // 右視点のみ（デバッグ）
        kParallaxBarrier = 3,  // パララックスバリア（列インターリーブ。物理シート前提）
        kLenticular = 4,       // レンチキュラー（斜めサブピクセル織り込み。物理シート前提）
    };

    static StereoRenderer* GetInstance();

    /// <summary>
    /// 合成パイプラインとSRVスロットの予約を行う（オフスクリーン本体はまだ確保しない）。
    /// （ShaderCompiler・DescriptorHeapManagerの初期化後に呼ぶ）
    /// </summary>
    void Initialize(ID3D12Device* device, int32_t width, int32_t height);

    // 全リソースを解放する（リソースリークチェックより前に呼ぶ）
    void Finalize();

    // 立体視が有効か（kOff以外か）。無効ならFrameworkは通常描画を行う。
    bool IsEnabled() const { return mode_ != DisplayMode::kOff; }

    // 現在の方式で描画が必要な視点数（kOff:1 / レンチキュラー:kMaxViewCount / その他:2）
    uint32_t GetActiveViewCount() const;

    /// <summary>
    /// ウィンドウサイズ変更を反映する（GPU完了待ちは呼び出し側で実施済みの前提）。
    /// 確保済みのオフスクリーンは解放され、次のBeginViewで新サイズで作り直される。
    /// </summary>
    void Resize(int32_t width, int32_t height);

    /// <summary>
    /// 指定ビューをレンダーターゲットにし、色・深度をクリアする（シーン描画の直前に呼ぶ）。
    /// オフスクリーンが未確保ならこのとき遅延確保する。
    /// </summary>
    void BeginView(ID3D12GraphicsCommandList* commandList, uint32_t viewIndex);

    /// <summary>
    /// 使用中の全ビューをSRVへ遷移し、現在の方式でバックバッファへフルスクリーン合成する。
    /// </summary>
    /// <param name="backBufferRTV">出力先（現在のバックバッファのRTVハンドル）</param>
    /// <param name="viewport">出力先のビューポート（画面分割時はゲーム表示領域）</param>
    /// <param name="scissorRect">出力先のシザー矩形</param>
    void Composite(
        ID3D12GraphicsCommandList* commandList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect);

#ifdef USE_IMGUI
    // 合成方式の切り替えとキャリブレーションUI
    void DrawImGui();
#endif

private:

    StereoRenderer() = default;

    ~StereoRenderer() = default;

    StereoRenderer(const StereoRenderer&) = delete;

    StereoRenderer& operator=(const StereoRenderer&) = delete;

private:

    // ビュー用ターゲットの色フォーマット（既存の描画PSOに合わせてsRGB）
    static const DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    // 合成PSへ渡すパラメータ（HLSLのcbufferと一致させる。16バイト×3）
    struct CompositeParams {
        int32_t mode = 0;
        float barrierPitch = 2.0f;
        float barrierPhase = 0.0f;
        int32_t swapEyes = 0;
        float lensPitch = 8.0f;   // レンズ1周期あたりのサブピクセル数
        float lensSlant = 0.0f;   // 1行あたりの水平サブピクセルずれ（0=垂直レンズ）
        float lensOffset = 0.0f;  // 位相オフセット（サブピクセル）
        int32_t viewCount = 2;    // 描画済みの視点数（Compositeで毎フレーム設定する）
        float ghostReduction = 0.15f;  // アナグリフのクロストーク相殺量（0..1）
        int32_t anaglyphGray = 0;      // 0以外でグレーアナグリフ（輝度ベース）
        float pad0 = 0.0f;
        float pad1 = 0.0f;
    };

    // 現在の視点数ぶんのオフスクリーンが無ければ作る（遅延確保。増える方向のみ）
    void EnsureTargets(uint32_t viewCount);

    // オフスクリーンと共有深度を解放する（Off切替・Resize時。GPU完了待ちは呼び出し側で実施済みの前提）
    void ReleaseTargets();

    // 合成用のルートシグネチャ・PSO・パラメータCBufferを生成する（シェーダーもここでコンパイルする）
    void CreatePipeline();

private:

    ID3D12Device* device_ = nullptr;

    int32_t width_ = 0;
    int32_t height_ = 0;

    DisplayMode mode_ = DisplayMode::kOff;  // 既定はOff（通常描画）

    // --- オフスクリーンのビュー用ターゲット（遅延確保）---
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    uint32_t rtvDescriptorSize_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> viewColorResources_[kMaxViewCount];
    D3D12_CPU_DESCRIPTOR_HANDLE viewRTVHandles_[kMaxViewCount]{};
    D3D12_RESOURCE_STATES viewStates_[kMaxViewCount]{};
    uint32_t allocatedViewCount_ = 0;  // 確保済みのターゲット数（0=未確保）

    // 深度バッファはビュー間で共有（視点ごとにクリアして使い回す）
    Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_;

    // 合成PSが参照するビューSRV（共有SRVヒープにkMaxViewCount個連続で予約する）
    uint32_t srvStartIndex_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE viewSRVTableStart_{};

    // --- 合成パス ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> compositeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePSO_;
    ConstantBuffer<CompositeParams> paramsCB_;

    // --- キャリブレーション（ImGuiで調整）---
    bool swapEyes_ = false;
    float barrierPitch_ = 2.0f;
    float barrierPhase_ = 0.0f;
    float lensPitch_ = 8.0f;
    float lensSlant_ = 0.0f;
    float lensOffset_ = 0.0f;
    float ghostReduction_ = 0.15f;  // アナグリフのクロストーク相殺量（メガネに合わせて調整）
    bool anaglyphGray_ = false;     // グレーアナグリフ（色競合・ゴーストがさらに気になる場合に）
};
