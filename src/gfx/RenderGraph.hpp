#pragma once

#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

struct RGResourceId {
  uint32_t idx{UINT32_MAX};
  RGResourceType type{};
  uint32_t version{};

  [[nodiscard]] bool is_valid() const { return idx != UINT32_MAX; }
};

inline bool operator==(const RGResourceId& lhs, const RGResourceId& rhs) {
  return lhs.idx == rhs.idx && lhs.type == rhs.type && lhs.version == rhs.version;
}

struct RGResourceIdHash {
  size_t operator()(const RGResourceId& id) const noexcept {
    auto h = std::make_tuple(id.idx, static_cast<uint32_t>(id.type), id.version);
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

struct ResourceAndUsage {
  RGResourceId id;
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
  using NameId = uint32_t;
  static constexpr NameId kInvalidNameId = UINT32_MAX;

  void init(rhi::Device* device);
  void bake(glm::uvec2 fb_size, bool verbose = false);
  void execute();
  void execute(rhi::CmdEncoder* enc);
  void shutdown();

  class Pass {
   public:
    Pass() = default;
    Pass(std::string name, RenderGraph* rg, uint32_t pass_i, RGPassType type);

    RGResourceId sample_tex(RGResourceId id);
    RGResourceId sample_tex(RGResourceId id, rhi::PipelineStage stage);
    RGResourceId read_tex(RGResourceId id, rhi::PipelineStage stage);
    RGResourceId write_tex(RGResourceId id, rhi::PipelineStage stage);
    RGResourceId write_color_output(RGResourceId id);
    RGResourceId write_depth_output(RGResourceId id);
    RGResourceId rw_tex(RGResourceId input, rhi::PipelineStage stage, rhi::AccessFlags access);
    RGResourceId rw_color_output(RGResourceId input);
    RGResourceId rw_depth_output(RGResourceId input);
    void w_swapchain_tex(rhi::Swapchain* swapchain);
    RGResourceId read_buf(RGResourceId id, rhi::PipelineStage stage);
    RGResourceId write_buf(RGResourceId id, rhi::PipelineStage stage);
    RGResourceId rw_buf(RGResourceId input, rhi::PipelineStage stage);

    RGResourceId import_external_texture(rhi::TextureHandle tex_handle,
                                         std::string_view debug_name = {});
    RGResourceId import_external_buffer(rhi::BufferHandle buf_handle,
                                        std::string_view debug_name = {});

    struct NameAndAccess {
      RGResourceId id;
      rhi::PipelineStage stage;
      rhi::AccessFlags acc;
      RGResourceType type;
      bool is_swapchain_write{false};
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

    [[nodiscard]] uint32_t get_idx() const { return pass_i_; }
    void set_ex(auto&& f) { execute_fn_ = f; }
    [[nodiscard]] const std::string& get_name() const { return name_; }
    [[nodiscard]] const ExecuteFn& get_execute_fn() const { return execute_fn_; }
    [[nodiscard]] RGPassType type() const { return type_; }

   private:
    void add_read_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access);
    void add_write_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access,
                         bool is_swapchain_write = false);
    std::vector<ResourceAndUsage> resource_usages_;
    ExecuteFn execute_fn_;
    RenderGraph* rg_;
    uint32_t pass_i_{};
    const std::string name_;
    RGPassType type_{};
  };

  Pass& add_compute_pass(const std::string& name) { return add_pass(name, RGPassType::Compute); }
  Pass& add_transfer_pass(const std::string& name) { return add_pass(name, RGPassType::Transfer); }
  Pass& add_graphics_pass(const std::string& name) { return add_pass(name, RGPassType::Graphics); }

  RGResourceId create_texture(const AttachmentInfo& att_info, std::string_view debug_name = {});
  RGResourceId create_buffer(const BufferInfo& buf_info, std::string_view debug_name = {});
  RGResourceId import_external_texture(rhi::TextureHandle tex_handle,
                                       std::string_view debug_name = {});
  RGResourceId import_external_buffer(rhi::BufferHandle buf_handle,
                                      std::string_view debug_name = {});

  rhi::TextureHandle get_att_img(RGResourceId tex_id);
  rhi::TextureHandle get_att_img(RGResourceHandle tex_handle);
  rhi::BufferHandle get_buf(RGResourceId buf_id);
  rhi::BufferHandle get_buf(RGResourceHandle buf_handle);

 private:
  Pass& add_pass(const std::string& name, RGPassType type);

  // begin usually called by Pass
  void register_write(RGResourceId id, Pass& pass);
  RGResourceId next_version(RGResourceId id);
  void add_external_read_id(RGResourceId id) { external_read_ids_.insert(id); }
  // end usually called by Pass

  NameId intern_name(const std::string& name);
  const std::string& debug_name(NameId name) const;
  std::string debug_name(RGResourceId id) const;

  AttachmentInfo* get_tex_att_info(RGResourceId id);
  AttachmentInfo* get_tex_att_info(RGResourceHandle handle);
  RGResourceHandle get_physical_handle(RGResourceId id) const;
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
    RGResourceId debug_id{};
    bool is_swapchain_write{false};
  };
  std::vector<Pass> passes_;
  std::vector<std::vector<BarrierInfo>> pass_barrier_infos_;
  std::vector<std::unordered_set<uint32_t>> pass_dependencies_;
  std::unordered_map<RGResourceId, uint32_t, RGResourceIdHash> resource_use_id_to_writer_pass_idx_;
  std::unordered_set<RGResourceId, RGResourceIdHash> external_read_ids_;
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

  std::unordered_map<std::string, NameId> name_to_id_;
  std::vector<std::string> id_to_name_;

  std::unordered_map<uint64_t, RGResourceId> external_tex_handle_to_id_;
  std::unordered_map<uint64_t, RGResourceId> external_buf_handle_to_id_;

  struct ResourceRecord {
    RGResourceType type{};
    uint32_t physical_idx{UINT32_MAX};
    NameId debug_name{RenderGraph::kInvalidNameId};
    uint32_t latest_version{};
  };
  std::vector<ResourceRecord> resources_;
  ResourceRecord create_resource_record(RGResourceType type, uint32_t physical_idx,
                                        std::string_view debug_name);

  // cache for intermediate calc data
  std::unordered_set<uint32_t> intermed_pass_visited_;
  std::vector<uint32_t> intermed_pass_stack_;
  rhi::Device* device_{};
  size_t external_texture_count_{};
};

using RGPass = RenderGraph::Pass;

}  // namespace gfx

}  // namespace TENG_NAMESPACE
