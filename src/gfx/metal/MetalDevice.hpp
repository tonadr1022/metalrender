#pragma once

#include <Metal/Metal.hpp>
#include <filesystem>

#include "MetalBuffer.hpp"
#include "MetalPipeline.hpp"
#include "MetalTexture.hpp"
#include "core/Allocator.hpp"
#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "core/Util.hpp"
#include "gfx/Config.hpp"
#include "gfx/Device.hpp"
#include "gfx/metal/Metal3CmdEncoder.hpp"
#include "gfx/metal/MetalCmdEncoder.hpp"
#include "gfx/metal/MetalSampler.hpp"
#include "gfx/metal/MetalSwapchain.hpp"
#include "shader_constants.h"

class Window;
namespace CA {
class MetalLayer;
}

namespace rhi {

struct TextureDesc;

}  // namespace rhi

namespace NS {
class AutoreleasePool;
}

namespace CA {
class MetalLayer;
class MetalDrawable;
}  // namespace CA

namespace MTL {
class Device;
class Texture;
class RenderCommandEncoder;

}  // namespace MTL

struct MetalDeviceInitInfo {
  bool prefer_mtl4{};
};

class MetalDevice : public rhi::Device {
 public:
  using rhi::Device::get_buf;
  using rhi::Device::get_pipeline;
  using rhi::Device::get_tex;
  void shutdown() override;
  void init(const InitInfo& init_info, const MetalDeviceInitInfo& metal_init_info);
  void init(const InitInfo& init_info) override;
  void set_vsync(bool vsync) override;
  void on_imgui() override;
  bool get_vsync() const override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] MTL::Device* get_device() const { return device_; }

  rhi::BufferHandle create_buf(const rhi::BufferDesc& desc) override;
  rhi::Buffer* get_buf(rhi::BufferHandle handle) override { return buffer_pool_.get(handle); }

  rhi::TextureHandle create_tex(const rhi::TextureDesc& desc) override;
  rhi::Texture* get_tex(rhi::TextureHandle handle) override { return texture_pool_.get(handle); }
  rhi::SamplerHandle create_sampler(const rhi::SamplerDesc& desc) override;

  rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) override {
    return pipeline_pool_.get(handle);
  }
  void fill_buffer(rhi::BufferHandle handle, size_t size, size_t offset,
                   uint32_t fill_value) override;

  void destroy(rhi::BufferHandle handle) override;
  void destroy(rhi::TextureHandle handle) override;
  void destroy(rhi::PipelineHandle handle) override;
  void destroy(rhi::SamplerHandle handle) override;

  rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) override;
  rhi::PipelineHandle create_compute_pipeline(const rhi::ShaderCreateInfo& cinfo) override;

  void submit_frame() override;
  [[nodiscard]] const Info& get_info() const override { return info_; }

  void use_bindless_buffer(MTL::RenderCommandEncoder* enc);
  rhi::CmdEncoder* begin_command_list() override;
  // TODO: is there a better spot for setting window dims, ie on event
  bool begin_frame(glm::uvec2 window_dims) override;

  [[nodiscard]] size_t frame_num() const { return frame_num_; }
  [[nodiscard]] size_t frame_idx() const { return frame_num_ % info_.frames_in_flight; }

  rhi::Swapchain& get_swapchain() override { return swapchain_; }
  const rhi::Swapchain& get_swapchain() const override { return swapchain_; }

  void init_bindless();
  void copy_to_buffer(const void* src, size_t src_size, rhi::BufferHandle buf,
                      size_t dst_offset) override;

  void set_name(rhi::BufferHandle handle, const char* name) override;

  struct MetalPSOs {
    MTL::ComputePipelineState* dispatch_indirect_pso{};
    MTL::ComputePipelineState* dispatch_mesh_pso{};
  };

  const MetalPSOs& get_psos() const { return psos_; }

 private:
  MTL::ComputePipelineState* compile_mtl_compute_pipeline(const std::filesystem::path& path,
                                                          const char* entry_point = "comp_main");

  std::filesystem::path get_metallib_path_from_shader_info(
      const rhi::ShaderCreateInfo& shader_info);

  size_t curr_cmd_list_idx_{};
  BlockPool<rhi::BufferHandle, MetalBuffer> buffer_pool_{128, 1, true};
  BlockPool<rhi::TextureHandle, MetalTexture> texture_pool_{128, 1, true};
  BlockPool<rhi::PipelineHandle, MetalPipeline> pipeline_pool_{20, 1, true};
  BlockPool<rhi::SamplerHandle, MetalSampler> sampler_pool_{16, 1, true};
  Info info_{};
  std::filesystem::path metal_shader_dir_;
  struct MTL4_Resources {
    MTL4::Compiler* shader_compiler{};
    std::array<MTL4::CommandAllocator*, k_max_frames_in_flight> cmd_allocators{};
    MTL4::CommandBuffer* main_cmd_buf{};
    MTL4::CommandQueue* main_cmd_q{};
    std::vector<std::unique_ptr<MetalCmdEncoder>> cmd_lists_;
  };
  std::optional<MTL4_Resources> mtl4_resources_;

  struct MTL3_Resources {
    MTL::CommandBuffer* main_cmd_buf{};
    MTL::CommandQueue* main_cmd_q{};
    std::vector<std::unique_ptr<Metal3CmdEncoder>> cmd_lists_;
  };
  std::optional<MTL3_Resources> mtl3_resources_;

  IndexAllocator resource_desc_heap_allocator_{k_max_textures + k_max_buffers};
  IndexAllocator sampler_desc_heap_allocator_{k_max_samplers};
  MetalSwapchain swapchain_;

  size_t frame_num_{};

  MTL::Device* device_{};
  CA::MetalLayer* metal_layer_{};
  CA::MetalDrawable* curr_drawable_{};
  NS::AutoreleasePool* frame_ar_pool_{};
  MTL::ResidencySet* main_res_set_{};

  MetalPSOs psos_;

 public:  // TODO: fix
  // rhi::BufferHandleHolder top_level_arg_buf_;
  struct GPUFrameAllocator {
    struct Alloc {
      MTL::Buffer* buf;
      size_t offset;
    };

    GPUFrameAllocator(size_t size, MetalDevice* device, size_t frames_in_flight) {
      capacity_ = size;
      device_ = device;
      for (size_t i = 0; i < frames_in_flight; i++) {
        buffers[i] = device->create_buf_h(
            rhi::BufferDesc{.usage = rhi::BufferUsage_Storage, .size = size, .bindless = false});
      }
    }

    Alloc alloc(size_t size) {
      size = align_up(size, 8);
      ALWAYS_ASSERT(size + offset_ <= capacity_);
      auto* buf = device_->get_mtl_buf(buffers[frame_idx_]);
      size_t offset = offset_;
      offset_ += size;
      return {buf, offset};
    }

    void reset(size_t frame_idx) {
      frame_idx_ = frame_idx;
      offset_ = 0;
    }

    std::array<rhi::BufferHandleHolder, k_max_frames_in_flight> buffers;
    size_t capacity_{};
    size_t offset_{};
    size_t frame_idx_{};
    MetalDevice* device_;
  };

 private:
  bool mtl4_enabled_{};
  std::filesystem::path shader_lib_dir_;
  MTL::SharedEvent* shared_event_;

  // TODO: no public members pls
  // public to other implementation classes
 public:
  std::optional<GPUFrameAllocator> arg_buf_allocator_;
  std::optional<GPUFrameAllocator> push_constant_allocator_;
  std::optional<GPUFrameAllocator> test_allocator_;
  rhi::BufferHandleHolder resource_descriptor_table_;
  rhi::BufferHandleHolder sampler_descriptor_table_;

  MTL::Buffer* get_mtl_buf(const rhi::BufferHandleHolder& handle) {
    return reinterpret_cast<MetalBuffer*>(get_buf(handle.handle))->buffer();
  }
  MTL::Buffer* get_mtl_buf(rhi::BufferHandle handle) {
    return reinterpret_cast<MetalBuffer*>(get_buf(handle))->buffer();
  }

  MTL::Texture* get_mtl_tex(rhi::TextureHandle handle) {
    return reinterpret_cast<MetalTexture*>(get_tex(handle))->texture();
  }

  MTL::Library* create_or_get_lib(const std::filesystem::path& path);
  std::unordered_map<std::string, MTL::Library*> path_to_lib_;

  MTL::ResidencySet* make_residency_set();

  class ICB_Mgr {
   public:
    ICB_Mgr(MetalDevice* device, MTL::IndirectCommandType cmd_types)
        : device_(device), cmd_types_(cmd_types) {}

    struct ICB_Data {
      uint64_t curr_id{};
      std::vector<MTL::IndirectCommandBuffer*> icbs;
    };

    struct ICB_Alloc {
      uint32_t id;
      MTL::IndirectCommandBuffer* icb;
    };

    ICB_Alloc alloc(rhi::BufferHandle indirect_buf_handle, uint32_t draw_cnt);
    MTL::IndirectCommandBuffer* get(rhi::BufferHandle indirect_buf, uint32_t id);
    void reset_for_frame();
    void remove(rhi::BufferHandle indirect_buf);

    std::unordered_map<uint64_t, ICB_Data> indirect_buffer_handle_to_icb_;
    MetalDevice* device_{};
    MTL::IndirectCommandType cmd_types_{};
  };

  ICB_Mgr icb_mgr_draw_indexed_{this, MTL::IndirectCommandTypeDrawIndexed};
  ICB_Mgr icb_mgr_draw_mesh_threadgroups_{this, MTL::IndirectCommandTypeDrawMeshThreadgroups};

  MTL::ResidencySet* get_main_residency_set() const { return main_res_set_; }
  struct RequestedAllocationSizes {
    size_t total_buffer_space_allocated;
  };
  RequestedAllocationSizes req_alloc_sizes_{};
};

struct GLFWwindow;

CA::MetalLayer* init_metal_window(GLFWwindow* window, MTL::Device* device,
                                  bool transparent_allowed);
