// Screen-space UI/text shader: textured quads in pixel coordinates,
// converted to NDC by the screen-size constant. Font atlas is R8 coverage
// sampled as alpha; vertex color tints.

cbuffer UiConstants : register(b0) {
    float2 screenSize;
    float2 pad0;
};

Texture2D uiTexture : register(t0);
SamplerState uiSampler : register(s0);

struct VSIn {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 col   : COLOR0;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 col   : COLOR0;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos.x / screenSize.x * 2.0f - 1.0f,
                  1.0f - i.pos.y / screenSize.y * 2.0f,
                  0.0f, 1.0f);
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET {
    // Modulate: vertex color x texture. Font atlas uploads as RGBA
    // (white RGB, coverage alpha); images upload as RGBA with their own
    // alpha — one shader serves tinted rects, glyphs and pictures.
    float4 t = uiTexture.Sample(uiSampler, i.uv);
    return float4(i.col.rgb * t.rgb, i.col.a * t.a);
}
