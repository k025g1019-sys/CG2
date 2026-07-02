// カメラ映像ブリットPS：Webカメラのテクスチャを、指定矩形へレターボックス（contain）で描く。
// CPU側で算出した fit/offset でカメラ像を矩形内へ収め、余白は黒で塗る。mirrorで左右反転する。
Texture2D<float4> gCamera : register(t0);
SamplerState gSampler : register(s0);

cbuffer BlitParams : register(b0)
{
    float2 gFit;     // カメラ像の収めスケール（quad UV単位）
    float2 gOffset;  // 収め先の左下オフセット（quad UV単位）
    int gMirror;     // 1で左右反転
    float3 gPad;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
    // 矩形全体[0,1]のUVを、カメラ像の収め領域[offset, offset+fit]に対する座標へ変換する。
    float2 cuv = (input.texcoord - gOffset) / gFit;

    // 収め領域の外（レターボックスの黒帯）は黒で塗る。
    if (cuv.x < 0.0f || cuv.x > 1.0f || cuv.y < 0.0f || cuv.y > 1.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // 内蔵カメラは自分視点なので鏡像のほうが自然。
    if (gMirror != 0)
    {
        cuv.x = 1.0f - cuv.x;
    }

    float3 color = gCamera.Sample(gSampler, cuv).rgb;
    return float4(color, 1.0f);
}
