struct Vertex {
    float3 position;
};

struct Color {
    float3 color;
};

struct VOut
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};


StructuredBuffer<Vertex> vert_buf : register(t0);
StructuredBuffer<Color> color_buf : register(t1);

VOut vertex_main(uint vert_id : SV_VertexID) {
    Vertex v = vert_buf[vert_id];
    Color c = color_buf[vert_id];
    VOut o;
    o.color = float3(vert_id & 1, vert_id & 2, vert_id & 4) * c.color;
    o.position = float4(v.position, 1.0);
    return o;
}


float4 frag_main(VOut input) : SV_Target {
    float4 color = float4(input.color, 1.0); 
    return color;
}
