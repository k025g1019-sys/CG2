#pragma once
#include <d3d12.h>
#include "TransformData3D.h"
#include "Matrix4x4.h"
#include <cstdint>

class PipelineManager;

struct VertexData;

class Object3D {
public:
    void Initialize(
        ID3D12Device* device,
        size_t vertexCount
    );

    void SetVertices(
        const void* data,
        size_t size
    );

    void Update(const Matrix4x4& viewProjection);

    void Draw(
        ID3D12GraphicsCommandList* cmd,
        PipelineManager* pipeline,
        D3D12_GPU_DESCRIPTOR_HANDLE srv
    );

public:
    Transform3D transform;

private:
    ID3D12Resource* vbResource_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};

    ID3D12Resource* wvpBuffer_ = nullptr;

    Matrix4x4* wvpData_ = nullptr;

    uint32_t vertexCount_ = 0;
};