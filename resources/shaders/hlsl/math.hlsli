#ifndef MATH_HLSL
#define MATH_HLSL

float3 rotate_quat(float3 v, float4 q) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

#endif

