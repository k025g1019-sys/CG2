#include "Sprite.h"
#include <cassert>
#include <cstring>

#include "Engine/Core/DirectXCore.h"
#include <VertexData.h>

ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t size);

void Sprite::Initialize(ID3D12Device* device, uint32_t vertexSize) {
    vb_ = CreateBufferResource(device, vertexSize);

    vbv_.BufferLocation = vb_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = (UINT)vertexSize;
    vbv_.StrideInBytes = sizeof(VertexData);

    transformBuffer_ = CreateBufferResource(device, sizeof(Matrix4x4));

    transformBuffer_->Map(0, nullptr, (void**)&transformData_);
    *transformData_ = MakeIdentity4x4();
}

void Sprite::SetVertices(const void* data, size_t size) {
    void* dst = nullptr;
    vb_->Map(0, nullptr, &dst);
    memcpy(dst, data, size);
}

void Sprite::Update(const Matrix4x4& cameraOrtho) {
    Matrix4x4 world =
        MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);

    *transformData_ = Multiply(world, cameraOrtho);
}

void Sprite::Draw(
    ID3D12GraphicsCommandList* cmd,
    ID3D12RootSignature* rootSig,
    ID3D12PipelineState* pso,
    D3D12_GPU_DESCRIPTOR_HANDLE srv
) {
    cmd->SetGraphicsRootSignature(rootSig);
    cmd->SetPipelineState(pso);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv_);

    cmd->SetGraphicsRootConstantBufferView(1, transformBuffer_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(2, srv);

    cmd->DrawInstanced(6, 1, 0, 0);
}