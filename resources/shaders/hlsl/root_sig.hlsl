#ifndef __HLSL__
#define __HLSL__ 1
#endif

#define ROOT_SIGNATURE                                                                            \
  "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), "                \
  "RootConstants(num32BitConstants = 20, b998, space = 0, visibility = SHADER_VISIBILITY_ALL),"   \
  "RootConstants(num32BitConstants = 2, b999, space = 0, visibility = SHADER_VISIBILITY_ALL),"    \
  "CBV(b0, space = 0), "                                                                          \
  "CBV(b1, space = 0), "                                                                          \
  "CBV(b2, space = 0), "                                                                          \
  "DescriptorTable( "                                                                             \
  "CBV(b3, numDescriptors = 9, space = 0, flags = DATA_STATIC_WHILE_SET_AT_EXECUTE),"             \
  "SRV(t0, numDescriptors = 12,space = 0,  flags = DESCRIPTORS_VOLATILE | "                       \
  "DATA_STATIC_WHILE_SET_AT_EXECUTE),"                                                            \
  "UAV(u0, numDescriptors = 12, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE)" \
  "),"                                                                                            \
  "StaticSampler(s100, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, "      \
  "addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_LINEAR),"                        \
  "StaticSampler(s101, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, "        \
  "addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_MIN_MAG_MIP_LINEAR),"                         \
  "StaticSampler(s102, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, "    \
  "addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_MIN_MAG_MIP_LINEAR),"                       \
  "StaticSampler(s103, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, "      \
  "addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_POINT),"                         \
  "StaticSampler(s104, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, "        \
  "addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_MIN_MAG_MIP_POINT),"                          \
  "StaticSampler(s105, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, "    \
  "addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_MIN_MAG_MIP_POINT),"                        \
  "StaticSampler(s106, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, "      \
  "addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16),"           \
  "StaticSampler(s107, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, "        \
  "addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16),"            \
  "StaticSampler(s108, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, "    \
  "addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16),"          \
  "StaticSampler(s109, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, "      \
  "addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, "       \
  "comparisonFunc = COMPARISON_GREATER_EQUAL),"



#if defined(VULKAN)

// From WickedEngine
// In Vulkan, we can manually overlap descriptor sets to reduce bindings:
//	Note that HLSL register space declaration was not working correctly with overlapped spaces,
//	But vk::binding works correctly in this case.
//	HLSL register space declaration is working well with Vulkan when spaces are not overlapping.
static const uint DESCRIPTOR_SET_BINDLESS_STORAGE_BUFFER = 1;
static const uint DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER = 2;
static const uint DESCRIPTOR_SET_BINDLESS_SAMPLER = 3;
static const uint DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE = 4;
static const uint DESCRIPTOR_SET_BINDLESS_STORAGE_IMAGE = 5;
static const uint DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER = 6;
static const uint DESCRIPTOR_SET_BINDLESS_ACCELERATION_STRUCTURE = 7;

[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_BUFFER)]] ByteAddressBuffer bindless_buffers[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<uint> bindless_buffers_uint[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<uint2> bindless_buffers_uint2[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<uint3> bindless_buffers_uint3[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<uint4> bindless_buffers_uint4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<float> bindless_buffers_float[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<float2> bindless_buffers_float2[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<float3> bindless_buffers_float3[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<float4> bindless_buffers_float4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<half> bindless_buffers_half[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<half2> bindless_buffers_half2[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<half3> bindless_buffers_half3[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_UNIFORM_TEXEL_BUFFER)]] StructuredBuffer<half4> bindless_buffers_half4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLER)]] SamplerState bindless_samplers[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture2D bindless_textures[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture2DArray bindless_textures2DArray[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture2DArray<half4> bindless_textures2DArray_half4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] TextureCube bindless_cubemaps[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] TextureCube<half4> bindless_cubemaps_half4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] TextureCubeArray bindless_cubearrays[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture3D bindless_textures3D[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture3D<half4> bindless_textures3D_half4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture2D<float> bindless_textures_float[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture2D<float2> bindless_textures_float2[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture2D<uint> bindless_textures_uint[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture2D<uint4> bindless_textures_uint4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture2D<half4> bindless_textures_half4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture1D<half4> bindless_textures1D[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_SAMPLED_IMAGE)]] Texture1D<half4> bindless_textures1D_half4[];

[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_BUFFER)]] RWByteAddressBuffer bindless_rwbuffers[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER)]] RWStructuredBuffer<uint> bindless_rwbuffers_uint[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER)]] RWStructuredBuffer<uint2> bindless_rwbuffers_uint2[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER)]] RWStructuredBuffer<uint3> bindless_rwbuffers_uint3[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER)]] RWStructuredBuffer<uint4> bindless_rwbuffers_uint4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER)]] RWStructuredBuffer<float> bindless_rwbuffers_float[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER)]] RWStructuredBuffer<float2> bindless_rwbuffers_float2[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER)]] RWStructuredBuffer<float3> bindless_rwbuffers_float3[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_TEXEL_BUFFER)]] RWStructuredBuffer<float4> bindless_rwbuffers_float4[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_IMAGE)]] RWTexture2D<float4> bindless_rwtextures[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_IMAGE)]] RWTexture2DArray<float4> bindless_rwtextures2DArray[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_IMAGE)]] RWTexture3D<float4> bindless_rwtextures3D[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_IMAGE)]] RWTexture2D<uint> bindless_rwtextures_uint[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_IMAGE)]] RWTexture2D<uint2> bindless_rwtextures_uint2[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_IMAGE)]] RWTexture2D<uint3> bindless_rwtextures_uint3[];
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_STORAGE_IMAGE)]] RWTexture2D<uint4> bindless_rwtextures_uint4[];
#ifdef RTAPI
[[vk::binding(0, DESCRIPTOR_SET_BINDLESS_ACCELERATION_STRUCTURE)]] RaytracingAccelerationStructure bindless_accelerationstructures[];
#endif // RTAPI

#else

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

#endif

#define NEAREST_SAMPLER_IDX 0
#define LINEAR_SAMPLER_IDX 1
#define NEAREST_CLAMP_EDGE_SAMPLER_IDX 2
