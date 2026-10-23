// Draws the game's frame (a texture) into the current viewport. The vertex shader generates a triangle that covers
// the viewport from the vertex index alone, so no vertex buffer is needed.

Texture2D frame : register(t0);
SamplerState frameSampler : register(s0);

struct VertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOut VSMain(uint id : SV_VertexID)
{
    VertexOut output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

float4 PSMain(VertexOut input) : SV_Target
{
    return float4(frame.Sample(frameSampler, input.uv).rgb, 1);
}
