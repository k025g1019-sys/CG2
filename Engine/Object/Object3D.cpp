#include "Object3D.h"

#include <cassert>
#include <cstring>

#include "VertexData.h"
#include "PipelineManager.h"

extern ID3D12Resource* CreateBufferResource(
    ID3D12Device* device,
    size_t sizeInBytes
);

void Object3D::Initialize(
    ID3D12Device* device,
    size_t vertexCount
) {
    vertexCount_ = static_cast<uint32_t>(vertexCount);

    size_t bufferSize =
        sizeof(VertexData) * vertexCount;

    vbResource_ =
        CreateBufferResource(device, bufferSize);

    vbv_.BufferLocation =
        vbResource_->GetGPUVirtualAddress();

    vbv_.SizeInBytes =
        static_cast<UINT>(bufferSize);

    vbv_.StrideInBytes =
        sizeof(VertexData);

    // WVP Buffer
    wvpBuffer_ =
        CreateBufferResource(
        device,
        sizeof(Matrix4x4)
        );

    wvpBuffer_->Map(
        0,
        nullptr,
        reinterpret_cast<void**>(&wvpData_)
    );

    *wvpData_ = MakeIdentity4x4();
}

void Object3D::SetVertices(
    const void* data,
    size_t size
) {
    void* dst = nullptr;

    vbResource_->Map(
        0,
        nullptr,
        &dst
    );

    memcpy(dst, data, size);

    vbResource_->Unmap(0, nullptr);
}

void Object3D::Update(
    const Matrix4x4& viewProjection
) {
    Matrix4x4 world =
        MakeAffineMatrix(
        transform.scale,
        transform.rotate,
        transform.translate
        );

    *wvpData_ =
        Multiply(world, viewProjection);
}

void Object3D::Draw(
    ID3D12GraphicsCommandList* cmd,
    PipelineManager* pipeline,
    D3D12_GPU_DESCRIPTOR_HANDLE srv
) {
    cmd->SetGraphicsRootSignature(
        pipeline->GetRootSignature()
    );

    cmd->SetPipelineState(
        pipeline->GetPipeline()
    );

    cmd->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );

    cmd->IASetVertexBuffers(
        0,
        1,
        &vbv_
    );

    // RootParameter 1 = WVP
    cmd->SetGraphicsRootConstantBufferView(
        1,
        wvpBuffer_->GetGPUVirtualAddress()
    );

    // RootParameter 2 = Texture
    cmd->SetGraphicsRootDescriptorTable(
        2,
        srv
    );

    cmd->DrawInstanced(
        vertexCount_,
        1,
        0,
        0
    );
}