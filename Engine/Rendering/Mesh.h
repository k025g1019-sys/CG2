#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <wrl.h>

#include "Engine/Math/Vector3.h"
#include "Engine/Rendering/VertexData.h"

/// <summary>
/// 頂点（＋任意でインデックス）バッファと、カリング／ピッキング用の
/// ローカル空間バウンディング球を持つメッシュ。
/// 頂点バッファはMapしたままにするため、GetMappedVerticesで直接編集できる。
/// </summary>
class Mesh {
public:

    // 頂点配列から生成する（verticesがnullptrなら領域確保のみ）
    void Create(ID3D12Device* device, const VertexData* vertices, uint32_t vertexCount);

    // 頂点＋インデックス配列から生成する
    void Create(
        ID3D12Device* device,
        const VertexData* vertices, uint32_t vertexCount,
        const uint32_t* indices, uint32_t indexCount);

    // OBJファイルから生成する
    void CreateFromObj(ID3D12Device* device, const std::string& directoryPath, const std::string& filename);

    // 緯度経度分割の球（半径1・原点中心）を生成する
    void CreateSphere(ID3D12Device* device, uint32_t subdivision);

    // 頂点（インデックスがあればインデックス）バッファを設定して描画コマンドを積む
    void Draw(ID3D12GraphicsCommandList* commandList) const;

    // Map済み頂点への書き込みアクセス（ImGuiでの頂点編集用）
    VertexData* GetMappedVertices() { return mappedVertices_; }

    uint32_t GetVertexCount() const { return vertexCount_; }

    // ローカル空間のバウンディング球（Create時に頂点から算出）
    const Vector3& GetLocalCenter() const { return localCenter_; }
    float GetLocalRadius() const { return localRadius_; }

    // 頂点編集後にバウンディング球を再計算する
    void RecomputeBoundingSphere();

private:

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    VertexData* mappedVertices_ = nullptr;
    uint32_t vertexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW ibv_{};
    uint32_t indexCount_ = 0;

    Vector3 localCenter_{ 0.0f, 0.0f, 0.0f };
    float localRadius_ = 0.0f;
};
