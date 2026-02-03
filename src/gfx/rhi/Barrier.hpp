#pragma once

#include "GFXTypes.hpp"
namespace TENG_NAMESPACE {

namespace rhi {

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
    ImageAspect aspect;
  };
  union {
    Buffer buf;
    Texture tex;
  };

  static GPUBarrier tex_barrier(rhi::TextureHandle handle, ResourceState src_state,
                                ResourceState dst_state, uint32_t mip = 0, uint32_t slice = 0,
                                ImageAspect aspect = ImageAspect_Color) {
    return {.type = Type::Texture,
            .tex = {.texture = handle,
                    .src_layout = src_state,
                    .dst_layout = dst_state,
                    .mip = mip,
                    .slice = slice,
                    .aspect = aspect}};
  }
};

}  // namespace rhi
}  // namespace TENG_NAMESPACE
