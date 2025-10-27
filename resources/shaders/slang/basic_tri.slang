
struct Vertex {
    float3 position;
};

const StructuredBuffer<Vertex> buf;

struct VOut
{
    float4 position : SV_Position;
    float3 color;
};

[shader("vertex")]
VOut vertex_main(uint vert_id : SV_VertexID) {
    const Vertex v = buf[vert_id];    
    VOut o;
    o.color = float3(vert_id & 1, vert_id & 2, vert_id & 4);
    o.position = float4(v.position, 1.0);
    return o;
}

[shader("fragment")]
float4 frag_main(VOut input) : SV_Target {
    return float4(input.color, 1.0); 
}
