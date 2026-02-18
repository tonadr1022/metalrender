#pragma once

#include <unordered_set>

#include "core/Config.hpp"
#include "core/Hash.hpp"
#include "gfx/rhi/CmdEncoder.hpp"

namespace TENG_NAMESPACE {

namespace rhi {
class Swapchain;
}

namespace gfx {

enum class SizeClass : uint8_t { Swapchain, Custom };

inline bool is_depth_format(rhi::TextureFormat format) {
  return format == rhi::TextureFormat::D32float;
}

struct AttachmentInfo {
  rhi::TextureFormat format{rhi::TextureFormat::Undefined};
  glm::uvec2 dims{};
  uint32_t mip_levels{1};
  uint32_t array_layers{1};
  SizeClass size_class{SizeClass::Swapchain};
  bool is_swapchain_tex{};

  bool operator==(const AttachmentInfo& other) const {
    return is_swapchain_tex == other.is_swapchain_tex && format == other.format &&
           dims == other.dims && mip_levels == other.mip_levels &&
           array_layers == other.array_layers && size_class == other.size_class;
  }
};

struct BufferInfo {
  size_t size;
  bool defer_reuse;
  bool operator==(const BufferInfo& other) const {
    return size == other.size && defer_reuse == other.defer_reuse;
  }
};

// Custom hash functors defined in your own namespace
struct AttachmentInfoHash {
  size_t operator()(const AttachmentInfo& att_info) const {
    auto h = std::make_tuple((uint32_t)att_info.size_class, att_info.array_layers,
                             att_info.mip_levels, att_info.dims.x, att_info.dims.y,
                             (uint32_t)att_info.format, att_info.is_swapchain_tex);
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

struct BufferInfoHash {
  size_t operator()(const BufferInfo& buff_info) const {
    auto h = std::make_tuple(buff_info.size);
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

}  // namespace gfx

namespace gfx {

using ExecuteFn = std::function<void(rhi::CmdEncoder* enc)>;

class RenderGraph;

enum class RGResourceType { Texture, Buffer, ExternalTexture, ExternalBuffer };

const char* to_string(RGResourceType type);

struct RGResourceHandle {
  uint32_t idx{UINT32_MAX};
  RGResourceType type{};

  [[nodiscard]] uint64_t to64() const {
    return static_cast<uint64_t>(idx) | static_cast<uint64_t>(type) << 32;
  }
};

enum RGAccess : uint16_t {
  None = 1ULL << 0,
  ColorWrite = 1ULL << 1,
  ColorRead = 1ULL << 2,
  ColorRW = ColorRead | ColorWrite,
  DepthStencilRead = 1ULL << 3,
  DepthStencilWrite = 1ULL << 4,
  DepthStencilRW = DepthStencilRead | DepthStencilWrite,
  VertexRead = 1ULL << 5,
  IndexRead = 1ULL << 6,
  IndirectRead = 1ULL << 7,
  ComputeRead = 1ULL << 8,
  ComputeWrite = 1ULL << 9,
  ComputeRW = ComputeRead | ComputeWrite,
  TransferRead = 1ULL << 10,
  TransferWrite = 1ULL << 11,
  FragmentStorageRead = 1ULL << 12,
  ComputeSample = 1ULL << 13,
  FragmentSample = 1ULL << 14,
  ShaderRead = 1ULL << 15,
  AnyRead = ColorRead | DepthStencilRead | VertexRead | IndexRead | IndirectRead | ComputeRead |
            TransferRead | FragmentSample | FragmentStorageRead | ComputeSample | ShaderRead,
  AnyWrite = ColorWrite | DepthStencilWrite | ComputeWrite | TransferWrite,
};

struct ResourceAndUsage {
  std::string name;
  RGResourceHandle handle;
  rhi::AccessFlags access;
  rhi::PipelineStage stage;
  uint32_t pass_rw_read_idx{UINT32_MAX};
};

enum class RGPassType { None, Compute, Graphics, Transfer };

// This render graph has the following features:
// - auto pass ordering based on resource dependencies
// - auto attachment image creation
// - auto barrier placement
// - external resource integration
//
// The following are TODOs
// - multiple queues
// - auto buffers
// - attachment image/buffer history (ie read from previous frame)
//
// Misc notes:
// - not thread safe
// - must acquire swapchain image before baking, since barriers depend on having the correct handle
//
class RenderGraph {
 public:
  void init(rhi::Device* device);
  void bake(glm::uvec2 fb_size, bool verbose = false);
  void execute();
  void execute(rhi::CmdEncoder* enc);
  void shutdown();

  class Pass {
   public:
    Pass() = default;
    Pass(std::string name, RenderGraph* rg, uint32_t pass_i, RGPassType type);

    RGResourceHandle sample_tex(const std::string& name);
    void sample_external_tex(std::string name);
    void sample_external_tex(std::string name, rhi::PipelineStage stage);
    void r_external_tex(std::string name);
    void r_external_tex(std::string name, rhi::PipelineStage stage);
    void w_external_tex(const std::string& name, rhi::TextureHandle tex_handle);
    void w_external_tex_color_output(const std::string& name, rhi::TextureHandle tex_handle);
    void w_swapchain_tex(rhi::Swapchain* swapchain);
    void w_external_buf(const std::string& name, rhi::BufferHandle buf);
    void w_external_buf(const std::string& name, rhi::BufferHandle buf, rhi::PipelineStage stage);
    RGResourceHandle r_tex(const std::string& name);
    RGResourceHandle w_tex(const std::string& name);
    RGResourceHandle w_color_output(const std::string& name, const AttachmentInfo& att_info);
    RGResourceHandle w_depth_output(const std::string& name, const AttachmentInfo& att_info);
    RGResourceHandle rw_color_output(const std::string& name, const std::string& input_name);
    RGResourceHandle rw_depth_output(const std::string& name, const std::string& input_name);
    void r_external_buf(std::string name, rhi::PipelineStage stage);
    void r_external_buf(std::string name);
    void rw_external_buf(std::string name, const std::string& input_name);
    void rw_external_buf(std::string name, const std::string& input_name, rhi::PipelineStage stage);

    RGResourceHandle r_buf(const std::string& name, rhi::PipelineStage stage);
    /// @param defer_reuse defer reuse by one frame
    RGResourceHandle w_buf(const std::string& name, rhi::PipelineStage stage, size_t size,
                           bool defer_reuse = false);
    RGResourceHandle rw_buf(const std::string& name, rhi::PipelineStage stage,
                            const std::string& input_name);

    struct NameAndAccess {
      std::string name;
      rhi::PipelineStage stage;
      rhi::AccessFlags acc;
      RGResourceType type;
      uint32_t rw_read_name_i;
      bool is_swapchain_write;
    };

    [[nodiscard]] const std::vector<NameAndAccess>& get_external_reads() const {
      return external_reads_;
    }
    [[nodiscard]] const std::vector<NameAndAccess>& get_external_writes() const {
      return external_writes_;
    }
    [[nodiscard]] const std::vector<NameAndAccess>& get_internal_reads() const {
      return internal_reads_;
    }
    [[nodiscard]] const std::vector<NameAndAccess>& get_internal_writes() const {
      return internal_writes_;
    }

    rhi::Swapchain* swapchain_write_{nullptr};
    std::vector<NameAndAccess> external_reads_;
    std::vector<NameAndAccess> external_writes_;

    std::vector<NameAndAccess> internal_reads_;
    std::vector<NameAndAccess> internal_writes_;

    // [[nodiscard]] const std::vector<ResourceAndUsage>& get_resource_usages() const {
    // return resource_usages_;
    // }

    const std::string& get_resource_read_names(const NameAndAccess& resource) {
      ASSERT(resource.rw_read_name_i != UINT32_MAX);
      return rw_resource_read_names_[resource.rw_read_name_i];
    }

    [[nodiscard]] uint32_t get_idx() const { return pass_i_; }
    void set_ex(auto&& f) { execute_fn_ = f; }
    [[nodiscard]] const std::string& get_name() const { return name_; }
    [[nodiscard]] const ExecuteFn& get_execute_fn() const { return execute_fn_; }
    [[nodiscard]] RGPassType type() const { return type_; }

   private:
    RGResourceHandle read_tex(const std::string& name, RGAccess access);
    RGResourceHandle rw_tex(const std::string& name, const std::string& input_name,
                            rhi::PipelineStage stage, rhi::AccessFlags access);
    uint32_t add_read_write_resource(const std::string& name);
    std::vector<ResourceAndUsage> resource_usages_;
    std::vector<std::string> rw_resource_read_names_;
    ExecuteFn execute_fn_;
    RenderGraph* rg_;
    uint32_t pass_i_{};
    const std::string name_;
    RGPassType type_{};
  };

  Pass& add_compute_pass(const std::string& name) { return add_pass(name, RGPassType::Compute); }
  Pass& add_transfer_pass(const std::string& name) { return add_pass(name, RGPassType::Transfer); }
  Pass& add_graphics_pass(const std::string& name) { return add_pass(name, RGPassType::Graphics); }

  rhi::TextureHandle get_att_img(RGResourceHandle tex_handle);
  rhi::BufferHandle get_buf(RGResourceHandle buf_handle);

 private:
  Pass& add_pass(const std::string& name, RGPassType type);

  // begin usually called by Pass
  RGResourceHandle get_resource(const std::string& name, RGResourceType type);
  RGResourceHandle add_tex_usage(const std::string& name, const AttachmentInfo& att_info,
                                 RGAccess access, Pass& pass);
  RGResourceHandle add_buf_usage(const std::string& name, const BufferInfo& buf_info, Pass& pass);
  void add_external_write_usage(const std::string& name, rhi::TextureHandle handle, Pass& pass);
  void add_external_write_usage(const std::string& name, rhi::BufferHandle handle, Pass& pass);

  // void add_internal_rw_tex_usage(const std::string& name, const std::string& input_name,
  //                                RGResourceType type, Pass& pass);
  void add_internal_rw_usage(const std::string& name, RGResourceHandle handle, Pass& pass);

  void add_external_rw_buffer_usage(const std::string& name, const std::string& input_name,
                                    Pass& pass);
  void add_external_read_name(const std::string& name) { external_read_names_.insert(name); }
  // end usually called by Pass

  AttachmentInfo* get_tex_att_info(RGResourceHandle handle);
  rhi::BufferHandle get_external_buf(RGResourceHandle handle) {
    ASSERT(handle.type == RGResourceType::ExternalBuffer);
    return external_buffers_[handle.idx];
  }
  rhi::TextureHandle get_external_tex(RGResourceHandle handle) {
    ASSERT(handle.type == RGResourceType::ExternalTexture);
    return external_textures_[handle.idx];
  }

  void find_deps_recursive(uint32_t pass_i, uint32_t stack_size);
  static void dfs(const std::vector<std::unordered_set<uint32_t>>& pass_dependencies,
                  std::unordered_set<uint32_t>& curr_stack_passes,
                  std::unordered_set<uint32_t>& visited_passes, std::vector<uint32_t>& pass_stack,
                  uint32_t pass);

  // glm::uvec2 prev_frame_fb_size_{};
  std::vector<AttachmentInfo> tex_att_infos_;
  std::vector<BufferInfo> buffer_infos_;
  std::vector<rhi::TextureHandle> tex_att_handles_;
  std::vector<rhi::BufferHandle> buffer_handles_;
  std::vector<rhi::BufferHandle> history_buffer_handles_;

  struct BarrierInfo {
    RGResourceHandle resource;
    rhi::PipelineStage src_stage;
    rhi::PipelineStage dst_stage;
    rhi::AccessFlags src_access;
    rhi::AccessFlags dst_access;
    std::string debug_name;
    bool is_swapchain_write;
  };
  std::vector<Pass> passes_;
  std::vector<std::vector<BarrierInfo>> pass_barrier_infos_;
  std::vector<std::unordered_set<uint32_t>> pass_dependencies_;
  std::unordered_map<std::string, RGResourceHandle> resource_name_to_handle_;
  std::unordered_map<uint64_t, RGResourceHandle> tex_handle_to_handle_;
  std::unordered_map<std::string, uint32_t> resource_read_name_to_writer_pass_;
  std::unordered_set<std::string> external_read_names_;
  std::vector<rhi::TextureHandle> external_textures_;
  std::vector<rhi::BufferHandle> external_buffers_;
  std::vector<rhi::TextureHandle> curr_submitted_swapchain_textures_;

  std::unordered_map<AttachmentInfo, std::vector<rhi::TextureHandle>, AttachmentInfoHash>
      free_atts_;
  std::unordered_map<BufferInfo, std::vector<rhi::BufferHandle>, BufferInfoHash> free_bufs_;
  std::unordered_map<BufferInfo, std::vector<rhi::BufferHandle>, BufferInfoHash> history_free_bufs_;

  struct BufferInfoAndHandle {
    BufferInfo buf_info;
    rhi::BufferHandle buf_handle;
  };

  std::vector<uint32_t> sink_passes_;
  std::vector<uint32_t> pass_stack_;

  std::unordered_map<std::string, uint32_t> external_name_to_handle_idx_;
  std::unordered_map<std::string, uint32_t> resource_use_name_to_writer_pass_idx_;

  // cache for intermediate calc data
  std::unordered_set<uint32_t> intermed_pass_visited_;
  std::vector<uint32_t> intermed_pass_stack_;
  rhi::Device* device_{};
  size_t external_texture_count_{};
};

using RGPass = RenderGraph::Pass;

}  // namespace gfx

}  // namespace TENG_NAMESPACE
