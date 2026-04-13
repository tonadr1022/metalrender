#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
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
enum class TemporalSlotMode : uint8_t { DoubleBuffered, SingleSlot };

struct AttachmentInfo {
  rhi::TextureFormat format{rhi::TextureFormat::Undefined};
  glm::uvec2 dims{};
  uint32_t mip_levels{1};
  uint32_t array_layers{1};
  SizeClass size_class{SizeClass::Swapchain};
  bool is_swapchain_tex{};
  bool temporal{};
  TemporalSlotMode temporal_slot_mode{TemporalSlotMode::DoubleBuffered};

  bool operator==(const AttachmentInfo& other) const {
    return is_swapchain_tex == other.is_swapchain_tex && format == other.format &&
           dims == other.dims && mip_levels == other.mip_levels &&
           array_layers == other.array_layers && size_class == other.size_class &&
           temporal == other.temporal && temporal_slot_mode == other.temporal_slot_mode;
  }
};

// Descriptor for transient buffers created via `RenderGraph::create_buffer` (pooled per bake).
struct BufferInfo {
  size_t size{};
  // When true, the physical `BufferHandle` is not returned to the pool until after the next
  // `execute()` finishes (one execution boundary), so reuse on a later `bake()` cannot race
  // in-flight GPU reads. This is pool lifetime only: it does not allocate a second buffer, add an
  // RG version for "previous frame" reads, or implement temporal attachment/buffer history in the
  // graph.
  bool defer_reuse{};
  bool temporal{};
  TemporalSlotMode temporal_slot_mode{TemporalSlotMode::DoubleBuffered};
  bool operator==(const BufferInfo& other) const {
    return size == other.size && defer_reuse == other.defer_reuse && temporal == other.temporal &&
           temporal_slot_mode == other.temporal_slot_mode;
  }
};

