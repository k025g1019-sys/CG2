#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    float3 padding;
};

cbuffer MaterialBuffer : register(b0)
{
    Material material;
};

cbuffer DirectionalLightBuffer : register(b1)
{
    DirectionalLight gDirectionalLight;
}

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 textureColor =
        gTexture.Sample(gSampler, input.texcoord);

    if (material.enableLighting == 0)
    {
        output.color =
            material.color *
            textureColor;

        return output;
    }
    
    float NdotL =
        dot(
            normalize(input.normal),
            -normalize(gDirectionalLight.direction)
        );

    //float cos =
    //    saturate(NdotL);
    
    float cos =
    saturate(pow(NdotL * 0.5f + 0.5f, 2.0f));
    
    float4 diffuseLight =
        gDirectionalLight.color *
        cos *
        gDirectionalLight.intensity;

    output.color =
        material.color *
        textureColor *
        diffuseLight;

    return output;
}