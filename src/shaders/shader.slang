static float2 positions[3] = float2[](
    float2(0.0, -0.5),
    float2(0.5, 0.5),
    float2(-0.5, 0.5)
);

static float3 colors[3] = float3[](
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0)
);

struct VIn {
    float2 position;
    float3 color;
}

struct VOut {
    float3 color;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VOut vertMain(VIn input) {
    VOut output;
    output.sv_position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragMain(VOut input) : SV_Target
{
    return float4(input.color, 1.0);
}
