#ifndef MATH_HLSL
#define MATH_HLSL

float3 rotate_quat(float3 v, float4 q) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }

bool cone_cull(float3 center, float radius, float3 cone_axis, float cone_cutoff,
               float3 camera_position) {
  return dot(center - camera_position, cone_axis) >=
         cone_cutoff * length(center - camera_position) + radius;
}

// Ref:
// https://github.com/zeux/niagara/blob/7fa51801abc258c3cb05e9a615091224f02e11cf/src/shaders/math.h#L2
// Original Ref: 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara,
// Morgan McGuire. 2013
struct ProjectSphereResult {
  float4 aabb;
  bool success;
};

ProjectSphereResult project_sphere(float3 c, float r, float znear, float P00, float P11) {
  ProjectSphereResult res;
  res.success = false;
  c.z = abs(c.z);
  if (c.z < r + znear) return res;

  float3 cr = c * r;
  float czr2 = c.z * c.z - r * r;

  float vx = sqrt(c.x * c.x + czr2);
  float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
  float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

  float vy = sqrt(c.y * c.y + czr2);
  float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
  float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

  res.aabb = float4(minx * P00, miny * P11, maxx * P00, maxy * P11);
  res.aabb = res.aabb.xwzy * float4(0.5f, -0.5f, 0.5f, -0.5f) +
             float4(.5f, .5f, .5f, .5f);  // clip space -> uv space

  res.success = true;

  return res;
}

// Optimized filmic operator by Jim Hejl and Richard Burgess-Dawson
// http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 tonemap(float3 c) {
  float3 x = max(float3(0, 0, 0), c - 0.004);
  return (x * (6.2 * x + .5)) / (x * (6.2 * x + 1.7) + 0.06);
}

float3 gamma_correct(float3 c) { return pow(c, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2)); }

// https://64.github.io/tonemapping/
float3 ACESFilm(float3 x) {
  float a = 2.51;
  float b = 0.03;
  float c = 2.43;
  float d = 0.59;
  float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

#endif
