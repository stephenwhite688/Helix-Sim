// Screen-space colored line rendering for the cockpit HUD: velocity vector,
// attitude ladder, reticle, target box, radar sweep, etc. Input positions
// are already in normalized [-1,1] NDC space (computed on the CPU HUD system).

struct VSInput {
    float2 position : POSITION;
    float4 color : COLOR;
};

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    return input.color;
}
