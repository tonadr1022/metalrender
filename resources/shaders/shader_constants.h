#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#define RENDER_MODE_DEFAULT 0
#define RENDER_MODE_NORMALS 1
#define RENDER_MODE_NORMAL_MAP 2

#ifndef __METAL__
#define CONSTANT
#define INLINE inline
#else
#define CONSTANT constant
#define INLINE
#endif  // __cplusplus

#define SHADER_CONSTANT INLINE constexpr CONSTANT uint32_t
SHADER_CONSTANT k_max_materials = 1024;
SHADER_CONSTANT k_max_textures = 1024;
SHADER_CONSTANT k_max_vertices_per_meshlet = 128;
SHADER_CONSTANT k_max_triangles_per_meshlet = 256;
SHADER_CONSTANT k_max_total_threads_per_mesh_grid = 32;

#endif