// Custom hash functors defined in your own namespace
struct AttachmentInfoHash {
  size_t operator()(const AttachmentInfo& att_info) const {
    auto h = std::make_tuple((uint32_t)att_info.size_class, att_info.array_layers,
                             att_info.mip_levels, att_info.dims.x, att_info.dims.y,
                             (uint32_t)att_info.format, att_info.is_swapchain_tex,
                             att_info.temporal, (uint32_t)att_info.temporal_slot_mode);
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

struct BufferInfoHash {
  size_t operator()(const BufferInfo& buff_info) const {
    auto h = std::make_tuple(buff_info.size, buff_info.defer_reuse, buff_info.temporal,
                             (uint32_t)buff_info.temporal_slot_mode);
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

struct TexPoolKey {
  AttachmentInfo info;
  rhi::TextureUsage usage{rhi::TextureUsage::None};
  bool operator==(const TexPoolKey& o) const { return info == o.info && usage == o.usage; }
};

struct TexPoolKeyHash {
  size_t operator()(const TexPoolKey& k) const {
    using U = std::underlying_type_t<rhi::TextureUsage>;
    auto h = std::make_tuple(AttachmentInfoHash{}(k.info), static_cast<U>(k.usage));
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

struct BufPoolKey {
  BufferInfo info;
  rhi::BufferUsage usage{rhi::BufferUsage::None};
  bool operator==(const BufPoolKey& o) const { return info == o.info && usage == o.usage; }
};

struct BufPoolKeyHash {
  size_t operator()(const BufPoolKey& k) const {
    using U = std::underlying_type_t<rhi::BufferUsage>;
    auto h = std::make_tuple(BufferInfoHash{}(k.info), static_cast<U>(k.usage));
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

}  // namespace gfx

namespace gfx {

using ExecuteFn = std::function<void(rhi::CmdEncoder* enc)>;

class RenderGraph;

enum class RGResourceType {
  Texture,
  Buffer,
  ExternalTexture,
  ExternalBuffer,
};
inline bool is_texture(RGResourceType type) {
  return type == RGResourceType::Texture || type == RGResourceType::ExternalTexture;
}
inline bool is_buffer(RGResourceType type) {
  return type == RGResourceType::Buffer || type == RGResourceType::ExternalBuffer;
}
inline bool is_external(RGResourceType type) {
  return type == RGResourceType::ExternalTexture || type == RGResourceType::ExternalBuffer;
}

const char* to_string(RGResourceType type);

struct RGState {
  rhi::AccessFlags access{rhi::AccessFlags::None};
  rhi::PipelineStage stage{rhi::PipelineStage::TopOfPipe};
  rhi::ResourceLayout layout{rhi::ResourceLayout::Undefined};
};

inline bool operator==(const RGState& a, const RGState& b) noexcept {
  return a.access == b.access && a.stage == b.stage && a.layout == b.layout;
}

/// Subresource slice for texture reads/writes in the render graph. Buffers ignore this.
struct RgSubresourceRange {
  static constexpr uint32_t k_all_mips = UINT32_MAX;
  static constexpr uint32_t k_all_slices = UINT32_MAX;

  uint32_t base_mip{};
  uint32_t mip_count{k_all_mips};
  uint32_t base_slice{};
  uint32_t slice_count{k_all_slices};

  [[nodiscard]] constexpr static RgSubresourceRange all_mips_all_slices() noexcept { return {}; }
  [[nodiscard]] constexpr static RgSubresourceRange single_mip(uint32_t m) noexcept {
    return RgSubresourceRange{m, 1u, 0u, 1u};
  }
  [[nodiscard]] constexpr static RgSubresourceRange mip_span(uint32_t base_mip,
                                                             uint32_t count) noexcept {
    return RgSubresourceRange{base_mip, count, 0u, 1u};
  }
  [[nodiscard]] constexpr static RgSubresourceRange mip_layers(uint32_t base_mip, uint32_t mip_cnt,
                                                               uint32_t base_sl,
                                                               uint32_t slice_cnt) noexcept {
    return RgSubresourceRange{base_mip, mip_cnt, base_sl, slice_cnt};
  }
  [[nodiscard]] constexpr bool operator==(const RgSubresourceRange& o) const noexcept {
    return base_mip == o.base_mip && mip_count == o.mip_count && base_slice == o.base_slice &&
           slice_count == o.slice_count;
  }
};

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

// For maps keyed by logical external import: same GPU handle for all RGResourceId versions.
struct RGResourceIdStableHash {
  size_t operator()(RGResourceId id) const noexcept {
    auto h = std::make_tuple(id.idx, static_cast<uint32_t>(id.type));
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

struct RGResourceIdStableEq {
  bool operator()(const RGResourceId& a, const RGResourceId& b) const noexcept {
    return a.idx == b.idx && a.type == b.type;
  }
};

enum class RGPassType { None, Compute, Graphics, Transfer };

// This render graph has the following features:
// - auto pass ordering based on resource dependencies
// - auto attachment image creation
// - auto barrier placement
// - external resource integration
// - first-class temporal buffer / texture history
//
// Buffer / attachment pooling lifetime:
// - Transient attachment textures and internal buffers are pooled across bake/execute cycles.
// - `BufferInfo::defer_reuse` delays returning the same handle to the free list until the next
//   execute completes (see `defer_pool_*` members); it is not shader-visible "history".
// - Attachment textures are returned to the pool at the end of each execute (no defer path yet).
// - Temporal resources persist across frames and stay out of the transient pools entirely.
// - Temporal slot policy chooses either explicit current/history double-buffering or a single
//   persistent slot reused across frames while preserving logical history/current views.
//
// The following are TODOs
// - multiple queues
// - auto buffers
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
  /// Arm a single JSON+DOT dump when `developer_render_graph_dump_mode` is 3 (not thread-safe).
  void request_debug_dump_once() { debug_dump_once_requested_ = true; }
  void execute();
  void execute(rhi::CmdEncoder* enc);
  void shutdown();

  /// Debug/CI: validates texture barrier coalescing invariants (returns false on failure).
  [[nodiscard]] static bool run_barrier_coalesce_self_tests();

  class Pass {
   public:
    Pass() = default;
    Pass(NameId name_id, RenderGraph* rg, uint32_t pass_i, RGPassType type);

    // Sampled image (HLSL Texture* with .Sample / .SampleLevel / .Load -> SPIR-V OpImageFetch).
    // Barriers use ShaderReadOnly / SHADER_READ_ONLY_OPTIMAL; bind with bind_srv (SAMPLED_IMAGE).
    RGResourceId sample_tex(RGResourceId id);
    RGResourceId sample_tex(
        RGResourceId id, rhi::PipelineStage stage,
        RgSubresourceRange subresource = RgSubresourceRange::all_mips_all_slices());
    // Storage read (ShaderStorageRead -> compute GENERAL). Use for RWTexture2D[] / storage buffers,
    // not for Texture2D + bind_srv; that layout must match storage descriptors (e.g. bind_uav).
    RGResourceId read_tex(
        RGResourceId id, rhi::PipelineStage stage,
        RgSubresourceRange subresource = RgSubresourceRange::all_mips_all_slices());
    RGResourceId write_tex(
        RGResourceId id, rhi::PipelineStage stage,
        RgSubresourceRange subresource = RgSubresourceRange::all_mips_all_slices());
    RGResourceId write_color_output(RGResourceId id);
    RGResourceId write_depth_output(RGResourceId id);
    RGResourceId rw_tex(
        RGResourceId input, rhi::PipelineStage stage, rhi::AccessFlags read_access,
        rhi::AccessFlags write_access,
        RgSubresourceRange read_subresource = RgSubresourceRange::all_mips_all_slices(),
        RgSubresourceRange write_subresource = RgSubresourceRange::all_mips_all_slices());
    RGResourceId rw_color_output(RGResourceId input);
    RGResourceId rw_depth_output(RGResourceId input);
    void w_swapchain_tex(rhi::Swapchain* swapchain);
    RGResourceId read_buf(RGResourceId id, rhi::PipelineStage stage);
    RGResourceId copy_from_buf(RGResourceId id);
    RGResourceId read_buf(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access);
    RGResourceId write_buf(RGResourceId id, rhi::PipelineStage stage);
    RGResourceId rw_buf(RGResourceId input, rhi::PipelineStage stage);

    RGResourceId import_external_texture(rhi::TextureHandle tex_handle,
                                         std::string_view debug_name = {});
    RGResourceId import_external_texture(rhi::TextureHandle tex_handle, const RGState& initial,
                                         std::string_view debug_name = {});
    RGResourceId import_external_texture(const rhi::TextureHandleHolder& tex_handle,
                                         std::string_view debug_name = {}) {
      return import_external_texture(tex_handle.handle, debug_name);
    }
    RGResourceId import_external_buffer(rhi::BufferHandle buf_handle,
                                        std::string_view debug_name = {});
    RGResourceId import_external_buffer(rhi::BufferHandle buf_handle, const RGState& initial,
                                        std::string_view debug_name = {});
    RGResourceId import_external_buffer(const rhi::BufferHandleHolder& buf_handle,
                                        std::string_view debug_name = {}) {
      return import_external_buffer(buf_handle.handle, debug_name);
    }

    struct NameAndAccess {
      RGResourceId id;
      rhi::PipelineStage stage;
      rhi::AccessFlags acc;
      RGResourceType type;
      bool is_swapchain_write{false};
      RgSubresourceRange subresource{RgSubresourceRange::all_mips_all_slices()};
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

    [[nodiscard]] uint32_t get_idx() const { return pass_i_; }
    void set_ex(auto&& f) { execute_fn_ = f; }
    [[nodiscard]] const std::string& get_name() const { return rg_->debug_name(name_id_); }
    [[nodiscard]] const ExecuteFn& get_execute_fn() const { return execute_fn_; }
    [[nodiscard]] RGPassType type() const { return type_; }

   private:
    void add_read_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access,
                        RgSubresourceRange subresource = RgSubresourceRange::all_mips_all_slices());
    void add_write_usage(
        RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access,
        bool is_swapchain_write = false,
        RgSubresourceRange subresource = RgSubresourceRange::all_mips_all_slices());
    ExecuteFn execute_fn_;
    RenderGraph* rg_{};
    uint32_t pass_i_{};
    const NameId name_id_{kInvalidNameId};
    RGPassType type_{};
  };

  Pass& add_compute_pass(std::string_view name) { return add_pass(name, RGPassType::Compute); }
  Pass& add_transfer_pass(std::string_view name) { return add_pass(name, RGPassType::Transfer); }
  Pass& add_graphics_pass(std::string_view name) { return add_pass(name, RGPassType::Graphics); }

  RGResourceId create_texture(const AttachmentInfo& att_info, std::string_view debug_name = {});
  RGResourceId create_buffer(const BufferInfo& buf_info, std::string_view debug_name = {});
  [[nodiscard]] bool is_temporal(RGResourceId id) const;
  [[nodiscard]] bool has_history(RGResourceId id) const;
  RGResourceId history(RGResourceId id);
  RGResourceId import_external_texture(rhi::TextureHandle tex_handle,
                                       std::string_view debug_name = {});
  RGResourceId import_external_texture(rhi::TextureHandle tex_handle, const RGState& initial,
                                       std::string_view debug_name = {});
  /// Per-mip initial state for external textures (size must equal `tex_handle` mip count).
  RGResourceId import_external_texture(rhi::TextureHandle tex_handle,
                                       std::span<const RGState> per_mip_initial,
                                       std::string_view debug_name = {});
  RGResourceId import_external_texture(const rhi::TextureHandleHolder& tex_handle,
                                       std::string_view debug_name = {}) {
    return import_external_texture(tex_handle.handle, debug_name);
  }
  RGResourceId import_external_buffer(rhi::BufferHandle buf_handle,
                                      std::string_view debug_name = {});
  RGResourceId import_external_buffer(rhi::BufferHandle buf_handle, const RGState& initial,
                                      std::string_view debug_name = {});
  RGResourceId import_external_buffer(const rhi::BufferHandleHolder& buf_handle,
                                      std::string_view debug_name = {}) {
    return import_external_buffer(buf_handle.handle, debug_name);
  }

  [[nodiscard]] rhi::TextureHandle get_att_img(RGResourceId tex_id) const;
  [[nodiscard]] rhi::TextureHandle get_att_img(RGResourceHandle tex_handle) const;
  [[nodiscard]] rhi::BufferHandle get_buf(RGResourceId buf_id) const;
  [[nodiscard]] rhi::BufferHandle get_buf(RGResourceHandle buf_handle) const;
  [[nodiscard]] rhi::TextureHandle get_external_texture(RGResourceId id) const;
  [[nodiscard]] rhi::BufferHandle get_external_buffer(RGResourceId id) const;

  struct BarrierInfo {
    RGResourceHandle resource;
    RGState src_state;
    RGState dst_state;
    RGResourceId debug_id{};
    bool is_swapchain_write{false};
    RgSubresourceRange subresource{};
  };

 private:
  struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept {
      return std::hash<std::string_view>{}(s);
    }
  };

  Pass& add_pass(std::string_view name, RGPassType type);

  // begin usually called by Pass
  void register_write(RGResourceId id, Pass& pass);
  RGResourceId next_version(RGResourceId id);
  void add_external_read_id(RGResourceId id) { external_read_ids_.insert(id); }
  // end usually called by Pass

  NameId intern_name(std::string_view name);
  [[nodiscard]] const std::string& debug_name(NameId name) const;
  [[nodiscard]] std::string debug_name(RGResourceId id) const;

  AttachmentInfo* get_tex_att_info(RGResourceId id);
  AttachmentInfo* get_tex_att_info(RGResourceHandle handle);
  [[nodiscard]] RGResourceHandle get_physical_handle(RGResourceId id) const;
  rhi::BufferHandle get_external_buf(RGResourceHandle handle) {
    ASSERT(handle.type == RGResourceType::ExternalBuffer);
    return external_buffers_[handle.idx];
  }
  rhi::TextureHandle get_external_tex(RGResourceHandle handle) {
    ASSERT(handle.type == RGResourceType::ExternalTexture);
    return external_textures_[handle.idx];
  }

  void find_deps_recursive(uint32_t pass_i);
  static void dfs(const std::vector<std::unordered_set<uint32_t>>& pass_dependencies,
                  const std::vector<Pass>& passes, std::unordered_set<uint32_t>& curr_stack_passes,
                  std::unordered_set<uint32_t>& visited_passes, std::vector<uint32_t>& pass_stack,
                  uint32_t pass);

  void bake_reset_and_gc_pools_(glm::uvec2 fb_size);
  void bake_find_sink_passes_(bool verbose);
  void bake_compute_pass_order_(bool verbose);
  void bake_accumulate_physical_access_(std::vector<rhi::AccessFlags>& tex_physical_access,
                                        std::vector<rhi::AccessFlags>& buf_physical_access);
  void bake_allocate_transient_resources_(glm::uvec2 fb_size,
                                          const std::vector<rhi::AccessFlags>& tex_physical_access,
                                          const std::vector<rhi::AccessFlags>& buf_physical_access);
  void bake_allocate_temporal_resources_(glm::uvec2 fb_size);
  void bake_schedule_barriers_(bool verbose);
  void bake_validate_();
  void bake_write_debug_dump_if_requested_(glm::uvec2 fb_size);

  struct DebugDumpOnceRequestScope {
    RenderGraph* rg{};
    explicit DebugDumpOnceRequestScope(RenderGraph* r) : rg(r) {}
    ~DebugDumpOnceRequestScope();
  };
  bool debug_dump_once_requested_{false};

  // glm::uvec2 prev_frame_fb_size_{};
  std::vector<AttachmentInfo> tex_att_infos_;
  std::vector<BufferInfo> buffer_infos_;
  std::vector<rhi::TextureHandle> tex_att_handles_;
  std::vector<rhi::BufferHandle> buffer_handles_;
  // Per-buffer slot: valid handle when `buffer_infos_[i].defer_reuse` so execute teardown can route
  // the handle into `defer_pool_pending_return_` instead of `free_bufs_`.
  std::vector<rhi::BufferHandle> defer_pool_handles_by_slot_;

  std::vector<Pass> passes_;
  std::vector<std::vector<BarrierInfo>> pass_barrier_infos_;
  std::vector<std::unordered_set<uint32_t>> pass_dependencies_;
  std::unordered_map<RGResourceId, uint32_t, RGResourceIdHash> resource_use_id_to_writer_pass_idx_;
  std::unordered_set<RGResourceId, RGResourceIdHash> external_read_ids_;
  std::vector<rhi::TextureHandle> external_textures_;
  std::vector<rhi::BufferHandle> external_buffers_;
  std::vector<rhi::TextureHandle> curr_submitted_swapchain_textures_;

  std::unordered_map<TexPoolKey, std::vector<rhi::TextureHandle>, TexPoolKeyHash> free_atts_;
  std::unordered_map<BufPoolKey, std::vector<rhi::BufferHandle>, BufPoolKeyHash> free_bufs_;
  // Buffers waiting one execute boundary before merging into `free_bufs_` (see `defer_reuse`).
  std::unordered_map<BufPoolKey, std::vector<rhi::BufferHandle>, BufPoolKeyHash>
      defer_pool_pending_return_;

  struct BufferInfoAndHandle {
    BufferInfo buf_info;
    rhi::BufferHandle buf_handle;
  };

  std::vector<uint32_t> sink_passes_;
  std::vector<uint32_t> pass_stack_;

  std::unordered_map<std::string, NameId, StringHash, std::equal_to<>> name_to_id_;
  std::vector<std::string> id_to_name_;

  std::unordered_map<uint64_t, RGResourceId> external_tex_handle_to_id_;
  std::unordered_map<uint64_t, RGResourceId> external_buf_handle_to_id_;
  std::unordered_map<RGResourceId, rhi::TextureHandle, RGResourceIdStableHash, RGResourceIdStableEq>
      rg_id_to_external_texture_;
  std::unordered_map<RGResourceId, rhi::BufferHandle, RGResourceIdStableHash, RGResourceIdStableEq>
      rg_id_to_external_buffer_;
  std::unordered_map<uint64_t, RGState> external_initial_states_;
  /// Optional per-mip initial `RGState` for external textures (key: physical handle `to64()`).
  std::unordered_map<uint64_t, std::vector<RGState>> external_tex_mip_initial_states_;

  static constexpr uint32_t k_invalid_temporal_idx = UINT32_MAX;

  struct TemporalResourceKey {
    NameId debug_name{kInvalidNameId};
    RGResourceType type{};
    bool operator==(const TemporalResourceKey& other) const {
      return debug_name == other.debug_name && type == other.type;
    }
  };

  struct TemporalResourceKeyHash {
    size_t operator()(const TemporalResourceKey& key) const noexcept {
      auto h = std::make_tuple(key.debug_name, static_cast<uint32_t>(key.type));
      return util::hash::tuple_hash<decltype(h)>{}(h);
    }
  };

  struct TemporalTextureState {
    std::vector<RGState> per_mip;
  };

  struct TemporalTextureRecord {
    AttachmentInfo info{};
    NameId debug_name{kInvalidNameId};
    TemporalSlotMode slot_mode{TemporalSlotMode::DoubleBuffered};
    rhi::TextureUsage usage{rhi::TextureUsage::None};
    std::array<rhi::TextureHandle, 2> handles{};
    std::array<TemporalTextureState, 2> slot_states{};
    uint32_t current_slot{};
    bool history_valid{};
    bool current_used_this_frame{};
    bool history_used_this_frame{};
    bool wrote_current_this_frame{};
  };

  struct TemporalBufferRecord {
    BufferInfo info{};
    NameId debug_name{kInvalidNameId};
    TemporalSlotMode slot_mode{TemporalSlotMode::DoubleBuffered};
    rhi::BufferUsage usage{rhi::BufferUsage::None};
    std::array<rhi::BufferHandle, 2> handles{};
    std::array<RGState, 2> slot_states{};
    uint32_t current_slot{};
    bool history_valid{};
    bool current_used_this_frame{};
    bool history_used_this_frame{};
    bool wrote_current_this_frame{};
  };

  struct ResourceRecord {
    RGResourceType type{};
    uint32_t physical_idx{UINT32_MAX};
    NameId debug_name{RenderGraph::kInvalidNameId};
    uint32_t latest_version{};
    uint32_t temporal_idx{k_invalid_temporal_idx};
    bool temporal_history_view{};
  };
  std::vector<ResourceRecord> resources_;
  ResourceRecord create_resource_record(RGResourceType type, uint32_t physical_idx,
                                        std::string_view debug_name);
  ResourceRecord create_temporal_resource_record(RGResourceType type, uint32_t physical_idx,
                                                 uint32_t temporal_idx, bool history_view,
                                                 std::string_view debug_name);
  template <typename Record, typename Info, typename DestroyFn>
  uint32_t get_temporal_resource_index_(
      NameId debug_name, const Info& info, RGResourceType resource_type,
      std::unordered_map<TemporalResourceKey, uint32_t, TemporalResourceKeyHash>& by_key,
      std::vector<Record>& records, DestroyFn&& destroy);
  uint32_t get_temporal_buffer_index_(NameId debug_name, const BufferInfo& info);
  uint32_t get_temporal_texture_index_(NameId debug_name, const AttachmentInfo& info);
  RGResourceId allocate_temporal_history_id_(RGResourceType type, uint32_t temporal_idx,
                                             bool distinct_history_slot, uint32_t base_physical_idx,
                                             const std::string& hist_name);
  [[nodiscard]] static constexpr uint32_t temporal_slot_count(TemporalSlotMode slot_mode) {
    return slot_mode == TemporalSlotMode::SingleSlot ? 1u : 2u;
  }
  [[nodiscard]] static constexpr bool temporal_has_distinct_history_slot(
      TemporalSlotMode slot_mode) {
    return slot_mode == TemporalSlotMode::DoubleBuffered;
  }
  [[nodiscard]] static constexpr uint32_t resolve_current_slot(uint32_t current_slot) {
    return current_slot;
  }
  [[nodiscard]] static constexpr uint32_t resolve_history_slot(TemporalSlotMode slot_mode,
                                                               uint32_t current_slot) {
    return temporal_has_distinct_history_slot(slot_mode) ? (current_slot ^ 1u) : current_slot;
  }
  void reset_temporal_frame_usage_();
  void destroy_temporal_texture_record_(TemporalTextureRecord& record);
  void destroy_temporal_buffer_record_(TemporalBufferRecord& record);
  void install_temporal_buffer_slot_(uint32_t resource_idx);
  void install_temporal_texture_slot_(uint32_t resource_idx);
  void add_single_slot_temporal_dependencies_and_validate_();
  void mark_temporal_use_(RGResourceId id, bool is_write);

  template <typename Fn>
  [[nodiscard]] static bool temporal_any_slot_invalidated_(uint32_t slot_count, Fn&& fn) {
    for (uint32_t slot = 0; slot < slot_count; ++slot) {
      if (fn(slot)) {
        return true;
      }
    }
    return false;
  }

  template <typename TemporalRecord, typename DerivedUsage>
  static void reset_temporal_for_resource_recreate_(TemporalRecord& temporal,
                                                    DerivedUsage derived_usage) {
    temporal.usage = derived_usage;
    temporal.current_slot = 0;
    temporal.history_valid = false;
    temporal.slot_states = {};
  }

  // cache for intermediate calc data (pass dependency DFS: 0=white, 1=gray, 2=black)
  std::vector<uint8_t> pass_dep_dfs_state_;
  std::vector<uint32_t> pass_dep_dfs_path_;
  std::vector<uint32_t> intermed_pass_stack_;
  rhi::Device* device_{};
  size_t external_texture_count_{};
  std::unordered_map<TemporalResourceKey, uint32_t, TemporalResourceKeyHash>
      temporal_textures_by_key_;
  std::unordered_map<TemporalResourceKey, uint32_t, TemporalResourceKeyHash>
      temporal_buffers_by_key_;
  std::vector<TemporalTextureRecord> temporal_textures_;
  std::vector<TemporalBufferRecord> temporal_buffers_;
};

using RGPass = RenderGraph::Pass;

void add_buffer_readback_copy(RenderGraph& rg, std::string_view pass_name, RGResourceId src_buf,
                              rhi::BufferHandle dst_buf, RGResourceId dst_rg_id, size_t src_offset,
                              size_t dst_offset, size_t size_bytes);

}  // namespace gfx

}  // namespace TENG_NAMESPACE
