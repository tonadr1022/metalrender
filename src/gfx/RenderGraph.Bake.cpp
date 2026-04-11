#include "RenderGraph.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <tracy/Tracy.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "gfx/RenderGraph.Format.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Texture.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

namespace {

// Pass→producer dependency walk in bake() (white/gray/black DFS).
constexpr uint8_t kPassDepDfsWhite = 0;
constexpr uint8_t kPassDepDfsGray = 1;
constexpr uint8_t kPassDepDfsBlack = 2;

struct RgSubresourceStateKey {
  RGResourceType type{};
  uint32_t idx{};
  int32_t mip{-1};
  int32_t slice{-1};
  bool operator==(RgSubresourceStateKey o) const {
    return type == o.type && idx == o.idx && mip == o.mip && slice == o.slice;
  }
};
struct RgSubresourceStateKeyHash {
  size_t operator()(RgSubresourceStateKey k) const {
    auto h = std::make_tuple(static_cast<uint32_t>(k.type), k.idx, k.mip, k.slice);
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

template <typename Flags, typename Bits>
constexpr Flags flag_or(Flags x, Bits y) noexcept {
  return static_cast<Flags>(static_cast<std::underlying_type_t<Flags>>(x) |
                            static_cast<uint64_t>(y));
}

constexpr uint32_t kStageBitCount = 64;

inline void for_each_set_stage_bits(rhi::PipelineStage stage, const auto& fn) {
  auto mask = static_cast<uint64_t>(stage);
  for (uint32_t i = 0; i < kStageBitCount; ++i) {
    if ((mask & (1ull << i)) != 0) {
      fn(i);
    }
  }
}

inline rhi::PipelineStage stage_from_bit(uint32_t bit) {
  return static_cast<rhi::PipelineStage>(1ull << bit);
}

bool has_write_access(rhi::AccessFlags access) {
  return has_flag(access, rhi::AccessFlags::AnyWrite);
}

rhi::ResourceLayout layout_from_access(rhi::AccessFlags access, RGPassType pass_type,
                                       bool is_swapchain_write) {
  if (is_swapchain_write) {
    return rhi::ResourceLayout::ColorAttachment;
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentRead) ||
      has_flag(access, rhi::AccessFlags::ColorAttachmentWrite)) {
    return rhi::ResourceLayout::ColorAttachment;
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilRead) ||
      has_flag(access, rhi::AccessFlags::DepthStencilWrite)) {
    return rhi::ResourceLayout::DepthStencil;
  }
  if (has_flag(access, rhi::AccessFlags::TransferRead)) {
    return rhi::ResourceLayout::TransferSrc;
  }
  if (has_flag(access, rhi::AccessFlags::TransferWrite)) {
    return rhi::ResourceLayout::TransferDst;
  }
  if (has_flag(access, rhi::AccessFlags::InputAttachmentRead)) {
    return rhi::ResourceLayout::InputAttachment;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderSampledRead) ||
      has_flag(access, rhi::AccessFlags::ShaderRead)) {
    return rhi::ResourceLayout::ShaderReadOnly;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageRead) ||
      has_flag(access, rhi::AccessFlags::ShaderStorageWrite) ||
      has_flag(access, rhi::AccessFlags::ShaderWrite)) {
    return pass_type == RGPassType::Compute ? rhi::ResourceLayout::ComputeRW
                                            : rhi::ResourceLayout::General;
  }
  return rhi::ResourceLayout::Undefined;
}

rhi::TextureUsage texture_usage_from_accumulated_access(rhi::AccessFlags acc) {
  using U = rhi::TextureUsage;
  auto u = U::None;
  if (has_flag(acc, rhi::AccessFlags::ColorAttachmentRead) ||
      has_flag(acc, rhi::AccessFlags::ColorAttachmentWrite) ||
      has_flag(acc, rhi::AccessFlags::InputAttachmentRead)) {
    u |= U::ColorAttachment;
  }
  if (has_flag(acc, rhi::AccessFlags::DepthStencilRead) ||
      has_flag(acc, rhi::AccessFlags::DepthStencilWrite)) {
    u |= U::DepthStencilAttachment;
  }
  if (has_flag(acc, rhi::AccessFlags::ShaderSampledRead) ||
      has_flag(acc, rhi::AccessFlags::ShaderRead)) {
    u |= U::Sample;
  }
  if (has_flag(acc, rhi::AccessFlags::ShaderStorageRead) ||
      has_flag(acc, rhi::AccessFlags::ShaderStorageWrite)) {
    u |= U::Storage;
  }
  if (has_flag(acc, rhi::AccessFlags::ShaderWrite)) {
    u |= U::ShaderWrite;
  }
  if (has_flag(acc, rhi::AccessFlags::TransferRead)) {
    u |= U::TransferSrc;
  }
  if (has_flag(acc, rhi::AccessFlags::TransferWrite)) {
    u |= U::TransferDst;
  }
  return u;
}

rhi::TextureUsage fallback_texture_usage(const AttachmentInfo& att_info) {
  using U = rhi::TextureUsage;
  const auto attach =
      !is_depth_format(att_info.format) ? U::ColorAttachment : U::DepthStencilAttachment;
  return attach | U::Sample | U::Storage;
}

rhi::TextureUsage texture_desc_usage_for_bake(rhi::AccessFlags accumulated,
                                              const AttachmentInfo& att_info) {
  const auto u = texture_usage_from_accumulated_access(accumulated);
  if (u == rhi::TextureUsage::None) {
    return fallback_texture_usage(att_info);
  }
  return u;
}

