#include "PipelineManager.h"
#include <cassert>

ID3D12PipelineState* PipelineManager::CreateGraphicsPipeline(const PipelineConfig& config) {
    ID3D12Device* device = config.device;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = config.rootSignature;
    desc.InputLayout = config.inputLayout;
    desc.BlendState = config.blendDesc;
    desc.RasterizerState = config.rasterizerDesc;
    desc.DepthStencilState = config.depthStencilDesc;

    desc.VS = {
        config.vertexShader->GetBufferPointer(),
        config.vertexShader->GetBufferSize()
    };

    desc.PS = {
        config.pixelShader->GetBufferPointer(),
        config.pixelShader->GetBufferSize()
    };

    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = config.rtvFormat;

    desc.DSVFormat = config.dsvFormat;

    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
    assert(SUCCEEDED(hr));

    return pso;
}