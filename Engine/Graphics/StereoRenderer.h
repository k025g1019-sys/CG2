#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

/// <summary>
/// 立体視レンダラ。シーンをオフスクリーンの「ビュー用ターゲット」へ視点数ぶん描画し、
/// 最後にフルスクリーンの合成パスでバックバッファへ出力する。
///
/// Step2：2視点（左=view0 / 右=view1）を描画し、ImGuiで選んだ方式で合成する。
///   ・アナグリフ（左→赤, 右→シアン）
///   ・左視点のみ／右視点のみ（メガネ無しで視点の差を目視確認するデバッグ用）
/// 以降の段でパララックスバリア → N視点＋レンチキュラー と拡張する。
/// </summary>
class StereoRenderer {
public:
    // 合成方式。ImGuiで切り替える。値はシェーダーのgModeと一致させる。
    enum class DisplayMode : int {
        Anaglyph = 0,        // 左→赤チャンネル / 右→緑青チャンネル
        LeftEyeOnly = 1,     // 左視点のみ（デバッグ）
        RightEyeOnly = 2,    // 右視点のみ（デバッグ）
        ParallaxBarrier = 3, // パララックスバリア（列インターリーブ。物理シート前提）
        Lenticular = 4,      // レンチキュラー（斜めサブピクセル織り込み。物理シート前提）
    };

    /// <summary>
    /// 初期化する。
    /// </summary>
    /// <param name="device">リソース・パイプライン生成に使うデバイス</param>
    /// <param name="width">描画解像度（バックバッファと同じ）</param>
    /// <param name="height">描画解像度（バックバッファと同じ）</param>
    /// <param name="srvDescriptorHeap">ビューSRVを置く共有のシェーダ可視CBV/SRV/UAVヒープ</param>
    /// <param name="srvStartIndex">上記ヒープ内でこのレンダラが使える先頭index（kViewCount個を連続使用）</param>
    void Initialize(
        ID3D12Device* device,
        int32_t width,
        int32_t height,
        ID3D12DescriptorHeap* srvDescriptorHeap,
        uint32_t srvStartIndex);

    /// <summary>
    /// ウィンドウサイズ変更時にオフスクリーンターゲットを作り直す（GPU完了待ちは呼び出し側で実施済みの前提）。
    /// </summary>
    void Resize(int32_t width, int32_t height);

    /// <summary>
    /// 指定ビューをレンダーターゲットにし、色・深度をクリアする（シーン描画の直前に呼ぶ）。
    /// </summary>
    void BeginView(ID3D12GraphicsCommandList* commandList, uint32_t viewIndex);

    /// <summary>
    /// 全ビューをSRVへ遷移し、現在の方式でバックバッファへフルスクリーン合成する。
    /// </summary>
    /// <param name="backBufferRTV">出力先（現在のバックバッファのRTVハンドル）</param>
    /// <param name="viewport">バックバッファのビューポート</param>
    /// <param name="scissorRect">バックバッファのシザー矩形</param>
    void Composite(
        ID3D12GraphicsCommandList* commandList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect);

#ifdef USE_IMGUI
    // 合成方式を切り替えるUI
    void DrawImGui();
#endif

    /// <summary>視点数（Step2では2）。</summary>
    static uint32_t GetViewCount() { return kViewCount; }

private:
    // オフスクリーンの色／深度リソースと、そのRTV/SRV/DSVを生成する（リサイズ時の作り直しにも使う）。
    void CreateOffscreenTargets(int32_t width, int32_t height);

    // 合成用のルートシグネチャ・PSO・パラメータCBufferを生成する（シェーダーもここでコンパイルする）。
    void CreatePipeline();

private:
    // ビュー数。レンチキュラーの視点数に合わせて固定確保する（毎フレーム全視点を描画）。
    // アナグリフ／バリア／左右単独は左右端の2視点（view0 / view kViewCount-1）を使う。
    static const uint32_t kViewCount = 8;

    // ビュー用ターゲットの色フォーマット（既存の描画PSOに合わせてsRGB）。
    static const DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    // 合成PSへ渡すパラメータ（HLSLのcbufferと一致させる。16バイト×2）
    struct CompositeParams {
        int32_t mode = 0;
        float barrierPitch = 2.0f;
        float barrierPhase = 0.0f;
        int32_t swapEyes = 0;
        float lensPitch = 8.0f;   // レンズ1周期あたりのサブピクセル数
        float lensSlant = 0.0f;   // 1行あたりの水平サブピクセルずれ（0=垂直レンズ）
        float lensOffset = 0.0f;  // 位相オフセット（サブピクセル）
        int32_t viewCount = static_cast<int32_t>(kViewCount);
    };

    ID3D12Device* device_ = nullptr;

    int32_t width_ = 0;
    int32_t height_ = 0;

    // 外部所有の共有SRVヒープ（シェーダ可視）と、このレンダラが使う先頭index
    ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
    uint32_t srvStartIndex_ = 0;
    uint32_t srvDescriptorSize_ = 0;

    // --- オフスクリーンのビュー用ターゲット ---
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    uint32_t rtvDescriptorSize_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> viewColorResources_[kViewCount];
    D3D12_CPU_DESCRIPTOR_HANDLE viewRTVHandles_[kViewCount]{};
    D3D12_GPU_DESCRIPTOR_HANDLE viewSRVHandlesGPU_[kViewCount]{};
    D3D12_RESOURCE_STATES viewStates_[kViewCount]{};

    // 深度バッファはビュー間で共有（視点ごとにクリアして使い回す）
    Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_;

    // --- 合成パイプライン ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> compositeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePSO_;

    // 合成パラメータCBuffer（mode等）。毎フレーム書き込む。
    Microsoft::WRL::ComPtr<ID3D12Resource> compositeParamsResource_;
    CompositeParams* compositeParamsData_ = nullptr;

    // 現在の合成方式
    DisplayMode mode_ = DisplayMode::Anaglyph;

    // パララックスバリアのキャリブレーション（物理シートに合わせて調整する）
    float barrierPitch_ = 2.0f;  // 1周期(左+右)あたりのスクリーンピクセル数
    float barrierPhase_ = 0.0f;  // パターンの水平オフセット(px)。スリット位置へ合わせる
    bool swapEyes_ = false;      // 左右の割り当てを入れ替える（擬似立体／視点順の補正）

    // レンチキュラーのキャリブレーション（物理シートに合わせて調整する）
    float lensPitch_ = 8.0f;     // レンズ1周期あたりのサブピクセル数
    float lensSlant_ = 0.0f;     // 1行あたりの水平サブピクセルずれ（0=垂直レンズ）
    float lensOffset_ = 0.0f;    // 位相オフセット（サブピクセル）。レンズ位置へ合わせる
};