rhi::BufferUsage buffer_usage_from_accumulated_access(rhi::AccessFlags acc) {
  using B = rhi::BufferUsage;
  auto u = B::None;
  if (has_flag(acc, rhi::AccessFlags::ShaderStorageRead) ||
      has_flag(acc, rhi::AccessFlags::ShaderStorageWrite)) {
    u |= B::Storage;
  }
  if (has_flag(acc, rhi::AccessFlags::IndirectCommandRead)) {
    u |= B::Indirect;
  }
  if (has_flag(acc, rhi::AccessFlags::ShaderRead)) {
    u |= B::Uniform;
  }
  if (has_flag(acc, rhi::AccessFlags::IndexRead)) {
    u |= B::Index;
  }
  if (has_flag(acc, rhi::AccessFlags::VertexAttributeRead)) {
    u |= B::Vertex;
  }
  if (has_flag(acc, rhi::AccessFlags::UniformRead)) {
    u |= B::Uniform;
  }
  return u;
}

struct SubresourceState {
  rhi::PipelineStage last_write_stage{rhi::PipelineStage::None};
  rhi::AccessFlags to_flush_access{rhi::AccessFlags::None};
  rhi::ResourceLayout layout{rhi::ResourceLayout::Undefined};
  bool has_write{false};
  std::array<rhi::AccessFlags, kStageBitCount> invalidated_in_stage{};
};

inline void clear_invalidated(SubresourceState& state) {
  for (auto& entry : state.invalidated_in_stage) {
    entry = rhi::AccessFlags::None;
  }
}

inline rhi::PipelineStage read_stage_mask(const SubresourceState& state) {
  rhi::PipelineStage mask = rhi::PipelineStage::None;
  for (uint32_t i = 0; i < kStageBitCount; ++i) {
    if (state.invalidated_in_stage[i] != rhi::AccessFlags::None) {
      mask = flag_or(mask, stage_from_bit(i));
    }
  }
  return mask;
}

// returns true if, for any pipeline stage in stages, access has bits not yet recorded in
// invalidated_in_stage for that stage.
inline bool need_invalidate_read(const SubresourceState& state, rhi::PipelineStage stages,
                                 rhi::AccessFlags access) {
  bool need_invalidate = false;
  for_each_set_stage_bits(stages, [&](uint32_t bit) {
    const auto inv = state.invalidated_in_stage[bit];
    const uint64_t missing = static_cast<uint64_t>(access) & ~static_cast<uint64_t>(inv);
    if (missing != 0) {
      need_invalidate = true;
    }
  });
  return need_invalidate;
}

uint32_t rg_resolved_mip_level_count(const RgSubresourceRange& r, uint32_t texture_mip_levels) {
  if (r.base_mip >= texture_mip_levels) {
    return 0;
  }
  if (r.mip_count == RgSubresourceRange::k_all_mips) {
    return texture_mip_levels - r.base_mip;
  }
  ASSERT(r.mip_count <= texture_mip_levels - r.base_mip);
  return r.mip_count;
}

uint32_t rg_resolved_slice_count(const RgSubresourceRange& r, uint32_t texture_array_layers) {
  if (r.base_slice >= texture_array_layers) {
    return 0;
  }
  if (r.slice_count == RgSubresourceRange::k_all_slices) {
    return texture_array_layers - r.base_slice;
  }
  ASSERT(r.slice_count <= texture_array_layers - r.base_slice);
  return r.slice_count;
}

bool rg_barrier_mergeable(const RenderGraph::BarrierInfo& a, const RenderGraph::BarrierInfo& b) {
  if (a.resource.to64() != b.resource.to64()) {
    return false;
  }
  if (a.is_swapchain_write != b.is_swapchain_write) {
    return false;
  }
  if (!(a.src_state == b.src_state) || !(a.dst_state == b.dst_state)) {
    return false;
  }
  if (is_buffer(a.resource.type)) {
    return false;
  }
  if (a.subresource.base_slice != b.subresource.base_slice ||
      a.subresource.slice_count != b.subresource.slice_count) {
    return false;
  }
  return b.subresource.base_mip == a.subresource.base_mip + a.subresource.mip_count;
}

void coalesce_pass_texture_barriers(std::vector<RenderGraph::BarrierInfo>& barriers) {
  if (barriers.size() < 2) {
    return;
  }
  std::ranges::stable_sort(barriers, [](const RenderGraph::BarrierInfo& x,
                                        const RenderGraph::BarrierInfo& y) {
    if (x.resource.to64() != y.resource.to64()) {
      return x.resource.to64() < y.resource.to64();
    }
    if (x.is_swapchain_write != y.is_swapchain_write) {
      return x.is_swapchain_write < y.is_swapchain_write;
    }
    if (x.src_state.layout != y.src_state.layout) {
      return static_cast<int>(x.src_state.layout) < static_cast<int>(y.src_state.layout);
    }
    if (x.dst_state.layout != y.dst_state.layout) {
      return static_cast<int>(x.dst_state.layout) < static_cast<int>(y.dst_state.layout);
    }
    if (x.src_state.access != y.src_state.access) {
      return static_cast<uint64_t>(x.src_state.access) < static_cast<uint64_t>(y.src_state.access);
    }
    if (x.dst_state.access != y.dst_state.access) {
      return static_cast<uint64_t>(x.dst_state.access) < static_cast<uint64_t>(y.dst_state.access);
    }
    if (x.src_state.stage != y.src_state.stage) {
      return static_cast<uint64_t>(x.src_state.stage) < static_cast<uint64_t>(y.src_state.stage);
    }
    if (x.dst_state.stage != y.dst_state.stage) {
      return static_cast<uint64_t>(x.dst_state.stage) < static_cast<uint64_t>(y.dst_state.stage);
    }
    if (x.subresource.base_slice != y.subresource.base_slice) {
      return x.subresource.base_slice < y.subresource.base_slice;
    }
    if (x.subresource.slice_count != y.subresource.slice_count) {
      return x.subresource.slice_count < y.subresource.slice_count;
    }
    return x.subresource.base_mip < y.subresource.base_mip;
  });

  std::vector<RenderGraph::BarrierInfo> out;
  out.reserve(barriers.size());
  for (size_t i = 0; i < barriers.size();) {
    RenderGraph::BarrierInfo cur = barriers[i];
    if (is_buffer(cur.resource.type)) {
      out.push_back(cur);
      ++i;
      continue;
    }
    size_t j = i + 1;
    while (j < barriers.size() && rg_barrier_mergeable(cur, barriers[j])) {
      cur.subresource.mip_count += barriers[j].subresource.mip_count;
      ++j;
    }
    out.push_back(cur);
    i = j;
  }
  barriers.swap(out);
}

}  // namespace

