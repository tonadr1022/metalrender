#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#ifndef __METAL__
#define CONSTANT
#define INLINE inline
#else
#define CONSTANT constant
#define INLINE
#endif  // __cplusplus

INLINE constexpr CONSTANT int k_max_materials = 1024;
INLINE constexpr CONSTANT int k_max_textures = 1024;

#endif
