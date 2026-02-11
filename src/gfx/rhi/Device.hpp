#pragma once

#include <filesystem>
#include <span>

#include "GFXTypes.hpp"
#include "core/Config.hpp"

namespace MTL {
class Texture;
}

namespace TENG_NAMESPACE {

class Window;

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

enum class GraphicsCapability : uint32_t {
  None = 0,
  CacheCoherentUMA = 1 << 0,  // CPU-GPU shared memory is cache coherent -> no staging buffers, etc.
};

AUGMENT_ENUM_CLASS(GraphicsCapability);

class Device {
 public:
  struct Info {
    size_t frames_in_flight;
    // gpu ticks per second
    size_t timestamp_frequency;
  };
  struct InitInfo {
    std::filesystem::path shader_lib_dir;
    std::string app_name;
    bool validation_layers_enabled{true};
    size_t frames_in_flight{2};
  };
  virtual ~Device() = default;
  virtual void init(const InitInfo &init_info) = 0;
  [[nodiscard]] virtual void *get_native_device() const = 0;
  virtual void shutdown() = 0;
  virtual rhi::ShaderTarget get_supported_shader_targets() = 0;

  // resource CRUD
  virtual BufferHandle create_buf(const BufferDesc &desc) = 0;
  [[nodiscard]] BufferHandleHolder create_buf_h(const BufferDesc &desc) {
    return BufferHandleHolder{create_buf(desc), this};
  }
  virtual TextureHandle create_tex(const TextureDesc &desc) = 0;
  [[nodiscard]] TextureHandleHolder create_tex_h(const TextureDesc &desc) {
    return TextureHandleHolder{create_tex(desc), this};
  }
  virtual TextureViewHandle create_tex_view(TextureHandle handle, uint32_t base_mip_level,
                                            uint32_t level_count, uint32_t base_array_layer,
                                            uint32_t layer_count) = 0;
  virtual QueryPoolHandle create_query_pool(const QueryPoolDesc &desc) = 0;
  QueryPoolHandleHolder create_query_pool_h(const QueryPoolDesc &desc) {
    return QueryPoolHandleHolder{create_query_pool(desc), this};
  }
  virtual SamplerHandle create_sampler(const SamplerDesc &desc) = 0;
  [[nodiscard]] SamplerHandleHolder create_sampler_h(const SamplerDesc &desc) {
    return SamplerHandleHolder{create_sampler(desc), this};
  }
  virtual SwapchainHandle create_swapchain(const SwapchainDesc &desc) = 0;
  SwapchainHandleHolder create_swapchain_h(const SwapchainDesc &desc) {
    return SwapchainHandleHolder{create_swapchain(desc), this};
  }
  virtual PipelineHandle create_graphics_pipeline(const GraphicsPipelineCreateInfo &cinfo) = 0;
  [[nodiscard]] PipelineHandleHolder create_graphics_pipeline_h(
      const GraphicsPipelineCreateInfo &cinfo) {
    return PipelineHandleHolder{create_graphics_pipeline(cinfo), this};
  }
  [[nodiscard]] virtual PipelineHandle create_compute_pipeline(const ShaderCreateInfo &) = 0;
  [[nodiscard]] PipelineHandleHolder create_compute_pipeline_h(const ShaderCreateInfo &cinfo) {
    return PipelineHandleHolder{create_compute_pipeline(cinfo), this};
  }
  virtual bool replace_pipeline(PipelineHandle handle, const GraphicsPipelineCreateInfo &cinfo) = 0;
  virtual bool replace_compute_pipeline(PipelineHandle handle, const ShaderCreateInfo &cinfo) = 0;

  // getters
  virtual uint32_t get_tex_view_bindless_idx(TextureHandle handle, int subresource_id) = 0;
  virtual Texture *get_tex(TextureHandle handle) = 0;
  Texture *get_tex(const TextureHandleHolder &handle) { return get_tex(handle.handle); }
  Buffer *get_buf(const BufferHandleHolder &handle) { return get_buf(handle.handle); }
  virtual void get_all_buffers(std::vector<Buffer *> &out_buffers) = 0;
  virtual Buffer *get_buf(BufferHandle handle) = 0;
  Pipeline *get_pipeline(const PipelineHandleHolder &handle) { return get_pipeline(handle.handle); }
  virtual Pipeline *get_pipeline(PipelineHandle handle) = 0;
  virtual Swapchain *get_swapchain(SwapchainHandle handle) = 0;
  Swapchain *get_swapchain(const SwapchainHandleHolder &handle) {
    return get_swapchain(handle.handle);
  }

  // destroyers
  virtual void destroy(BufferHandle handle) = 0;
  virtual void destroy(TextureHandle tex_handle, int tex_view_handle) = 0;
  virtual void destroy(PipelineHandle handle) = 0;
  virtual void destroy(QueryPoolHandle handle) = 0;
  virtual void destroy(TextureHandle handle) = 0;
  virtual void destroy(SamplerHandle handle) = 0;
  virtual void destroy(SwapchainHandle handle) = 0;

  virtual void cmd_encoder_wait_for(CmdEncoder *cmd_enc, CmdEncoder *wait_for) = 0;
  virtual CmdEncoder *begin_cmd_encoder() = 0;
  virtual void submit_frame() = 0;

  // does actual swapchain recreation, ideally only call this when something
  // needs to change.
  virtual bool recreate_swapchain(const SwapchainDesc &desc, Swapchain *swapchain) = 0;

  // calls CmdEncoder::begin_rendering on the given cmd encoder with the
  // swapchain's current draw image
  // TODO: move this to the encoder?
  virtual void begin_swapchain_rendering(Swapchain *swapchain, CmdEncoder *cmd_enc,
                                         glm::vec4 *clear_color) = 0;
  void begin_swapchain_rendering(Swapchain *swapchain, CmdEncoder *cmd_enc) {
    begin_swapchain_rendering(swapchain, cmd_enc, nullptr);
  }

  virtual void acquire_next_swapchain_image(rhi::Swapchain *) = 0;
  virtual void resolve_query_data(QueryPoolHandle query_pool, uint32_t start_query,
                                  uint32_t query_count, std::span<uint64_t> out_timestamps) = 0;

  [[nodiscard]] virtual const Info &get_info() const = 0;
  [[nodiscard]] size_t frames_in_flight() const { return get_info().frames_in_flight; }
  virtual void on_imgui() {}

  [[nodiscard]] const GraphicsCapability &get_graphics_capabilities() const {
    return capabilities_;
  }

 protected:
  GraphicsCapability capabilities_{};
};

enum class GfxAPI { Vulkan, Metal };

std::unique_ptr<Device> create_device(GfxAPI api);

}  // namespace rhi

}  // namespace TENG_NAMESPACE