void RenderGraph::bake(glm::uvec2 fb_size, bool verbose) {
  ZoneScoped;
  if (verbose) {
    LINFO("//////////// Baking Render Graph ////////////");
  }
  bake_reset_and_gc_pools_(fb_size);
  bake_find_sink_passes_(verbose);
  bake_compute_pass_order_(verbose);
  std::vector<rhi::AccessFlags> tex_physical_access(tex_att_infos_.size(), rhi::AccessFlags::None);
  std::vector<rhi::AccessFlags> buf_physical_access(buffer_infos_.size(), rhi::AccessFlags::None);
  bake_accumulate_physical_access_(tex_physical_access, buf_physical_access);
  bake_allocate_transient_resources_(fb_size, tex_physical_access, buf_physical_access);
  bake_schedule_barriers_(verbose);
  bake_validate_();
  bake_write_debug_dump_if_requested_(fb_size);
  if (verbose) {
    LINFO("//////////// Done Baking Render Graph ////////////");
  }
}

void RenderGraph::bake_reset_and_gc_pools_(glm::uvec2 fb_size) {
  ZoneScopedN("RG bake: reset_and_gc_pools");
  {
    sink_passes_.clear();
    pass_dependencies_.clear();
    pass_dep_dfs_state_.clear();
    pass_dep_dfs_path_.clear();
    intermed_pass_stack_.clear();
    pass_stack_.clear();

    static std::vector<TexPoolKey> stale_tex_keys;
    stale_tex_keys.clear();

    for (auto& [key, handles] : free_atts_) {
      ASSERT(!handles.empty());
      auto* tex = device_->get_tex(handles[0]);
      if (key.info.size_class == SizeClass::Swapchain && glm::uvec2{tex->desc().dims} != fb_size) {
        for (const auto& handle : handles) {
          device_->destroy(handle);
        }
        stale_tex_keys.emplace_back(key);
      }
    }
    for (const auto& stale_key : stale_tex_keys) {
      free_atts_.erase(stale_key);
    }
  }
}

void RenderGraph::bake_find_sink_passes_(bool verbose) {
  ZoneScopedN("RG bake: find_sink_passes");
  // find sink nodes, ie nodes that don't write to anything
  sink_passes_.clear();
  ALWAYS_ASSERT(passes_.size() > 0);
  // find resource that don't get read (swapchain img)
  // find pass that writes to that resource
  for (size_t pass_i = 0; pass_i < passes_.size(); pass_i++) {
    auto& pass = passes_[pass_i];
    bool sink = false;
    if (pass.swapchain_write_) {
      sink = true;
    } else {
      for (const auto& write : pass.get_external_writes()) {
        if (!external_read_ids_.contains(write.id)) {
          sink = true;
          break;
        }
      }
    }
    if (sink) {
      sink_passes_.push_back(pass_i);
    }
  }

  if (verbose) {
    LINFO("Sink passes:");
    for (auto p : sink_passes_) {
      LINFO("\t{}", passes_[p].get_name());
    }
    LINFO("\n\n");
  }
}

void RenderGraph::bake_compute_pass_order_(bool verbose) {
  ZoneScopedN("RG bake: compute_pass_order");
  {  // pass ordering
    for (auto& p : pass_dependencies_) {
      p.clear();
    }
    pass_dependencies_.resize(passes_.size());
    pass_dep_dfs_state_.assign(passes_.size(), kPassDepDfsWhite);
    pass_dep_dfs_path_.clear();
    pass_dep_dfs_path_.reserve(passes_.size());
    for (uint32_t pass_i : sink_passes_) {
      intermed_pass_stack_.push_back(pass_i);
      find_deps_recursive(pass_i);
    }
    static std::unordered_set<uint32_t> curr_stack_passes;
    static std::unordered_set<uint32_t> visited_passes;
    curr_stack_passes.clear();
    visited_passes.clear();
    curr_stack_passes.reserve(passes_.size());
    visited_passes.reserve(passes_.size());
    pass_stack_.clear();
    for (uint32_t root : intermed_pass_stack_) {
      dfs(pass_dependencies_, passes_, curr_stack_passes, visited_passes, pass_stack_, root);
    }
    if (verbose) {
      LINFO("//////////////// Pass Order ////////////////");
      for (auto pass_i : pass_stack_) {
        auto& pass = passes_[pass_i];
        LINFO("[PASS]: {}", pass.get_name());
        const std::vector<RenderGraph::Pass::NameAndAccess>* arrays[4] = {
            &pass.get_internal_reads(), &pass.get_internal_writes(), &pass.get_external_reads(),
            &pass.get_external_writes()};
        for (auto& arr : arrays) {
          for (const auto& u : *arr) {
            LINFO("{:<50}\t{:<50}\t{:<50}", debug_name(u.id), rg_fmt::to_string(u.acc),
                  rg_fmt::to_string(u.stage));
          }
        }
      }
      LINFO("");
    }
  }
}

