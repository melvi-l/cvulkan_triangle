struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;
Sampler2D texture;

struct VIn {
    float3 position;
    float3 color;
    float2 uv;
}

struct VOut {
    float4 position : SV_Position;
    float3 color;
    float2 frag_tex_coord;
};

[shader("vertex")]
VOut vertMain(VIn input) {
    VOut output;
    output.position = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.position, 1.0))));
    output.color = input.color;
    output.frag_tex_coord = input.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VOut input) : SV_Target
{
    return texture.Sample(input.frag_tex_coord);
}
