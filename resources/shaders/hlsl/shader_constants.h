#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#define RENDER_MODE_DEFAULT 0
#define RENDER_MODE_NORMALS 1
#define RENDER_MODE_NORMAL_MAP 2
#define RENDER_MODE_UVS 3

#ifndef __METAL__
#define CONSTANT
#define INLINE inline
#else
#define CONSTANT constant
#define INLINE
#endif  // __cplusplus

#ifndef __HLSL__

#define SHADER_CONSTANT INLINE constexpr CONSTANT uint32_t
SHADER_CONSTANT k_max_materials = 1024;
SHADER_CONSTANT k_max_textures = 1024;
SHADER_CONSTANT k_max_buffers = 1024;
SHADER_CONSTANT k_max_samplers = 16;
SHADER_CONSTANT k_max_vertices_per_meshlet = 128;
SHADER_CONSTANT k_max_triangles_per_meshlet = 128;

#endif

#define K_THREADS_PER_WAVE 32
#define K_TASK_TG_SIZE K_THREADS_PER_WAVE
#define K_MESH_TG_SIZE 32
#define K_MAX_TRIS_PER_MESHLET 128
#define K_MAX_VERTS_PER_MESHLET 128

#define kMeshThreadgroups 32

#endif
