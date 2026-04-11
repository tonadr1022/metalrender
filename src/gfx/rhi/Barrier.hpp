#pragma once

#include <cstdint>

#include "GFXTypes.hpp"

namespace TENG_NAMESPACE {

namespace gfx::rhi {

inline constexpr uint32_t k_gpu_barrier_mip_all = UINT32_MAX;
inline constexpr uint32_t k_gpu_barrier_slice_all = UINT32_MAX;

struct GPUBarrier {
  enum class Type : uint8_t { Buffer, Texture } type;
  struct Buffer {
    BufferHandle buffer;
    ResourceState src_state;
    ResourceState dst_state;
    size_t offset;
    size_t size;
  };
  struct Texture {
    TextureHandle texture;
    ResourceState src_layout;
    ResourceState dst_layout;
    uint32_t mip;
    uint32_t slice;
    uint32_t mip_level_count{1};
    uint32_t array_layer_count{1};
    ImageAspect aspect;
  };
  union {
    Buffer buf;
    Texture tex;
  };

  static GPUBarrier buf_barrier(BufferHandle handle, ResourceState src_state,
                                ResourceState dst_state, size_t offset = 0,
                                size_t size = SIZE_MAX) {
    return {.type = Type::Buffer,
            .buf = {.buffer = handle,
                    .src_state = src_state,
                    .dst_state = dst_state,
                    .offset = offset,
                    .size = size}};
  }

  static GPUBarrier tex_barrier(TextureHandle handle, ResourceState src_state,
                                ResourceState dst_state, uint32_t mip = k_gpu_barrier_mip_all,
                                uint32_t slice = k_gpu_barrier_slice_all,
                                ImageAspect aspect = ImageAspect_Color,
                                uint32_t mip_level_count = 1, uint32_t array_layer_count = 1) {
    const uint32_t resolved_mip_cnt =
        mip == k_gpu_barrier_mip_all ? k_gpu_barrier_mip_all : mip_level_count;
    const uint32_t resolved_layer_cnt =
        slice == k_gpu_barrier_slice_all ? k_gpu_barrier_slice_all : array_layer_count;
    return {.type = Type::Texture,
            .tex = {.texture = handle,
                    .src_layout = src_state,
                    .dst_layout = dst_state,
                    .mip = mip,
                    .slice = slice,
                    .mip_level_count = resolved_mip_cnt,
                    .array_layer_count = resolved_layer_cnt,
                    .aspect = aspect}};
  }
};

}  // namespace gfx::rhi
}  // namespace TENG_NAMESPACE
