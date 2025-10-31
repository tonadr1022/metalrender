#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

template<typename T>
struct spvDescriptor
{
    T value;
};

template<typename T>
struct spvDescriptorArray
{
    spvDescriptorArray(const device spvDescriptor<T>* ptr) : ptr(&ptr->value)
    {
    }
    const device T& operator [] (size_t i) const
    {
        return ptr[i];
    }
    const device T* ptr;
};

struct type_ColorBuffer
{
    uint vert_idx;
    uint color_idx;
};

struct Vertex
{
    float3 position;
};

struct type_StructuredBuffer_Vertex
{
    Vertex _m0[1];
};

struct Color
{
    float3 color;
};

struct type_StructuredBuffer_Color
{
    Color _m0[1];
};

struct spvDescriptorSetBuffer0
{
    constant type_ColorBuffer* ColorBuffer [[id(0)]];
    spvDescriptor<const device type_StructuredBuffer_Vertex *> ResourceDescriptorHeap [[id(1)]][1] /* unsized array hack */;
    // Overlapping binding: spvDescriptor<const device type_StructuredBuffer_Color *> ResourceDescriptorHeap_1 [[id(1)]][1] /* unsized array hack */;
};

struct vertex_main_out
{
    float3 out_var_COLOR0 [[user(locn0)]];
    float4 gl_Position [[position]];
};

vertex vertex_main_out vertex_main(const device spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], uint gl_VertexIndex [[vertex_id]])
{
    spvDescriptorArray<const device type_StructuredBuffer_Vertex*> ResourceDescriptorHeap {spvDescriptorSet0.ResourceDescriptorHeap};
    spvDescriptorArray<const device type_StructuredBuffer_Color*> ResourceDescriptorHeap_1 {reinterpret_cast<spvDescriptor<const device type_StructuredBuffer_Color *> const device *>(&spvDescriptorSet0.ResourceDescriptorHeap)};

    vertex_main_out out = {};
    out.gl_Position = float4(ResourceDescriptorHeap[(*spvDescriptorSet0.ColorBuffer).vert_idx]->_m0[gl_VertexIndex].position, 1.0);
    out.out_var_COLOR0 = float3(float(gl_VertexIndex & 1u), float(gl_VertexIndex & 2u), float(gl_VertexIndex & 4u)) * ResourceDescriptorHeap_1[(*spvDescriptorSet0.ColorBuffer).color_idx]->_m0[gl_VertexIndex].color;
    return out;
}

