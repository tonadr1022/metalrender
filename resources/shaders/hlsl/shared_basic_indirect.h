#ifndef SHARED_BASIC_INDIRECT_H
#define SHARED_BASIC_INDIRECT_H

#include "shader_core.h"

#if defined(VULKAN)

#define PUSHCONSTANT(type, name) [[vk::push_constant]] type name

#elif defined(__HLSL__)

#define PUSHCONSTANT(type, name) CONSTANT_BUFFER(type, name, 998)

#else

#define PUSHCONSTANT(type, name)

#endif

struct BasicIndirectPC {
  uint view_data_buf_idx;
  uint view_data_buf_offset;
  uint vert_buf_idx;
  uint instance_data_buf_idx;
  uint mat_buf_idx;
};

PUSHCONSTANT(BasicIndirectPC, pc);

#ifdef __HLSL__

struct VOut {
  float4 pos : SV_Position;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD0;
  nointerpolation uint material_id : MATERIAL_ID;
};

#endif

#endif  // SHARED_BASIC_INDIRECT_H
