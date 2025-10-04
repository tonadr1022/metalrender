#ifndef SHADER_CORE_H
#define SHADER_CORE_H

#ifdef __METAL__

#define uint32_t uint
#define ATTR_POSITION [[position]]

#else
#include <glm/ext/vector_int3_sized.hpp>
#include <glm/mat4x4.hpp>
#define packed_float4x4 glm::mat4
#define float4x4 glm::mat4

#define packed_float2 glm::vec2
#define packed_float3 glm::vec3
#define packed_char3 glm::i8vec3
#define packed_char char
#define packed_float4 glm::vec4
#define float2 glm::vec2
#define float3 glm::vec3
#define float4 glm::vec4

#define ATTR_POSITION
#endif

#endif
