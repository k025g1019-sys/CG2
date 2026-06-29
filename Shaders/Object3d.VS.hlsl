#include "Object3d.hlsli"

// オブジェクトごとのワールド行列（視点に依存しない）
struct TransformationMatrix
{
    float4x4 World;
};

cbuffer TransformationMatrixBuffer : register(b0)
{
    TransformationMatrix gTransformationMatrix;
};

// 視点ごとのビュー射影行列（立体視で視点ごとに差し替える）
cbuffer ViewProjectionBuffer : register(b1)
{
    float4x4 gViewProjection;
};

struct VertexShaderInput {
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};
VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    float4 worldPosition = mul(input.position, gTransformationMatrix.World);
    output.position = mul(worldPosition, gViewProjection);
    output.texcoord = input.texcoord;

    output.normal =
    normalize(
        mul(input.normal,
            (float3x3) gTransformationMatrix.World)
    );
    return output;
}
