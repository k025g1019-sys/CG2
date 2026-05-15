#include "Object3d.hlsli"
cbuffer Material : register(b0)
{
    float4 color;
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
    
    output.color = color * textureColor;
    return output;
}