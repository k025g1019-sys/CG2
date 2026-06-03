#pragma once

#include <d3d12.h>
#include <cstdint>

void GenerateSphere(
    uint32_t subdivision,
    ID3D12Resource* vertexResource,
    D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
    uint32_t sphereVertexCount
);