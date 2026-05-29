#pragma once
#include <d3d12.h>

class PipelineManager {
public:
    void Initialize(ID3D12Device* device);

    ID3D12PipelineState* GetPipeline() const { return pso_; }
    ID3D12RootSignature* GetRootSignature() const { return root_; }

private:
    void CreateRootSignature(ID3D12Device* device);
    void CreatePSO(ID3D12Device* device);

private:
    ID3D12RootSignature* root_ = nullptr;
    ID3D12PipelineState* pso_ = nullptr;
};