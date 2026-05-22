#include "Object3d.hlsli"
cbuffer Material : register(b0)
{
    float4 color;
    int enableLighting;
};
cbuffer DirectionalLight : register(b1)
{
    float4 lightColor;
    float3 direction;
    float intensity;
    float padding[3];
};
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord.xy);
    
    float4 baseColor = color * textureColor;

    if (enableLighting != 0) {
        float cos = saturate(dot(normalize(input.normal), -normalize(direction)));
        output.color = baseColor * lightColor * cos * intensity;
    } else {
        output.color = baseColor;
    }
    return output;
}