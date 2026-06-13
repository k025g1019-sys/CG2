#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <dxcapi.h>

class PipelineManager {
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

    // このプロジェクト標準のInputLayout/各種Stateで描画パイプラインを生成する
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateStandardPipeline(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        IDxcBlob* vertexShader,
        IDxcBlob* pixelShader);
};