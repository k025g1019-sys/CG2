#include "PipelineManager.h"
#include "ShaderCompiler.h"
#include <d3dcompiler.h>

void PipelineManager::Initialize(ID3D12Device* device) {
    CreateRootSignature(device);
    CreatePSO(device);
}

void PipelineManager::CreateRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.BaseShaderRegister = 0;
    range.NumDescriptors = 1;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    D3D12_ROOT_PARAMETER params[3]{};

    // Material
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0;

    // WVP
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].Descriptor.ShaderRegister = 0;

    // Texture
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.pDescriptorRanges = &range;
    params[2].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3;
    desc.pParameters = params;

    ID3DBlob* sig = nullptr;
    ID3DBlob* err = nullptr;

    D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &sig,
        &err
    );

    device->CreateRootSignature(
        0,
        sig->GetBufferPointer(),
        sig->GetBufferSize(),
        IID_PPV_ARGS(&root_)
    );
}

void PipelineManager::CreatePSO(ID3D12Device* device) {

    ShaderCompiler* compiler = ShaderCompiler::GetInstance();

    IDxcBlob* vsBlob = compiler->Compile(
        L"Object3D.VS.hlsl",
        L"vs_6_0"
    );

    IDxcBlob* psBlob = compiler->Compile(
        L"Object3D.PS.hlsl",
        L"ps_6_0"
    );

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};

    desc.pRootSignature = root_;

    desc.VS = {
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize()
    };

    desc.PS = {
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize()
    };

    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;

    desc.InputLayout = { nullptr, 0 };

    desc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    desc.SampleDesc.Count = 1;

    device->CreateGraphicsPipelineState(
        &desc,
        IID_PPV_ARGS(&pso_)
    );

    // 重要：解放
    vsBlob->Release();
    psBlob->Release();
}