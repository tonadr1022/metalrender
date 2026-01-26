#ifndef __HLSL__
#define __HLSL__ 1
#endif

#define ROOT_SIGNATURE                                                                            \
  "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), "                \
  "RootConstants(num32BitConstants = 20, b998, space = 0, visibility = SHADER_VISIBILITY_ALL),"   \
  "RootConstants(num32BitConstants = 2, b999, space = 0, visibility = SHADER_VISIBILITY_ALL),"    \
  "DescriptorTable( "                                                                             \
  "CBV(b3, numDescriptors = 12, space = 0, flags = DATA_STATIC_WHILE_SET_AT_EXECUTE),"            \
  "SRV(t0, numDescriptors = 12,space = 0,  flags = DESCRIPTORS_VOLATILE | "                       \
  "DATA_STATIC_WHILE_SET_AT_EXECUTE),"                                                            \
  "UAV(u0, numDescriptors = 12, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE)" \
  ")"

#define CONSTANT_BUFFER(type, name, reg) ConstantBuffer<type> name : register(b##reg)
#define DRAW_COUNT_CONSTANT_BUFFER(type, name) CONSTANT_BUFFER(type, name, 999)

template <typename T>
struct BindlessResource {
  T operator[](uint index) { return T(ResourceDescriptorHeap[index]); }
};

template <>
struct BindlessResource<SamplerState> {
  SamplerState operator[](uint index) { return SamplerState(SamplerDescriptorHeap[index]); }
};

static const BindlessResource<SamplerState> bindless_samplers;
static const BindlessResource<Texture2D> bindless_textures;
static const BindlessResource<ByteAddressBuffer> bindless_buffers;
static const BindlessResource<StructuredBuffer<uint> > bindless_buffers_uint;
static const BindlessResource<StructuredBuffer<uint2> > bindless_buffers_uint2;
static const BindlessResource<StructuredBuffer<uint3> > bindless_buffers_uint3;
static const BindlessResource<StructuredBuffer<uint4> > bindless_buffers_uint4;
static const BindlessResource<StructuredBuffer<float> > bindless_buffers_float;
static const BindlessResource<StructuredBuffer<float2> > bindless_buffers_float2;
static const BindlessResource<StructuredBuffer<float3> > bindless_buffers_float3;
static const BindlessResource<StructuredBuffer<float4> > bindless_buffers_float4;
static const BindlessResource<StructuredBuffer<half> > bindless_buffers_half;
static const BindlessResource<StructuredBuffer<half2> > bindless_buffers_half2;
static const BindlessResource<StructuredBuffer<half3> > bindless_buffers_half3;
static const BindlessResource<StructuredBuffer<half4> > bindless_buffers_half4;
static const BindlessResource<Texture2DArray> bindless_textures2DArray;
static const BindlessResource<Texture2DArray<half4> > bindless_textures2DArray_half4;
static const BindlessResource<TextureCube> bindless_cubemaps;
static const BindlessResource<TextureCube<half4> > bindless_cubemaps_half4;
static const BindlessResource<TextureCubeArray> bindless_cubearrays;
static const BindlessResource<Texture3D> bindless_textures3D;
static const BindlessResource<Texture3D<half4> > bindless_textures3D_half4;
static const BindlessResource<Texture2D<float> > bindless_textures_float;
static const BindlessResource<Texture2D<float2> > bindless_textures_float2;
static const BindlessResource<Texture2D<uint> > bindless_textures_uint;
static const BindlessResource<Texture2D<uint4> > bindless_textures_uint4;
static const BindlessResource<Texture2D<half4> > bindless_textures_half4;
static const BindlessResource<Texture1D<float4> > bindless_textures1D;
static const BindlessResource<Texture1D<half4> > bindless_textures1D_half4;

static const BindlessResource<RWTexture2D<float4> > bindless_rwtextures;
static const BindlessResource<RWTexture2D<float> > bindless_rwtextures_float;
static const BindlessResource<RWByteAddressBuffer> bindless_rwbuffers;
static const BindlessResource<RWStructuredBuffer<uint> > bindless_rwbuffers_uint;
static const BindlessResource<RWStructuredBuffer<uint2> > bindless_rwbuffers_uint2;
static const BindlessResource<RWStructuredBuffer<uint3> > bindless_rwbuffers_uint3;
static const BindlessResource<RWStructuredBuffer<uint4> > bindless_rwbuffers_uint4;
static const BindlessResource<RWStructuredBuffer<float> > bindless_rwbuffers_float;
static const BindlessResource<RWStructuredBuffer<float2> > bindless_rwbuffers_float2;
static const BindlessResource<RWStructuredBuffer<float3> > bindless_rwbuffers_float3;
static const BindlessResource<RWStructuredBuffer<float4> > bindless_rwbuffers_float4;
static const BindlessResource<RWTexture2DArray<float4> > bindless_rwtextures2DArray;
static const BindlessResource<RWTexture3D<float4> > bindless_rwtextures3D;
static const BindlessResource<RWTexture2D<uint> > bindless_rwtextures_uint;
static const BindlessResource<RWTexture2D<uint2> > bindless_rwtextures_uint2;
static const BindlessResource<RWTexture2D<uint3> > bindless_rwtextures_uint3;
static const BindlessResource<RWTexture2D<uint4> > bindless_rwtextures_uint4;

#define NEAREST_SAMPLER_IDX 0
#define LINEAR_SAMPLER_IDX 1
#define NEAREST_CLAMP_EDGE_SAMPLER_IDX 2