void RenderGraph::bake_accumulate_physical_access_(
    std::vector<rhi::AccessFlags>& tex_physical_access,
    std::vector<rhi::AccessFlags>& buf_physical_access) {
  ZoneScopedN("RG bake: accumulate_physical_access");
  auto accumulate_name_access = [&](const Pass::NameAndAccess& use) {
    if (use.id.type == RGResourceType::ExternalTexture ||
        use.id.type == RGResourceType::ExternalBuffer) {
      return;
    }
    const RGResourceHandle phys = get_physical_handle(use.id);
    if (phys.type == RGResourceType::Texture) {
      ALWAYS_ASSERT(phys.idx < tex_physical_access.size());
      tex_physical_access[phys.idx] = flag_or(tex_physical_access[phys.idx], use.acc);
    } else if (phys.type == RGResourceType::Buffer) {
      ALWAYS_ASSERT(phys.idx < buf_physical_access.size());
      buf_physical_access[phys.idx] = flag_or(buf_physical_access[phys.idx], use.acc);
    }
  };
  for (uint32_t pass_i : pass_stack_) {
    const auto& pass = passes_[pass_i];
    for (const auto& u : pass.get_external_writes()) {
      accumulate_name_access(u);
    }
    for (const auto& u : pass.get_internal_writes()) {
      accumulate_name_access(u);
    }
    for (const auto& u : pass.get_external_reads()) {
      accumulate_name_access(u);
    }
    for (const auto& u : pass.get_internal_reads()) {
      accumulate_name_access(u);
    }
  }
}

void RenderGraph::bake_allocate_transient_resources_(
    glm::uvec2 fb_size, const std::vector<rhi::AccessFlags>& tex_physical_access,
    const std::vector<rhi::AccessFlags>& buf_physical_access) {
  ZoneScopedN("RG bake: allocate_transient");
  {
    // create attachment images
    tex_att_handles_.clear();
    for (size_t i = 0; i < tex_att_infos_.size(); ++i) {
      const auto& att_info = tex_att_infos_[i];
      if (att_info.is_swapchain_tex) {
        ASSERT(0);
        continue;
      }
      auto get_att_dims = [&att_info, &fb_size]() {
        if (att_info.size_class == SizeClass::Swapchain) {
          return fb_size;
        }
        return att_info.dims;
      };

      const rhi::TextureUsage derived_usage =
          texture_desc_usage_for_bake(tex_physical_access[i], att_info);

      rhi::TextureHandle actual_att_handle{};
      const TexPoolKey pool_key{att_info, derived_usage};
      auto free_att_it = free_atts_.find(pool_key);
      if (free_att_it != free_atts_.end()) {
        auto& texture_handles = free_att_it->second;
        if (!texture_handles.empty()) {
          actual_att_handle = texture_handles.back();
          texture_handles.pop_back();
        }
      }

      if (!actual_att_handle.is_valid()) {
        auto dims = get_att_dims();
        auto att_tx_handle =
            device_->create_tex(rhi::TextureDesc{.format = att_info.format,
                                                 .usage = derived_usage,
                                                 .dims = glm::uvec3{dims.x, dims.y, 1},
                                                 .mip_levels = att_info.mip_levels,
                                                 .array_length = att_info.array_layers,
                                                 .name = "render_graph_tex_att"});
        actual_att_handle = att_tx_handle;
      }

      tex_att_handles_.emplace_back(actual_att_handle);
    }
    ASSERT(tex_att_handles_.size() == tex_att_infos_.size());
  }
  {  // create buffers
    buffer_handles_.clear();
    defer_pool_handles_by_slot_.clear();
    for (size_t i = 0; i < buffer_infos_.size(); ++i) {
      const auto& binfo = buffer_infos_[i];
      const rhi::BufferUsage derived_usage =
          buffer_usage_from_accumulated_access(buf_physical_access[i]);

      rhi::BufferHandle actual_buf_handle{};
      const BufPoolKey pool_key{binfo, derived_usage};
      auto free_buf_it = free_bufs_.find(pool_key);
      if (free_buf_it != free_bufs_.end()) {
        auto& buffer_handles = free_buf_it->second;
        if (!buffer_handles.empty()) {
          actual_buf_handle = buffer_handles.back();
          buffer_handles.pop_back();
          if (buffer_handles.empty()) {
            free_bufs_.erase(free_buf_it);
          }
        }
      }
      if (!actual_buf_handle.is_valid()) {
        auto buf_handle = device_->create_buf(rhi::BufferDesc{
            .usage = derived_usage,
            .size = binfo.size,
            .name = "render_graph_buffer",
        });
        actual_buf_handle = buf_handle;
      }
      buffer_handles_.emplace_back(actual_buf_handle);
      if (binfo.defer_reuse) {
        defer_pool_handles_by_slot_.emplace_back(actual_buf_handle);
      } else {
        defer_pool_handles_by_slot_.emplace_back();
      }
    }
  }
  // enqueue delete unused free buffers
  for (auto& [key, handles] : free_bufs_) {
    (void)key;
    for (const auto& handle : handles) {
      device_->destroy(handle);
    }
  }
  free_bufs_.clear();
}

