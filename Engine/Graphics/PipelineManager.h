#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <dxcapi.h>

/// <summary>
/// RootSignatureと用途別PSOを一括生成・所有するレジストリ。
/// 描画側はGet(Pipeline::～)でPSOを取得するだけでよく、
/// シェーダーコンパイルやPSO生成の詳細を知る必要がない。
/// </summary>
class PipelineManager {
public:

    // 用途別のPSO（Initializeで一括生成する）
    enum class Pipeline {
        kStandard,  // 裏面カリング（通常の3Dオブジェクト・スプライト）
        kNoCull,    // カリング無効（内側から見る天球など）

        kCount,     // PSOの総数（enumの末尾に置くこと）
    };

    static PipelineManager* GetInstance();

    // 標準シェーダーをコンパイルし、RootSignatureと全PSOを生成する
    // （ShaderCompiler::Initializeの後に呼ぶ）
    void Initialize(ID3D12Device* device);

    // 全PSOとRootSignatureを解放する（リソースリークチェックより前に呼ぶ）
    void Finalize();

    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }

    ID3D12PipelineState* Get(Pipeline pipeline) const;

public:

    struct PipelineConfig {
        ID3D12Device* device = nullptr;
        ID3D12RootSignature* rootSignature = nullptr;
        D3D12_INPUT_LAYOUT_DESC inputLayout{};
        D3D12_BLEND_DESC blendDesc{};
        D3D12_RASTERIZER_DESC rasterizerDesc{};
        D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        DXGI_FORMAT dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

        Microsoft::WRL::ComPtr<IDxcBlob> vertexShader;
        Microsoft::WRL::ComPtr<IDxcBlob> pixelShader;
    };

    static Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipeline(const PipelineConfig& config);

    // このプロジェクト標準のRootSignatureを生成する
    // (b0:Material[PS], b0:Transform[VS], b1:Light[PS], t0:Texture[PS], s0:Sampler)
    static Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device);

    // このプロジェクト標準のInputLayout/各種Stateで描画パイプラインを生成する。
    // cullModeで裏面カリングの挙動を変えられる（天球は内側から見るためNONEを指定する）。
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateStandardPipeline(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        IDxcBlob* vertexShader,
        IDxcBlob* pixelShader,
        D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_BACK);

private:

    PipelineManager() = default;

    ~PipelineManager() = default;

    PipelineManager(const PipelineManager&) = delete;

    PipelineManager& operator=(const PipelineManager&) = delete;

private:

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelines_[size_t(Pipeline::kCount)];
};
