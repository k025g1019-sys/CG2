// 合成パス：複数視点（view0=左端 … view(N-1)=右端）を、方式に応じて1枚へ合成する。
//   gMode 0: アナグリフ（左→赤チャンネル / 右→緑青チャンネル）
//   gMode 1: 左視点のみ（デバッグ。メガネ無しで視点差を確認する用）
//   gMode 2: 右視点のみ（デバッグ）
//   gMode 3: パララックスバリア（列インターリーブ。2視点。物理シート前提）
//   gMode 4: レンチキュラー（斜めサブピクセル織り込み。全視点。物理シート前提）
// アナグリフ／バリア／左右単独は左右端の2視点を使う。
#define MAX_VIEWS 8
Texture2D<float4> gViews[MAX_VIEWS] : register(t0);
SamplerState gSampler : register(s0);

cbuffer CompositeParams : register(b0)
{
    int gMode;            // 合成方式
    float gBarrierPitch;  // バリア1周期(左+右)あたりのスクリーンピクセル数
    float gBarrierPhase;  // バリアパターンの水平オフセット(px)
    int gSwapEyes;        // 0以外で左右(視点順)を入れ替える
    float gLensPitch;     // レンズ1周期あたりのサブピクセル数
    float gLensSlant;     // 1行あたりの水平サブピクセルずれ（0=垂直レンズ）
    float gLensOffset;    // レンズ位相オフセット（サブピクセル）
    int gViewCount;       // 視点数
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// 視点インデックス（範囲外はクランプ／swap反映）でその画素位置の色を取得する
float4 SampleView(int index, float2 uv)
{
    if (gSwapEyes != 0)
    {
        index = gViewCount - 1 - index;
    }
    index = clamp(index, 0, gViewCount - 1);
    return gViews[index].Sample(gSampler, uv);
}

float4 main(VSOutput input) : SV_Target
{
    // --- レンチキュラー：斜めサブピクセル織り込み ---
    if (gMode == 4)
    {
        float px = floor(input.position.x);
        float py = floor(input.position.y);
        float3 outColor = float3(0.0f, 0.0f, 0.0f);
        [unroll]
        for (int c = 0; c < 3; ++c)
        {
            // RGBサブピクセルの水平インデックス（1画素=3サブピクセル, RGB横並び前提）
            float subX = px * 3.0f + (float)c;
            // 斜めレンズの1周期内での位置 [0,1) を視点へ写像する
            float vf = frac((subX - gLensSlant * py + gLensOffset) / gLensPitch);
            int view = (int)(vf * (float)gViewCount);
            float4 sampled = SampleView(view, input.texcoord);
            outColor[c] = sampled[c];
        }
        return float4(outColor, 1.0f);
    }

    // --- 2眼系（アナグリフ／バリア／左右単独）は左右端の視点を使う ---
    float4 left = SampleView(0, input.texcoord);
    float4 right = SampleView(gViewCount - 1, input.texcoord);

    if (gMode == 1)
    {
        return left;
    }
    if (gMode == 2)
    {
        return right;
    }
    if (gMode == 3)
    {
        // パララックスバリア：画素列(SV_Position.x)で1周期を左半分=左眼、右半分=右眼に割り当てる
        float cycle = frac((input.position.x + gBarrierPhase) / gBarrierPitch);
        return (cycle < 0.5f) ? left : right;
    }

    // アナグリフ：左眼を赤、右眼を緑青に割り当てる
    return float4(left.r, right.g, right.b, 1.0f);
}
