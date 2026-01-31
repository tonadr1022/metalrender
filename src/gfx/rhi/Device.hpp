#pragma once

#include <filesystem>
#include <span>

#include "GFXTypes.hpp"
#include "Sampler.hpp"

class Window;

namespace MTL {
class Texture;
}

namespace rhi {

struct SwapchainDesc;
struct GraphicsPipelineCreateInfo;
struct ComputePipelineCreateInfo;
struct ShaderCreateInfo;
struct QueryPoolDesc;
class Device;
class CmdEncoder;
class Swapchain;

using TextureViewHandle = int32_t;

class Device {
 public:
  struct Info {
    size_t frames_in_flight;
    float timestamp_period;
  };
  struct InitInfo {
    std::filesystem::path shader_lib_dir;
    std::string app_name;
    bool validation_layers_enabled{true};
    size_t frames_in_flight{2};
  };
  virtual ~Device() = default;
  virtual void init(const InitInfo& init_info) = 0;
  [[nodiscard]] virtual void* get_native_device() const = 0;
  virtual void shutdown() = 0;

  // resource CRUD
  virtual BufferHandle create_buf(const rhi::BufferDesc& desc) = 0;
  [[nodiscard]] BufferHandleHolder create_buf_h(const rhi::BufferDesc& desc) {
    return BufferHandleHolder{create_buf(desc), this};
  }
  virtual TextureHandle create_tex(const rhi::TextureDesc& desc) = 0;
  [[nodiscard]] TextureHandleHolder create_tex_h(const rhi::TextureDesc& desc) {
    return TextureHandleHolder{create_tex(desc), this};
  }
  virtual TextureViewHandle create_tex_view(rhi::TextureHandle handle, uint32_t base_mip_level,
                                            uint32_t level_count, uint32_t base_array_layer,
                                            uint32_t layer_count) = 0;
  virtual QueryPoolHandle create_query_pool(const QueryPoolDesc& desc) = 0;
  QueryPoolHandleHolder create_query_pool_h(const QueryPoolDesc& desc) {
    return QueryPoolHandleHolder{create_query_pool(desc), this};
  }
  virtual rhi::SamplerHandle create_sampler(const rhi::SamplerDesc& desc) = 0;
  [[nodiscard]] rhi::SamplerHandleHolder create_sampler_h(const rhi::SamplerDesc& desc) {
    return SamplerHandleHolder{create_sampler(desc), this};
  }
  virtual rhi::SwapchainHandle create_swapchain(const rhi::SwapchainDesc& desc) = 0;
  rhi::SwapchainHandleHolder create_swapchain_h(const rhi::SwapchainDesc& desc) {
    return rhi::SwapchainHandleHolder{create_swapchain(desc), this};
  }
  virtual rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) = 0;
  [[nodiscard]] rhi::PipelineHandleHolder create_graphics_pipeline_h(
      const rhi::GraphicsPipelineCreateInfo& cinfo) {
    return rhi::PipelineHandleHolder{create_graphics_pipeline(cinfo), this};
  }
  [[nodiscard]] virtual rhi::PipelineHandle create_compute_pipeline(
      const rhi::ShaderCreateInfo&) = 0;
  [[nodiscard]] rhi::PipelineHandleHolder create_compute_pipeline_h(
      const rhi::ShaderCreateInfo& cinfo) {
    return rhi::PipelineHandleHolder{create_compute_pipeline(cinfo), this};
  }
  virtual bool replace_pipeline(rhi::PipelineHandle handle,
                                const rhi::GraphicsPipelineCreateInfo& cinfo) = 0;
  virtual bool replace_compute_pipeline(rhi::PipelineHandle handle,
                                        const rhi::ShaderCreateInfo& cinfo) = 0;

  // getters
  virtual uint32_t get_tex_view_bindless_idx(rhi::TextureHandle handle, int subresource_id) = 0;
  virtual Texture* get_tex(TextureHandle handle) = 0;
  Texture* get_tex(const TextureHandleHolder& handle) { return get_tex(handle.handle); }
  Buffer* get_buf(const BufferHandleHolder& handle) { return get_buf(handle.handle); }
  virtual void get_all_buffers(std::vector<rhi::Buffer*>& out_buffers) = 0;
  virtual Buffer* get_buf(BufferHandle handle) = 0;
  rhi::Pipeline* get_pipeline(const rhi::PipelineHandleHolder& handle) {
    return get_pipeline(handle.handle);
  }
  virtual rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) = 0;
  virtual rhi::Swapchain* get_swapchain(rhi::SwapchainHandle handle) = 0;
  rhi::Swapchain* get_swapchain(const rhi::SwapchainHandleHolder& handle) {
    return get_swapchain(handle.handle);
  }

  virtual void destroy(BufferHandle handle) = 0;
  virtual void destroy(rhi::TextureHandle tex_handle, int tex_view_handle) = 0;
  virtual void destroy(PipelineHandle handle) = 0;
  virtual void destroy(rhi::QueryPoolHandle handle) = 0;
  virtual void destroy(TextureHandle handle) = 0;
  virtual void destroy(SamplerHandle handle) = 0;
  virtual void destroy(SwapchainHandle handle) = 0;

  virtual void cmd_encoder_wait_for(rhi::CmdEncoder* cmd_enc, rhi::CmdEncoder* wait_for) = 0;
  virtual CmdEncoder* begin_command_list() = 0;
  virtual void submit_frame() = 0;

  virtual bool recreate_swapchain(const rhi::SwapchainDesc& desc, rhi::Swapchain* swapchain) = 0;
  virtual void begin_swapchain_rendering(rhi::Swapchain* swapchain, rhi::CmdEncoder* cmd_enc) = 0;
  virtual void resolve_query_data(rhi::QueryPoolHandle query_pool, uint32_t start_query,
                                  uint32_t query_count, std::span<uint64_t> out_timestamps) = 0;

  [[nodiscard]] virtual const Info& get_info() const = 0;
  [[nodiscard]] size_t frames_in_flight() const { return get_info().frames_in_flight; }
  virtual void on_imgui() {}
};

enum class GfxAPI { Vulkan, Metal };

std::unique_ptr<Device> create_device(GfxAPI api);

}  // namespace rhi
