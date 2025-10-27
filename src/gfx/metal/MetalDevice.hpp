#pragma once

#include <Metal/Metal.hpp>
#include <filesystem>

#include "MetalBuffer.hpp"
#include "MetalPipeline.hpp"
#include "MetalTexture.hpp"
#include "core/Allocator.hpp"
#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "gfx/Config.hpp"
#include "gfx/Device.hpp"
#include "gfx/metal/MetalCmdEncoder.hpp"
#include "shader_constants.h"

namespace rhi {

struct TextureDesc;

}  // namespace rhi

namespace NS {
class AutoreleasePool;
}

namespace CA {
class MetalLayer;
}

namespace MTL {
class Device;
class Texture;
class RenderCommandEncoder;

}  // namespace MTL

class MetalDevice;

class MetalDevice : public rhi::Device {
 public:
  void init();
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] MTL::Device* get_device() const { return device_; }

  rhi::BufferHandle create_buf(const rhi::BufferDesc& desc) override;
  rhi::BufferHandleHolder create_buf_h(const rhi::BufferDesc& desc) override {
    return rhi::BufferHandleHolder{create_buf(desc), this};
  }
  rhi::Buffer* get_buf(const rhi::BufferHandleHolder& handle) override {
    return buffer_pool_.get(handle.handle);
  }
  rhi::Buffer* get_buf(rhi::BufferHandle handle) override { return buffer_pool_.get(handle); }

  rhi::TextureHandle create_tex(const rhi::TextureDesc& desc) override;
  rhi::TextureHandleHolder create_tex_h(const rhi::TextureDesc& desc) override {
    return rhi::TextureHandleHolder{create_tex(desc), this};
  }
  rhi::Texture* get_tex(rhi::TextureHandle handle) override { return texture_pool_.get(handle); }

  rhi::Texture* get_tex(const rhi::TextureHandleHolder& handle) override {
    return get_tex(handle.handle);
  }

  rhi::Pipeline* get_pipeline(const rhi::PipelineHandleHolder& handle) override {
    return pipeline_pool_.get(handle.handle);
  }
  rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) override {
    return pipeline_pool_.get(handle);
  }

  void destroy(rhi::BufferHandle handle) override;
  void destroy(rhi::TextureHandle handle) override;
  void destroy(rhi::PipelineHandle handle) override;

  rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) override;
  rhi::PipelineHandleHolder create_graphics_pipeline_h(
      const rhi::GraphicsPipelineCreateInfo& cinfo) override;

  void use_bindless_buffer(MTL::RenderCommandEncoder* enc);
  rhi::CmdEncoder* begin_command_list() override;
  void begin_frame();

  [[nodiscard]] size_t frame_num() const { return frame_num_; }
  [[nodiscard]] size_t frame_idx() const { return frame_num_ % frames_in_flight_; }

 private:
  BlockPool<rhi::BufferHandle, MetalBuffer> buffer_pool_{128, 1, true};
  BlockPool<rhi::TextureHandle, MetalTexture> texture_pool_{128, 1, true};
  BlockPool<rhi::PipelineHandle, MetalPipeline> pipeline_pool_{20, 1, true};
  std::filesystem::path metal_shader_dir_;
  MTL4::Compiler* shader_compiler_{};
  IndexAllocator texture_index_allocator_{k_max_textures};
  std::array<MTL4::CommandAllocator*, k_max_frames_in_flight> cmd_allocators_{};
  std::vector<std::unique_ptr<MetalCmdEncoder>> cmd_lists_;

  size_t frame_num_{};

  NS::AutoreleasePool* ar_pool_{};
  MTL::Device* device_{};
  CA::MetalLayer* metal_layer_{};

  size_t frames_in_flight_{2};

  MTL::Buffer* get_mtl_buf(const rhi::BufferHandleHolder& handle) {
    return reinterpret_cast<MetalBuffer*>(get_buf(handle))->buffer();
  }
  MTL::Library* create_or_get_lib(const std::filesystem::path& path);
  std::unordered_map<std::string, MTL::Library*> path_to_lib_;

  MTL::ResidencySet* make_residency_set();
};
