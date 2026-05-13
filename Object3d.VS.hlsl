cbuffer TransformationMatrix : register(b0)
{
    float4x4 WVP;
};
struct VertexShaderOutput {
    float4 position : SV_POSITION;
};
struct VertexShaderInput {
    float4 position : POSITION0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    output.position = mul(input.position, WVP);
    return output;
}
