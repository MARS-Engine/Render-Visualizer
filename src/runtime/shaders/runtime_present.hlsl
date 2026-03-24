cbuffer PresentConstants : register(b15) {
    uint source_texture_index;
};

SamplerState linearSampler : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertex_id : SV_VertexID) {
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    VSOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = uvs[vertex_id];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0 {
    Texture2D<float4> source_texture = ResourceDescriptorHeap[source_texture_index];
    return source_texture.Sample(linearSampler, input.uv);
}
