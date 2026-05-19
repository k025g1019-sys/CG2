#define _USE_MATH_DEFINES
#include "Sphere.h"
#include <cmath>
#include <cassert>
#include <d3d12.h>
#include "Matrix4x4.h"
#include "VertexData.h"

extern ID3D12Resource* CreateBufferResource(ID3D12Device*, size_t);

void Sphere::Initialize(ID3D12Device* device, int subdivision) {
    subdivision_ = subdivision;
    GenerateVertices(subdivision_);

    // GPUバッファ作成
    vertexResource_ = CreateBufferResource(device, vertices_.size() * sizeof(VertexData));

    VertexData* mapped = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    memcpy(mapped, vertices_.data(), vertices_.size() * sizeof(VertexData));
}

void Sphere::GenerateVertices(int subdivision) {
    vertices_.clear();

    const float pi = 3.1415926535f;
    float lonStep = 2.0f * pi / subdivision;
    float latStep = pi / subdivision;

    for (int lat = 0; lat < subdivision; ++lat) {
        float lat0 = -pi / 2.0f + lat * latStep;
        float lat1 = -pi / 2.0f + (lat + 1) * latStep;

        for (int lon = 0; lon < subdivision; ++lon) {
            float lon0 = lon * lonStep;
            float lon1 = (lon + 1) * lonStep;

            Vector3 p0 = { cos(lat0) * cos(lon0), sin(lat0), cos(lat0) * sin(lon0) };
            Vector3 p1 = { cos(lat1) * cos(lon0), sin(lat1), cos(lat1) * sin(lon0) };
            Vector3 p2 = { cos(lat1) * cos(lon1), sin(lat1), cos(lat1) * sin(lon1) };
            Vector3 p3 = { cos(lat0) * cos(lon1), sin(lat0), cos(lat0) * sin(lon1) };

            VertexData v0{ {p0.x,p0.y,p0.z,1}, {0,1} };
            VertexData v1{ {p1.x,p1.y,p1.z,1}, {0,0} };
            VertexData v2{ {p2.x,p2.y,p2.z,1}, {1,0} };
            VertexData v3{ {p3.x,p3.y,p3.z,1}, {1,1} };

            // 2 triangles
            vertices_.push_back(v0);
            vertices_.push_back(v1);
            vertices_.push_back(v2);

            vertices_.push_back(v0);
            vertices_.push_back(v2);
            vertices_.push_back(v3);
        }
    }
}

void Sphere::Draw(ID3D12GraphicsCommandList* commandList) {
    vbv_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = UINT(vertices_.size() * sizeof(VertexData));
    vbv_.StrideInBytes = sizeof(VertexData);

    commandList->IASetVertexBuffers(0, 1, &vbv_);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(UINT(vertices_.size()), 1, 0, 0);
}

#pragma region Sphere

void Sphere::Update() {}

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
void Sphere::DrawImGui() {
	ImGui::DragFloat3("Center", &center_.x, 0.01f);
	ImGui::DragFloat("Radius", &radius_, 0.01f);
	ImGui::Separator();
}
#endif

#pragma endregion
