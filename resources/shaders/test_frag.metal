#pragma clang diagnostic ignored "-Wmissing-prototypes"

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
    uint material_buf_idx;
    uint material_idx;
};

struct Material
{
    float3 color;
};

struct type_StructuredBuffer_Material
{
    Material _m0[1];
};

struct spvDescriptorSetBuffer0
{
    constant type_ColorBuffer* ColorBuffer [[id(0)]];
    spvDescriptor<const device type_StructuredBuffer_Material *> ResourceDescriptorHeap [[id(1)]][1] /* unsized array hack */;
};

struct frag_main_out
{
    float4 out_var_SV_Target [[color(0)]];
};

struct frag_main_in
{
    float3 in_var_COLOR0 [[user(locn0)]];
};

fragment frag_main_out frag_main(frag_main_in in [[stage_in]], const device spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])
{
    spvDescriptorArray<const device type_StructuredBuffer_Material*> ResourceDescriptorHeap {spvDescriptorSet0.ResourceDescriptorHeap};

    frag_main_out out = {};
    out.out_var_SV_Target = float4(in.in_var_COLOR0 * ResourceDescriptorHeap[(*spvDescriptorSet0.ColorBuffer).material_buf_idx]->_m0[(*spvDescriptorSet0.ColorBuffer).material_idx].color, 1.0);
    return out;
}

