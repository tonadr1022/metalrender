#ifndef SHADER_CORE_H
#define SHADER_CORE_H

#ifdef __HLSL__

#define HLSL_REG(x) : register(x)
#define HLSL_PC_REG HLSL_REG(b0)

#define packed_float2 float2
#define packed_float3 float3
#define packed_float4 float4
#define packed_char4 int8_t4_packed

#define ATTR_POSITION

#elif defined(__METAL__)

#define uint32_t uint
#define ATTR_POSITION [[position]]

#define PUSH_CONSTANT(x) cbuffer x HLSL_REG(b0)

#elif defined(__cplusplus)

#include <glm/ext/vector_int3_sized.hpp>
#include <glm/ext/vector_int4_sized.hpp>
#include <glm/mat4x4.hpp>
#define packed_float4x4 glm::mat4
#define float4x4 glm::mat4

#define uint uint32_t
#define packed_float2 glm::vec2
#define packed_float3 glm::vec3
#define packed_char3 glm::i8vec3
#define packed_char4 glm::i8vec4
#define packed_char char
#define packed_float4 glm::vec4
#define float2 glm::vec2
#define float3 glm::vec3
#define float4 glm::vec4
#define int3 glm::ivec3
#define int4 glm::ivec4
#define uint3 glm::uvec3

#define cbuffer struct
#define HLSL_REG(x)
#define HLSL_PC_REG

#define PUSH_CONSTANT(x) struct x

#define ATTR_POSITION

#endif  // __cplusplus

#endif  // SHADER_CORE_H