void RenderGraph::bake_schedule_barriers_(bool verbose) {
  ZoneScopedN("RG bake: schedule_barriers");
  std::unordered_map<RgSubresourceStateKey, SubresourceState, RgSubresourceStateKeyHash>
      subresource_states;

  auto get_resource_state = [&](RGResourceHandle handle, int32_t mip,
                                int32_t slice) -> SubresourceState& {
    RgSubresourceStateKey key{.type = handle.type, .idx = handle.idx, .mip = mip, .slice = slice};
    if (auto it = subresource_states.find(key); it != subresource_states.end()) {
      return it->second;
    }
    SubresourceState init{};
    const uint64_t h64 = handle.to64();
    if (handle.type == RGResourceType::ExternalTexture) {
      if (auto mit = external_tex_mip_initial_states_.find(h64);
          mit != external_tex_mip_initial_states_.end() && !mit->second.empty() && mip >= 0 &&
          static_cast<size_t>(mip) < mit->second.size()) {
        const RGState external = mit->second[static_cast<size_t>(mip)];
        init.layout = external.layout;
        init.to_flush_access = external.access;
        init.has_write = has_write_access(external.access);
        init.last_write_stage = external.stage == rhi::PipelineStage::None
                                    ? rhi::PipelineStage::TopOfPipe
                                    : external.stage;
      } else if (auto init_it = external_initial_states_.find(h64);
                 init_it != external_initial_states_.end()) {
        const RGState external = init_it->second;
        init.layout = external.layout;
        init.to_flush_access = external.access;
        init.has_write = has_write_access(external.access);
        init.last_write_stage = external.stage == rhi::PipelineStage::None
                                    ? rhi::PipelineStage::TopOfPipe
                                    : external.stage;
      }
    } else if (auto init_it = external_initial_states_.find(h64);
               init_it != external_initial_states_.end()) {
      const RGState external = init_it->second;
      init.layout = external.layout;
      init.to_flush_access = external.access;
      init.has_write = has_write_access(external.access);
      init.last_write_stage = external.stage == rhi::PipelineStage::None
                                  ? rhi::PipelineStage::TopOfPipe
                                  : external.stage;
    }
    auto [it, inserted] = subresource_states.emplace(key, init);
    return it->second;
  };

  for (auto& b : pass_barrier_infos_) {
    b.clear();
  }
  if (pass_barrier_infos_.size() < passes_.size()) {
    pass_barrier_infos_.resize(passes_.size());
  }
  if (verbose) {
    LINFO("//////////////// Barriers ////////////////");
  }
  uint32_t barrier_pass_exec_ord = 0;
  size_t barrier_total_count = 0;
  for (auto pass_i : pass_stack_) {
    ASSERT(pass_i < passes_.size());
    const auto& pass = passes_[pass_i];
    auto& barriers = pass_barrier_infos_[pass_i];

    struct PassUse {
      RGResourceHandle resource;
      RGResourceId debug_id{};
      rhi::AccessFlags access{rhi::AccessFlags::None};
      rhi::PipelineStage stage{rhi::PipelineStage::None};
      RGResourceType type{};
      bool is_swapchain_write{false};
      int32_t mip{-1};
      int32_t slice{-1};
    };
    std::unordered_map<RgSubresourceStateKey, PassUse, RgSubresourceStateKeyHash> pass_uses;

    auto merge_pass_use_key = [&](const RgSubresourceStateKey& key, const PassUse& incoming) {
      auto it = pass_uses.find(key);
      if (it == pass_uses.end()) {
        pass_uses.emplace(key, incoming);
        return;
      }
      auto& entry = it->second;
      entry.access = flag_or(entry.access, incoming.access);
      entry.stage = flag_or(entry.stage, incoming.stage);
      entry.is_swapchain_write |= incoming.is_swapchain_write;
    };

    auto accumulate_use = [&](const RenderGraph::Pass::NameAndAccess& use) {
      const auto rg_resource_handle = get_physical_handle(use.id);
      if (use.type == RGResourceType::Buffer || use.type == RGResourceType::ExternalBuffer) {
        const RgSubresourceStateKey key{
            .type = rg_resource_handle.type, .idx = rg_resource_handle.idx, .mip = -1, .slice = -1};
        merge_pass_use_key(key, PassUse{.resource = rg_resource_handle,
                                        .debug_id = use.id,
                                        .access = use.acc,
                                        .stage = use.stage,
                                        .type = use.type,
                                        .is_swapchain_write = use.is_swapchain_write,
                                        .mip = -1,
                                        .slice = -1});
        return;
      }

      uint32_t tex_mip_levels{};
      uint32_t tex_array_layers{};
      if (use.type == RGResourceType::ExternalTexture) {
        rhi::Texture* tex = device_->get_tex(get_external_tex(rg_resource_handle));
        ALWAYS_ASSERT(tex != nullptr);
        tex_mip_levels = std::max(1u, tex->desc().mip_levels);
        tex_array_layers = std::max(1u, tex->desc().array_length);
      } else if (use.type == RGResourceType::Texture) {
        ALWAYS_ASSERT(rg_resource_handle.idx < tex_att_infos_.size());
        tex_mip_levels = std::max(1u, tex_att_infos_[rg_resource_handle.idx].mip_levels);
        tex_array_layers = std::max(1u, tex_att_infos_[rg_resource_handle.idx].array_layers);
      }

      const uint32_t mip_run = rg_resolved_mip_level_count(use.subresource, tex_mip_levels);
      const uint32_t slice_run = rg_resolved_slice_count(use.subresource, tex_array_layers);
      for (uint32_t m = 0; m < mip_run; ++m) {
        const uint32_t mip_i = use.subresource.base_mip + m;
        for (uint32_t s = 0; s < slice_run; ++s) {
          const uint32_t slice_i = use.subresource.base_slice + s;
          const RgSubresourceStateKey key{.type = rg_resource_handle.type,
                                          .idx = rg_resource_handle.idx,
                                          .mip = static_cast<int32_t>(mip_i),
                                          .slice = static_cast<int32_t>(slice_i)};
          merge_pass_use_key(key, PassUse{.resource = rg_resource_handle,
                                          .debug_id = use.id,
                                          .access = use.acc,
                                          .stage = use.stage,
                                          .type = use.type,
                                          .is_swapchain_write = use.is_swapchain_write,
                                          .mip = static_cast<int32_t>(mip_i),
                                          .slice = static_cast<int32_t>(slice_i)});
        }
      }
    };

    for (const auto& write_use : pass.get_external_writes()) {
      accumulate_use(write_use);
    }
    for (const auto& write_use : pass.get_internal_writes()) {
      accumulate_use(write_use);
    }
    for (const auto& read_use : pass.get_external_reads()) {
      accumulate_use(read_use);
    }
    for (const auto& read_use : pass.get_internal_reads()) {
      accumulate_use(read_use);
    }

    for (const auto& [key, use] : pass_uses) {
      (void)key;
      auto& resource_state = get_resource_state(use.resource, use.mip, use.slice);
      RGState desired{};
      desired.access = use.access;
      desired.stage =
          use.stage == rhi::PipelineStage::None ? rhi::PipelineStage::TopOfPipe : use.stage;
      const bool is_buffer =
          use.type == RGResourceType::Buffer || use.type == RGResourceType::ExternalBuffer;
      if (is_buffer) {
        desired.layout = rhi::ResourceLayout::Undefined;
      } else {
        desired.layout = layout_from_access(use.access, pass.type(), use.is_swapchain_write);
      }

      const bool is_write = has_write_access(desired.access);
      const bool layout_change = !is_buffer && resource_state.layout != desired.layout;

      if (is_write) {
        const rhi::PipelineStage read_stages = read_stage_mask(resource_state);
        const bool has_reads = read_stages != rhi::PipelineStage::None;
        const bool need_barrier = layout_change || resource_state.has_write || has_reads;

        if (need_barrier) {
          RGState src{};
          if (resource_state.has_write) {
            src.stage = resource_state.last_write_stage;
            if (has_reads) {
              src.stage = flag_or(src.stage, read_stages);
            }
            src.access = resource_state.to_flush_access;
          } else if (has_reads) {
            src.stage = read_stages;
            src.access = rhi::AccessFlags::None;
          } else {
            // No prior writes/reads. If we need a layout transition, sync within the destination
            // stage to avoid swapchain acquire hazards.
            src.stage = layout_change ? desired.stage : rhi::PipelineStage::TopOfPipe;
            src.access = rhi::AccessFlags::None;
          }
          src.layout = resource_state.layout;

          barriers.emplace_back(BarrierInfo{
              .resource = use.resource,
              .src_state = src,
              .dst_state = desired,
              .debug_id = use.debug_id,
              .is_swapchain_write = use.is_swapchain_write,
              .subresource = is_buffer
                                 ? RgSubresourceRange{0, 1u, 0u, 1u}
                                 : RgSubresourceRange::mip_layers(
                                       use.mip >= 0 ? static_cast<uint32_t>(use.mip) : 0u, 1u,
                                       use.slice >= 0 ? static_cast<uint32_t>(use.slice) : 0u, 1u),
          });
        }

        resource_state.has_write = true;
        resource_state.last_write_stage = desired.stage;
        resource_state.to_flush_access = desired.access;
        if (!is_buffer) {
          resource_state.layout = desired.layout;
        }
        clear_invalidated(resource_state);
      } else {
        const bool need_invalidate =
            need_invalidate_read(resource_state, desired.stage, desired.access);
        const bool need_barrier = layout_change ||
                                  resource_state.to_flush_access != rhi::AccessFlags::None ||
                                  need_invalidate;

        if (need_barrier) {
          RGState src{};
          if (resource_state.has_write) {
            src.stage = resource_state.last_write_stage;
            src.access = resource_state.to_flush_access;
          } else {
            src.stage = rhi::PipelineStage::TopOfPipe;
            src.access = rhi::AccessFlags::None;
          }
          src.layout = resource_state.layout;

          barriers.emplace_back(BarrierInfo{
              .resource = use.resource,
              .src_state = src,
              .dst_state = desired,
              .debug_id = use.debug_id,
              .is_swapchain_write = use.is_swapchain_write,
              .subresource = is_buffer
                                 ? RgSubresourceRange{}
                                 : RgSubresourceRange::mip_layers(
                                       use.mip >= 0 ? static_cast<uint32_t>(use.mip) : 0u, 1u,
                                       use.slice >= 0 ? static_cast<uint32_t>(use.slice) : 0u, 1u),
          });

          if (resource_state.to_flush_access != rhi::AccessFlags::None || layout_change) {
            clear_invalidated(resource_state);
          }
          resource_state.to_flush_access = rhi::AccessFlags::None;
          if (!is_buffer) {
            resource_state.layout = desired.layout;
          }
        }

        for_each_set_stage_bits(desired.stage, [&](uint32_t bit) {
          resource_state.invalidated_in_stage[bit] =
              flag_or(resource_state.invalidated_in_stage[bit], desired.access);
        });
      }
    }

    coalesce_pass_texture_barriers(barriers);

    if (verbose) {
      static constexpr const char* kCyan = "\033[36m";
      static constexpr const char* kPurple = "\033[35m";
      static constexpr const char* kGreen = "\033[32m";
      static constexpr const char* kYellow = "\033[33m";
      static constexpr const char* kDim = "\033[2m";
      static constexpr const char* kReset = "\033[0m";

      barrier_total_count += barriers.size();
      LINFO("{}[exec {:>3} | pass_i {:>3}]{} {} {} — {}{} barrier(s){}", kGreen,
            barrier_pass_exec_ord, pass_i, kReset, rg_fmt::to_string(pass.type()), pass.get_name(),
            kYellow, barriers.size(), kReset);

      for (size_t bi = 0; bi < barriers.size(); ++bi) {
        const auto& barrier = barriers[bi];
        const auto dbg_name = debug_name(barrier.debug_id);
        const bool layout_mismatch = barrier.src_state.layout != barrier.dst_state.layout;
        const bool src_write = has_write_access(barrier.src_state.access);
        const bool dst_write = has_write_access(barrier.dst_state.access);
        std::string why;
        if (layout_mismatch) {
          why += "layout_mismatch ";
        }
        if (src_write) {
          why += "src_write ";
        }
        if (dst_write) {
          why += "dst_write ";
        }
        while (!why.empty() && why.back() == ' ') {
          why.pop_back();
        }

        LINFO("{}  [{}/{}] {}{}{}{}", kDim, bi + 1, barriers.size(), kReset, kPurple,
              dbg_name.size() ? dbg_name : "<unnamed>", kReset);
        LINFO("\t{}physical: {} idx={}", kDim, to_string(barrier.resource.type),
              barrier.resource.idx);
        if (barrier.debug_id.is_valid()) {
          LINFO("\t{}debug_id: idx={} type={} ver={}", kDim, barrier.debug_id.idx,
                to_string(barrier.debug_id.type), barrier.debug_id.version);
        }
        LINFO(
            "\t{}subresource: base_mip={} mip_count={} base_slice={} slice_count={} "
            "swapchain_write={}",
            kDim, barrier.subresource.base_mip, barrier.subresource.mip_count,
            barrier.subresource.base_slice, barrier.subresource.slice_count,
            barrier.is_swapchain_write);
        LINFO("\t{}why_barrier: [{}]", kDim, why.empty() ? "?" : why);
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "SRC_ACCESS:", kReset),
              rg_fmt::to_string(barrier.src_state.access));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "DST_ACCESS:", kReset),
              rg_fmt::to_string(barrier.dst_state.access));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "SRC_STAGE:", kReset),
              rg_fmt::to_string(barrier.src_state.stage));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "DST_STAGE:", kReset),
              rg_fmt::to_string(barrier.dst_state.stage));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "SRC_LAYOUT:", kReset),
              rg_fmt::to_string(barrier.src_state.layout));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "DST_LAYOUT:", kReset),
              rg_fmt::to_string(barrier.dst_state.layout));
      }
      if (!barriers.empty()) {
        LINFO("");
      }
    }
    ++barrier_pass_exec_ord;
  }

  if (verbose) {
    LINFO("Barrier summary: {} barrier record(s) in {} pass(es) (execution order).",
          barrier_total_count, barrier_pass_exec_ord);
  }
}

