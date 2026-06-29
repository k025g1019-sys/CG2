#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

class FaceTracker;  // フレームを渡して顔検出させる任意の連携先（前方宣言でwinrt依存を持ち込まない）

// ノートPC内蔵カメラ（Webカメラ）の映像をMedia Foundationで取得し、DX12テクスチャへ転送して
// 画面の指定矩形へ描画する。フレーム取得はワーカースレッドで行い、描画スレッドは最新フレームのみを
// アップロードするため、カメラFPSと描画FPSが分離される。
//
// カメラが無い／開けない場合はIsAvailable()==falseのまま安全に無効化される（クラッシュしない）。
class CameraCapture {
public:
    // device：GPUリソース生成 / srvHeap：共有のシェーダ可視SRVヒープ / srvIndex：このクラスが使う先頭index。
    // 参照を保持するだけで、この時点ではカメラを開かない（プライバシー配慮＋視線プロセスとの競合回避）。
    // 実際のオープンは最初のUpdateTexture()呼び出し（＝実カメラ表示ONの最初のフレーム）で行う。
    void Initialize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, uint32_t srvIndex);

    // ワーカースレッドを停止し、Media Foundationとリソースを解放する。
    void Finalize();

    // 表示の有無に関わらずカメラ取得ワーカーを起動する（視線追跡だけ使う＝映像非表示でもフレームを流す）。
    // 起動済みなら何もしない。UpdateTexture()でも内部的に呼ばれる。
    void StartCapture();

    // 取得した各カメラフレーム（BGRA）を顔検出へ渡す連携先を設定する（nullptrで無効）。
    // StartCapture()/UpdateTexture()でワーカーを起動する前に設定すること。
    void SetFaceTracker(FaceTracker* tracker) { faceTracker_ = tracker; }

    // 最新カメラフレームがあればGPUテクスチャへ反映する（描画コマンドを積む前に毎フレーム呼ぶ）。
    void UpdateTexture(ID3D12GraphicsCommandList* commandList);

    // カメラ映像を指定矩形（viewport/scissor）へレターボックスで描画する。
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect);

    // テクスチャが利用可能か（カメラが開けて解像度が確定済みか）。
    bool IsAvailable() const { return available_.load(); }

private:
    // 解像度確定後にDX12テクスチャ／SRV／アップロードバッファを生成する（描画スレッドで一度だけ）。
    void CreateTextureIfNeeded();
    // ブリット用のルートシグネチャ／PSO／パラメータCBufferを生成する。
    void CreateBlitPipeline();
    // Media Foundationでカメラを開き、フレームを取得し続ける（ワーカースレッド本体）。
    void WorkerThread();
    // テクスチャを指定状態へ遷移する（不要なら何もしない）。
    void Transition(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES after);

private:
    ID3D12Device* device_ = nullptr;
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    uint32_t srvIndex_ = 0;
    uint32_t srvDescriptorSize_ = 0;

    // 顔検出の連携先（任意）。ワーカースレッドが各フレームを渡す。所有しない。
    FaceTracker* faceTracker_ = nullptr;

    // --- ワーカースレッド ↔ 描画スレッド 共有 ---
    std::thread worker_;
    bool workerStarted_ = false;            // カメラ取得スレッドを起動済みか（遅延起動）
    std::atomic<bool> stop_{ false };       // 終了要求
    std::atomic<bool> available_{ false };  // 解像度確定＆テクスチャ生成済み
    std::atomic<bool> dimsReady_{ false };  // 解像度が確定した（テクスチャ生成のトリガ）
    std::atomic<bool> newFrame_{ false };   // 未反映の新フレームがある

    std::mutex frameMutex_;
    std::vector<uint8_t> cpuFrame_;  // 最新フレーム（top-down・BGRA・隙間なし）
    int frameWidth_ = 0;
    int frameHeight_ = 0;

    // --- GPUリソース（描画スレッドが生成・所有）---
    Microsoft::WRL::ComPtr<ID3D12Resource> texture_;  // DEFAULT, B8G8R8A8_UNORM
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_;   // UPLOAD（常時Map）
    uint8_t* uploadPtr_ = nullptr;
    uint64_t uploadRowPitch_ = 0;  // GetCopyableFootprintsの行ピッチ（256境界）
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};  // CopyTextureRegion用
    D3D12_RESOURCE_STATES textureState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu_{};
    bool textureCreated_ = false;
    bool needsCopy_ = false;  // uploadの内容をテクスチャへ転送する必要がある

    // --- ブリット用パイプライン ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> blitRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blitPSO_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blitParamsResource_;

    // 合成PSへ渡すレターボックス／反転パラメータ（HLSLのcbufferと一致させる）。
    struct BlitParams {
        float fitX = 1.0f;    // カメラ像を矩形内に収める水平スケール（quad UV単位）
        float fitY = 1.0f;    // 同・垂直
        float offsetX = 0.0f; // 収め先の左下オフセット（quad UV単位）
        float offsetY = 0.0f;
        int32_t mirror = 1;   // 1で左右反転（内蔵カメラの自分視点を自然にする）
        float pad0 = 0.0f;
        float pad1 = 0.0f;
        float pad2 = 0.0f;
    };
    BlitParams* blitParamsData_ = nullptr;
};
