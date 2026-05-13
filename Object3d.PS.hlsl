cbuffer Material : register(b0)
{
    float4 color;
};
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main()
{
    PixelShaderOutput output;
    output.color = color;
    return output;
}