void RenderGraph::find_deps_recursive(uint32_t pass_i) {
  ALWAYS_ASSERT(pass_i < pass_dep_dfs_state_.size());
  const uint8_t st = pass_dep_dfs_state_[pass_i];
  if (st == kPassDepDfsGray) {
    LERROR(
        "RenderGraph: cycle in pass dependency graph (re-entered pass '{}' while resolving "
        "producer chain). Current DFS stack:",
        passes_[pass_i].get_name());
    for (uint32_t p : pass_dep_dfs_path_) {
      LERROR("  {}", passes_[p].get_name());
    }
    LERROR("  -> '{}' (closes the cycle)", passes_[pass_i].get_name());
    ALWAYS_ASSERT(0 && "RenderGraph: cycle in pass dependency graph");
  }
  if (st == kPassDepDfsBlack) {
    return;
  }

  pass_dep_dfs_state_[pass_i] = kPassDepDfsGray;
  pass_dep_dfs_path_.push_back(pass_i);
  auto& pass = passes_[pass_i];

  for (const auto& external_read : pass.get_external_reads()) {
    auto writer_pass_it = resource_use_id_to_writer_pass_idx_.find(external_read.id);
    if (writer_pass_it == resource_use_id_to_writer_pass_idx_.end()) {
      LERROR(
          "RenderGraph: external read has no producer pass (no write registered for this resource "
          "id). reader_pass='{}' resource='{}' type={} — add a pass that writes this id/version "
          "before readers, or fix a stale RGResourceId (wrong #version).",
          pass.get_name(), debug_name(external_read.id), to_string(external_read.type));
      ASSERT(0);
    }
    uint32_t write_pass_i = writer_pass_it->second;
    intermed_pass_stack_.emplace_back(write_pass_i);
    pass_dependencies_[pass_i].insert(write_pass_i);
    find_deps_recursive(write_pass_i);
  }

  for (const auto& read : pass.get_internal_reads()) {
    auto writer_pass_it = resource_use_id_to_writer_pass_idx_.find(read.id);
    if (writer_pass_it == resource_use_id_to_writer_pass_idx_.end()) {
      LERROR(
          "RenderGraph: internal read has no producer pass. reader_pass='{}' resource='{}' type={} "
          "— ensure an earlier pass writes this id/version.",
          pass.get_name(), debug_name(read.id), to_string(read.type));
      ASSERT(0);
    }
    uint32_t write_pass_i = writer_pass_it->second;
    intermed_pass_stack_.emplace_back(write_pass_i);
    pass_dependencies_[pass_i].insert(write_pass_i);
    find_deps_recursive(write_pass_i);
  }

  pass_dep_dfs_path_.pop_back();
  pass_dep_dfs_state_[pass_i] = kPassDepDfsBlack;
}

