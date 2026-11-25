// Basic lit opaque shader: Lambert diffuse + simple rim/fresnel term to
// approximate the atmospheric-scatter "glow" look on hull edges without a
// full deferred PBR pipeline.

cbuffer FrameConstants : register(b0) {
    float4x4 gViewProj;
    float4 gLightDir;             // xyz = direction light travels, w unused
    float4 gLightColorIntensity;  // rgb = color, a = intensity
};

cbuffer ObjectConstants : register(b1) {
    float4x4 gWorld;
};

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float3 color    : COLOR;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL;
    float3 color : COLOR;
    float3 viewDir : TEXCOORD0;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPos = mul(float4(input.position, 1.0), gWorld);
    output.position = mul(worldPos, gViewProj);
    output.worldNormal = normalize(mul(float4(input.normal, 0.0), gWorld).xyz);
    output.color = input.color;
    output.viewDir = normalize(-worldPos.xyz);
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 n = normalize(input.worldNormal);
    float3 lightDir = normalize(-gLightDir.xyz);
    float ndotl = saturate(dot(n, lightDir));

    float3 ambient = input.color * 0.12;
    float3 diffuse = input.color * ndotl * gLightColorIntensity.rgb * gLightColorIntensity.a;

    float rim = pow(1.0 - saturate(dot(n, normalize(input.viewDir))), 3.0);
    float3 rimColor = float3(0.4, 0.6, 1.0) * rim * 0.5;

    float3 finalColor = ambient + diffuse + rimColor;
    return float4(finalColor, 1.0);
}
