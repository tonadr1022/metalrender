#pragma once

#include <filesystem>

#include "Buffer.hpp"
#include "RendererTypes.hpp"
#include "Texture.hpp"
#include "gfx/Sampler.hpp"

class Window;

namespace MTL {
class Texture;
}

namespace rhi {

struct GraphicsPipelineCreateInfo;
class Device;
class CmdEncoder;
class Swapchain;

class Device {
 public:
  struct Info {
    size_t frames_in_flight;
  };
  struct InitInfo {
    Window* window;
    std::filesystem::path shader_lib_dir;
    std::string app_name;
    bool validation_layers_enabled{true};
    size_t frames_in_flight{2};
  };
  virtual ~Device() = default;
  virtual void init(const InitInfo& init_info) = 0;
  [[nodiscard]] virtual void* get_native_device() const = 0;
  virtual void shutdown() = 0;

  virtual void set_vsync(bool vsync) = 0;
  [[nodiscard]] virtual bool get_vsync() const = 0;

  // resource CRUD
  virtual BufferHandle create_buf(const rhi::BufferDesc& desc) = 0;
  BufferHandleHolder create_buf_h(const rhi::BufferDesc& desc) {
    return BufferHandleHolder{create_buf(desc), this};
  }
  virtual TextureHandle create_tex(const rhi::TextureDesc& desc) = 0;
  TextureHandleHolder create_tex_h(const rhi::TextureDesc& desc) {
    return TextureHandleHolder{create_tex(desc), this};
  }
  virtual Texture* get_tex(TextureHandle handle) = 0;
  Texture* get_tex(const TextureHandleHolder& handle) { return get_tex(handle.handle); }
  Buffer* get_buf(const BufferHandleHolder& handle) { return get_buf(handle.handle); }
  virtual Buffer* get_buf(BufferHandle handle) = 0;
  rhi::Pipeline* get_pipeline(const rhi::PipelineHandleHolder& handle) {
    return get_pipeline(handle.handle);
  }
  virtual rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) = 0;
  virtual void destroy(BufferHandle handle) = 0;
  virtual void destroy(PipelineHandle handle) = 0;
  virtual rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) = 0;
  rhi::PipelineHandleHolder create_graphics_pipeline_h(
      const rhi::GraphicsPipelineCreateInfo& cinfo) {
    return rhi::PipelineHandleHolder{create_graphics_pipeline(cinfo), this};
  }
  virtual rhi::SamplerHandle create_sampler(const rhi::SamplerDesc& desc) = 0;
  rhi::SamplerHandleHolder create_sampler_h(const rhi::SamplerDesc& desc) {
    return SamplerHandleHolder{create_sampler(desc), this};
  }
  [[nodiscard]] virtual const Info& get_info() const = 0;
  // TODO: is there a better spot for setting window dims, ie on event
  virtual bool begin_frame(glm::uvec2 window_dims) = 0;
  virtual void copy_to_buffer(const void* src, size_t src_size, rhi::BufferHandle buf,
                              size_t dst_offset) = 0;
  void copy_to_buffer(const void* src, size_t src_size, rhi::BufferHandle buf) {
    copy_to_buffer(src, src_size, buf, 0);
  }
  virtual void fill_buffer(rhi::BufferHandle handle, size_t size, size_t offset,
                           uint32_t fill_value) = 0;

  // commands
  virtual CmdEncoder* begin_command_list() = 0;
  virtual void submit_frame() = 0;

  virtual void destroy(TextureHandle handle) = 0;
  virtual void destroy(SamplerHandle handle) = 0;

  virtual rhi::Swapchain& get_swapchain() = 0;
  [[nodiscard]] virtual const rhi::Swapchain& get_swapchain() const = 0;
};

enum class GfxAPI { Vulkan, Metal };

std::unique_ptr<Device> create_device(GfxAPI api);

}  // namespace rhi