void RenderGraph::dfs(const std::vector<std::unordered_set<uint32_t>>& pass_dependencies,
                      const std::vector<Pass>& passes,
                      std::unordered_set<uint32_t>& curr_stack_passes,
                      std::unordered_set<uint32_t>& visited_passes,
                      std::vector<uint32_t>& pass_stack, uint32_t pass) {
  if (curr_stack_passes.contains(pass)) {
    LERROR("RenderGraph: cycle in pass dependency graph during topological sort (pass '{}').",
           passes[pass].get_name());
    ALWAYS_ASSERT(0 && "RenderGraph: cycle in pass dependency graph");
  }
  if (visited_passes.contains(pass)) return;

  curr_stack_passes.insert(pass);

  for (const auto& dep : pass_dependencies[pass]) {
    dfs(pass_dependencies, passes, curr_stack_passes, visited_passes, pass_stack, dep);
  }

  curr_stack_passes.erase(pass);
  visited_passes.insert(pass);
  pass_stack.push_back(pass);
}
void RenderGraph::bake_validate_() {
  ZoneScopedN("RG bake: validate");
  ALWAYS_ASSERT(!pass_stack_.empty());
  ALWAYS_ASSERT(tex_att_handles_.size() == tex_att_infos_.size());
  ALWAYS_ASSERT(buffer_handles_.size() == buffer_infos_.size());
  ALWAYS_ASSERT(defer_pool_handles_by_slot_.size() == buffer_infos_.size());
  ALWAYS_ASSERT(pass_barrier_infos_.size() >= passes_.size());
  for (uint32_t pass_i : pass_stack_) {
    ALWAYS_ASSERT(pass_i < passes_.size());
    ALWAYS_ASSERT(pass_i < pass_barrier_infos_.size());
    for (const auto& barrier : pass_barrier_infos_[pass_i]) {
      switch (barrier.resource.type) {
        case RGResourceType::Texture:
          ALWAYS_ASSERT(barrier.resource.idx < tex_att_handles_.size());
          break;
        case RGResourceType::Buffer:
          ALWAYS_ASSERT(barrier.resource.idx < buffer_handles_.size());
          break;
        case RGResourceType::ExternalTexture:
          ALWAYS_ASSERT(barrier.resource.idx < external_textures_.size());
          break;
        case RGResourceType::ExternalBuffer:
          ALWAYS_ASSERT(barrier.resource.idx < external_buffers_.size());
          break;
      }
      if (barrier.debug_id.is_valid()) {
        ALWAYS_ASSERT(barrier.debug_id.idx < resources_.size());
      }

      if (is_texture(barrier.resource.type)) {
        uint32_t max_mips = 1;
        uint32_t max_layers = 1;
        if (barrier.resource.type == RGResourceType::ExternalTexture) {
          rhi::Texture* t = device_->get_tex(external_textures_[barrier.resource.idx]);
          ALWAYS_ASSERT(t);
          max_mips = std::max(1u, t->desc().mip_levels);
          max_layers = std::max(1u, t->desc().array_length);
        } else {
          ALWAYS_ASSERT(barrier.resource.idx < tex_att_infos_.size());
          max_mips = std::max(1u, tex_att_infos_[barrier.resource.idx].mip_levels);
          max_layers = std::max(1u, tex_att_infos_[barrier.resource.idx].array_layers);
        }
        ALWAYS_ASSERT(barrier.subresource.mip_count != RgSubresourceRange::k_all_mips);
        ALWAYS_ASSERT(barrier.subresource.slice_count != RgSubresourceRange::k_all_slices);
        ALWAYS_ASSERT(barrier.subresource.base_mip < max_mips);
        ALWAYS_ASSERT(barrier.subresource.base_slice < max_layers);
        ALWAYS_ASSERT(barrier.subresource.base_mip + barrier.subresource.mip_count <= max_mips);
        ALWAYS_ASSERT(barrier.subresource.base_slice + barrier.subresource.slice_count <=
                      max_layers);
      }
    }
  }
}

