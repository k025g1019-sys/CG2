#pragma once

#include <d3d12.h>
#include <cstdint>
#include <wrl.h>

#include "TransformData3D.h"
#include "LoadObjFile.h"
#include "RenderResource.h"

struct VertexData;

// デモ用シーン。三角形・球・OBJ・スプライトと開発用ImGuiを保持する。
class GameScene {
public:
    void Initialize(ID3D12Device* device);

    // UI操作を反映した行列計算と定数バッファ更新
    void Update();

#ifdef USE_IMGUI
    // 開発用ImGuiウィンドウの構築（deviceは球の再分割時の再生成に使用）
    void DrawImGui(ID3D12Device* device);
#endif

    // 描画コマンドを積む。textureHandlesは読み込み済みテクスチャのSRV(GPUハンドル)配列
    void Draw(
        ID3D12GraphicsCommandList* commandList,
        ID3D12RootSignature* rootSignature,
        ID3D12PipelineState* pipelineState,
        ID3D12DescriptorHeap* srvDescriptorHeap,
        const D3D12_GPU_DESCRIPTOR_HANDLE* textureHandles);

private:
    // --- 三角形 ---
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceTriangle_;
    VertexData* vertexDataTriangle_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbvTriangle_{};

    // --- OBJ ---
    ModelData modelData_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceObj_;
    D3D12_VERTEX_BUFFER_VIEW vbvObj_{};

    // --- 球 ---
    uint32_t subdivision_ = 16;
    uint32_t prevSubdivision_ = 16;
    uint32_t sphereVertexCount_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSphere_;
    D3D12_VERTEX_BUFFER_VIEW vbvSphere_{};

    // --- スプライト ---
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite_;
    VertexData* vertexDataSprite_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbvSprite_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite_;
    uint32_t* indexDataSprite_ = nullptr;
    D3D12_INDEX_BUFFER_VIEW ibvSprite_{};

    // --- マテリアル / ライト / Transformリソース ---
    MaterialResource material_;        // 三角形・OBJ共通の3Dマテリアル
    MaterialResource spriteMaterial_;  // スプライト用（ライティング無効）
    DirectionalLightResource light_;
    TransformResource triangleTransform_;
    TransformResource sphereTransform_;
    TransformResource objTransform_;
    TransformResource spriteTransform_;

    // --- CPU側Transform ---
    Transform3D transformTriangle_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {2.5f, 0.0f, 0.0f} };
    Transform3D transformSphere_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform3D transformObj_{ {1.0f, 1.0f, 1.0f}, {0.0f, 3.1415f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform3D transformSprite_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform3D uvTransformSprite_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform3D cameraTransform_{ {1.0f, 1.0f, 1.0f}, {0.04f, 0.0f, 0.0f}, {0.0f, 1.7f, -10.0f} };

    // --- 使用するテクスチャの選択（textureHandlesへのindex）---
    uint32_t triangleTextureIndex_ = 0;
    uint32_t sphereTextureIndex_ = 1;
    uint32_t objTextureIndex_ = 0;
    uint32_t spriteTextureIndex_ = 0;
};
