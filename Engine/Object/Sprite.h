#pragma once
#include <d3d12.h>
#include <cstdint>
#include "Matrix4x4.h"
#include "TransformData3D.h"

class Sprite {
public:
    void Initialize(ID3D12Device* device, uint32_t vertexSize);
    void SetVertices(const void* data, size_t size);

    void Update(const Matrix4x4& cameraOrtho);

    void Draw(
        ID3D12GraphicsCommandList* cmd,
        ID3D12RootSignature* rootSig,
        ID3D12PipelineState* pso,
        D3D12_GPU_DESCRIPTOR_HANDLE srv
    );

public:
    Transform3D transform;

private:
    ID3D12Resource* vb_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};

    ID3D12Resource* transformBuffer_ = nullptr;
    Matrix4x4* transformData_ = nullptr;
};