bool RenderGraph::run_barrier_coalesce_self_tests() {
  using B = BarrierInfo;
  RGResourceHandle h{.idx = 0, .type = RGResourceType::ExternalTexture};
  const RGState src{.access = rhi::AccessFlags::ShaderWrite,
                    .stage = rhi::PipelineStage::ComputeShader,
                    .layout = rhi::ResourceLayout::ComputeRW};
  const RGState dst{.access = rhi::AccessFlags::ShaderSampledRead,
                    .stage = rhi::PipelineStage::FragmentShader,
                    .layout = rhi::ResourceLayout::ShaderReadOnly};

  std::vector<B> merge_three;
  for (uint32_t m : {0u, 1u, 2u}) {
    merge_three.push_back(B{.resource = h,
                            .src_state = src,
                            .dst_state = dst,
                            .debug_id = {},
                            .is_swapchain_write = false,
                            .subresource = RgSubresourceRange::single_mip(m)});
  }
  coalesce_pass_texture_barriers(merge_three);
  if (merge_three.size() != 1 || merge_three[0].subresource.base_mip != 0u ||
      merge_three[0].subresource.mip_count != 3u || merge_three[0].subresource.slice_count != 1u) {
    return false;
  }

  const RGState other_src{.access = rhi::AccessFlags::ShaderSampledRead,
                          .stage = rhi::PipelineStage::FragmentShader,
                          .layout = rhi::ResourceLayout::ShaderReadOnly};
  std::vector<B> no_merge = {B{.resource = h,
                               .src_state = src,
                               .dst_state = dst,
                               .debug_id = {},
                               .is_swapchain_write = false,
                               .subresource = RgSubresourceRange::single_mip(0u)},
                             B{.resource = h,
                               .src_state = other_src,
                               .dst_state = dst,
                               .debug_id = {},
                               .is_swapchain_write = false,
                               .subresource = RgSubresourceRange::single_mip(1u)}};
  coalesce_pass_texture_barriers(no_merge);
  return no_merge.size() == 2;
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
