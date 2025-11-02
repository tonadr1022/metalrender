#pragma once

#include "Buffer.hpp"
#include "RendererTypes.hpp"
#include "Texture.hpp"
#include "gfx/Sampler.hpp"

namespace MTL {
class Texture;
}

namespace rhi {

struct GraphicsPipelineCreateInfo;
class Device;
class CmdEncoder;

class Device {
 public:
  struct Info {
    size_t frames_in_flight;
  };
  virtual ~Device() = default;
  [[nodiscard]] virtual void* get_native_device() const = 0;
  virtual void shutdown() = 0;

  // resource CRUD
  virtual BufferHandle create_buf(const rhi::BufferDesc& desc) = 0;
  virtual BufferHandleHolder create_buf_h(const rhi::BufferDesc& desc) = 0;
  virtual TextureHandle create_tex(const rhi::TextureDesc& desc) = 0;
  virtual TextureHandleHolder create_tex_h(const rhi::TextureDesc& desc) = 0;
  virtual Texture* get_tex(TextureHandle handle) = 0;
  virtual Texture* get_tex(const TextureHandleHolder& handle) = 0;
  virtual Buffer* get_buf(const BufferHandleHolder& handle) = 0;
  virtual Buffer* get_buf(BufferHandle handle) = 0;
  virtual rhi::Pipeline* get_pipeline(const rhi::PipelineHandleHolder& handle) = 0;
  virtual rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) = 0;
  virtual void destroy(BufferHandle handle) = 0;
  virtual void destroy(PipelineHandle handle) = 0;
  virtual rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) = 0;
  virtual rhi::PipelineHandleHolder create_graphics_pipeline_h(
      const rhi::GraphicsPipelineCreateInfo& cinfo) = 0;
  virtual rhi::SamplerHandle create_sampler(const rhi::SamplerDesc& desc) = 0;
  virtual rhi::SamplerHandleHolder create_sampler_h(const rhi::SamplerDesc& desc) = 0;

  [[nodiscard]] virtual const Info& get_info() const = 0;
  // TODO: is there a better spot for setting window dims, ie on event
  virtual bool begin_frame(glm::uvec2 window_dims) = 0;

  virtual void copy_to_buffer(void* src, size_t src_size, rhi::BufferHandle buf,
                              size_t dst_offset) = 0;

  void copy_to_buffer(void* src, size_t src_size, rhi::BufferHandle buf) {
    copy_to_buffer(src, src_size, buf, 0);
  }

  // commands
  virtual CmdEncoder* begin_command_list() = 0;
  virtual void submit_frame() = 0;

  virtual void destroy(TextureHandle handle) = 0;
  virtual void destroy(SamplerHandle handle) = 0;
};

}  // namespace rhi
