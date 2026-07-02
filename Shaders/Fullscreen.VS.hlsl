// フルスクリーン三角形。頂点バッファ無しで、SV_VertexID から画面全体を覆う
// 1枚の大きな三角形を生成する（合成パス共通の頂点シェーダー）。
struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(uint id : SV_VertexID)
{
    VSOutput output;
    // id=0:(0,0) / id=1:(2,0) / id=2:(0,2) のUVを作り、画面を覆うクリップ座標へ変換する
    float2 uv = float2((id << 1) & 2, id & 2);
    output.texcoord = uv;
    output.position = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return output;
}
