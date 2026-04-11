#include "RenderGraph.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <tracy/Tracy.hpp>
#include <utility>

#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "small_vector/small_vector.hpp"

namespace TENG_NAMESPACE {

namespace gfx {

namespace {

// Pass→producer dependency walk in bake() (white/gray/black DFS).
constexpr uint8_t kPassDepDfsWhite = 0;
constexpr uint8_t kPassDepDfsGray = 1;
constexpr uint8_t kPassDepDfsBlack = 2;

const char* to_string(RGPassType type) {
  switch (type) {
    case RGPassType::Compute:
      return "Compute";
    case RGPassType::Graphics:
      return "Graphics";
    case RGPassType::Transfer:
      return "Transfer";
    case RGPassType::None:
      return "None";
  }
}

std::string to_string(rhi::PipelineStage stage) {
  std::string result;
  if (has_flag(stage, rhi::PipelineStage::TopOfPipe)) {
    result += "PipelineStage_TopOfPipe | ";
  }
  if (has_flag(stage, rhi::PipelineStage::DrawIndirect)) {
    result += "PipelineStage_DrawIndirect | ";
  }
  if (has_flag(stage, rhi::PipelineStage::VertexInput)) {
    result += "PipelineStage_VertexInput | ";
  }
  if (has_flag(stage, rhi::PipelineStage::VertexShader)) {
    result += "PipelineStage_VertexShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::TaskShader)) {
    result += "PipelineStage_TaskShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::MeshShader)) {
    result += "PipelineStage_MeshShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::FragmentShader)) {
    result += "PipelineStage_FragmentShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::EarlyFragmentTests)) {
    result += "PipelineStage_EarlyFragmentTests | ";
  }
  if (has_flag(stage, rhi::PipelineStage::LateFragmentTests)) {
    result += "PipelineStage_LateFragmentTests | ";
  }
  if (has_flag(stage, rhi::PipelineStage::ColorAttachmentOutput)) {
    result += "PipelineStage_ColorAttachmentOutput | ";
  }
  if (has_flag(stage, rhi::PipelineStage::ComputeShader)) {
    result += "PipelineStage_ComputeShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllTransfer)) {
    result += "PipelineStage_AllTransfer | ";
  }
  if (has_flag(stage, rhi::PipelineStage::BottomOfPipe)) {
    result += "PipelineStage_BottomOfPipe | ";
  }
  if (has_flag(stage, rhi::PipelineStage::Host)) {
    result += "PipelineStage_Host | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllGraphics)) {
    result += "PipelineStage_AllGraphics | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllCommands)) {
    result += "PipelineStage_AllCommands | ";
  }
  if (result.size()) {
    result = result.substr(0, result.size() - 3);
  }
  return result;
}

std::string to_string(rhi::AccessFlags access) {
  std::string result;
  if (has_flag(access, rhi::AccessFlags::IndirectCommandRead)) {
    result += "IndirectCommandRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::IndexRead)) {
    result += "IndexRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::VertexAttributeRead)) {
    result += "VertexAttributeRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::UniformRead)) {
    result += "UniformRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::InputAttachmentRead)) {
    result += "InputAttachmentRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderRead)) {
    result += "ShaderRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderWrite)) {
    result += "ShaderWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentRead)) {
    result += "ColorAttachmentRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentWrite)) {
    result += "ColorAttachmentWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilRead)) {
    result += "DepthStencilRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilWrite)) {
    result += "DepthStencilWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::TransferRead)) {
    result += "TransferRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::TransferWrite)) {
    result += "TransferWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::MemoryRead)) {
    result += "MemoryRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::MemoryWrite)) {
    result += "MemoryWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderSampledRead)) {
    result += "ShaderSampledRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageRead)) {
    result += "ShaderStorageRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageWrite)) {
    result += "ShaderStorageWrite | ";
  }
  if (result.size()) {
    result = result.substr(0, result.size() - 3);
  }
  return result;
}

std::string to_string(rhi::ResourceLayout layout) {
  switch (layout) {
    case rhi::ResourceLayout::Undefined:
      return "Layout_Undefined";
    case rhi::ResourceLayout::General:
      return "Layout_General";
    case rhi::ResourceLayout::ShaderReadOnly:
      return "Layout_ShaderReadOnly";
    case rhi::ResourceLayout::ColorAttachment:
      return "Layout_ColorAttachment";
    case rhi::ResourceLayout::DepthStencil:
      return "Layout_DepthStencil";
    case rhi::ResourceLayout::TransferSrc:
      return "Layout_TransferSrc";
    case rhi::ResourceLayout::TransferDst:
      return "Layout_TransferDst";
    case rhi::ResourceLayout::Present:
      return "Layout_Present";
    case rhi::ResourceLayout::ComputeRW:
      return "Layout_ComputeRW";
    case rhi::ResourceLayout::InputAttachment:
      return "Layout_InputAttachment";
  }
  return "Layout_Unknown";
}

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

inline void for_each_stage_bit(rhi::PipelineStage stage, const auto& fn) {
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

rhi::BufferUsage buffer_desc_usage_for_bake(rhi::AccessFlags accumulated) {
  using B = rhi::BufferUsage;
  const auto u = buffer_usage_from_accumulated_access(accumulated);
  if (u == B::None) {
    return B::Storage | B::Indirect;
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

inline bool need_invalidate_read(const SubresourceState& state, rhi::PipelineStage stages,
                                 rhi::AccessFlags access) {
  bool need_invalidate = false;
  for_each_stage_bit(stages, [&](uint32_t bit) {
    const auto inv = state.invalidated_in_stage[bit];
    const uint64_t missing = static_cast<uint64_t>(access) & ~static_cast<uint64_t>(inv);
    if (missing != 0) {
      need_invalidate = true;
    }
  });
  return need_invalidate;
}

}  // namespace

const char* to_string(RGResourceType type) {
  switch (type) {
    case RGResourceType::Texture:
      return "Texture";
    case RGResourceType::Buffer:
      return "Buffer";
    case RGResourceType::ExternalTexture:
      return "ExternalTexture";
    case RGResourceType::ExternalBuffer:
      return "ExternalBuffer";
  }
}

RenderGraph::Pass::Pass(NameId name_id, RenderGraph* rg, uint32_t pass_i, RGPassType type)
    : rg_(rg), pass_i_(pass_i), name_id_(name_id), type_(type) {}

RenderGraph::Pass& RenderGraph::add_pass(std::string_view name, RGPassType type) {
  auto idx = static_cast<uint32_t>(passes_.size());
  const NameId name_id = intern_name(name);
  passes_.emplace_back(name_id, this, idx, type);
  return passes_.back();
}

void RenderGraph::execute() {
  rhi::CmdEncoder* enc = device_->begin_cmd_encoder();
  execute(enc);
  enc->end_encoding();
}

void RenderGraph::execute(rhi::CmdEncoder* enc) {
  ZoneScoped;
  gch::small_vector<rhi::GPUBarrier, 8> post_pass_barriers;
  for (auto pass_i : pass_stack_) {
    post_pass_barriers.clear();
    auto& pass = passes_[pass_i];
    ZoneScopedN("Execute Pass");
    for (auto& barrier : pass_barrier_infos_[pass_i]) {
      if (barrier.resource.type == RGResourceType::Buffer ||
          barrier.resource.type == RGResourceType::ExternalBuffer) {
        auto buf_handle = barrier.resource.type == RGResourceType::Buffer
                              ? get_buf(barrier.resource)
                              : get_external_buf(barrier.resource);
        enc->barrier(buf_handle, barrier.src_state.stage, barrier.src_state.access,
                     barrier.dst_state.stage, barrier.dst_state.access);
      } else {
        auto tex_handle = barrier.resource.type == RGResourceType::Texture
                              ? get_att_img(barrier.resource)
                              : get_external_tex(barrier.resource);

        if (barrier.is_swapchain_write) {
          tex_handle = pass.swapchain_write_->get_current_texture();
          ASSERT(pass.swapchain_write_);
          post_pass_barriers.emplace_back(rhi::GPUBarrier::tex_barrier(
              pass.swapchain_write_->get_current_texture(), rhi::ResourceState::ColorWrite,
              rhi::ResourceState::SwapchainPresent));
        }
        enc->barrier(tex_handle, barrier.src_state.stage, barrier.src_state.access,
                     barrier.dst_state.stage, barrier.dst_state.access, barrier.src_state.layout,
                     barrier.dst_state.layout, barrier.subresource_mip, barrier.subresource_slice);
      }
    }

    enc->set_debug_name(pass.get_name().c_str());
    {
      ZoneScopedN("Execute Fn");
      pass.get_execute_fn()(enc);
    }
    for (auto& b : post_pass_barriers) {
      enc->barrier(&b);
    }
  }

  {
    passes_.clear();
    for (auto& b : pass_barrier_infos_) {
      b.clear();
    }
    external_texture_count_ = 0;
  }

  for (size_t i = 0; i < tex_att_infos_.size(); i++) {
    const rhi::TextureUsage usage = device_->get_tex(tex_att_handles_[i])->desc().usage;
    free_atts_[TexPoolKey{tex_att_infos_[i], usage}].emplace_back(tex_att_handles_[i]);
  }
  // history bufs are now gtg
  for (auto& [key, bufs] : history_free_bufs_) {
    for (auto& buf : bufs) {
      const rhi::BufferUsage usage = device_->get_buf(buf)->desc().usage;
      free_bufs_[BufPoolKey{key.info, usage}].emplace_back(buf);
    }
  }
  history_free_bufs_.clear();

  for (size_t i = 0; i < buffer_infos_.size(); i++) {
    if (history_buffer_handles_[i].is_valid()) {
      const rhi::BufferUsage usage = device_->get_buf(history_buffer_handles_[i])->desc().usage;
      history_free_bufs_[BufPoolKey{buffer_infos_[i], usage}].emplace_back(
          history_buffer_handles_[i]);
    } else {
      const rhi::BufferUsage usage = device_->get_buf(buffer_handles_[i])->desc().usage;
      free_bufs_[BufPoolKey{buffer_infos_[i], usage}].emplace_back(buffer_handles_[i]);
    }
  }
  tex_att_infos_.clear();
  buffer_infos_.clear();
  resources_.clear();
  resource_use_id_to_writer_pass_idx_.clear();
  external_read_ids_.clear();
  external_buffers_.clear();
  external_textures_.clear();
  external_tex_handle_to_id_.clear();
  external_buf_handle_to_id_.clear();
  rg_id_to_external_texture_.clear();
  rg_id_to_external_buffer_.clear();
  curr_submitted_swapchain_textures_.clear();
  external_initial_states_.clear();
}

void RenderGraph::bake(glm::uvec2 fb_size, bool verbose) {
  ZoneScoped;
  if (verbose) {
    LINFO("//////////// Baking Render Graph ////////////");
  }
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
            LINFO("{:<50}\t{:<50}\t{:<50}", debug_name(u.id), to_string(u.acc), to_string(u.stage));
          }
        }
      }
      LINFO("");
    }
  }

  std::vector<rhi::AccessFlags> tex_physical_access(tex_att_infos_.size(), rhi::AccessFlags::None);
  std::vector<rhi::AccessFlags> buf_physical_access(buffer_infos_.size(), rhi::AccessFlags::None);
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
    history_buffer_handles_.clear();
    for (size_t i = 0; i < buffer_infos_.size(); ++i) {
      const auto& binfo = buffer_infos_[i];
      const rhi::BufferUsage derived_usage = buffer_desc_usage_for_bake(buf_physical_access[i]);

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
        history_buffer_handles_.emplace_back(actual_buf_handle);
      } else {
        history_buffer_handles_.emplace_back();
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

  std::unordered_map<RgSubresourceStateKey, SubresourceState, RgSubresourceStateKeyHash>
      subresource_states;

  auto get_resource_state = [&](RGResourceHandle handle, int32_t mip,
                                int32_t slice) -> SubresourceState& {
    RgSubresourceStateKey key{.type = handle.type, .idx = handle.idx, .mip = mip, .slice = slice};
    if (auto it = subresource_states.find(key); it != subresource_states.end()) {
      return it->second;
    }
    SubresourceState init{};
    if (auto init_it = external_initial_states_.find(handle.to64());
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

    auto accumulate_use = [&](const RenderGraph::Pass::NameAndAccess& use) {
      const auto rg_resource_handle = get_physical_handle(use.id);
      RgSubresourceStateKey key{.type = rg_resource_handle.type,
                                .idx = rg_resource_handle.idx,
                                .mip = use.subresource_mip,
                                .slice = use.subresource_slice};
      auto it = pass_uses.find(key);
      if (it == pass_uses.end()) {
        pass_uses.emplace(key, PassUse{.resource = rg_resource_handle,
                                       .debug_id = use.id,
                                       .access = use.acc,
                                       .stage = use.stage,
                                       .type = use.type,
                                       .is_swapchain_write = use.is_swapchain_write,
                                       .mip = use.subresource_mip,
                                       .slice = use.subresource_slice});
        return;
      }
      auto& entry = it->second;
      entry.access = flag_or(entry.access, use.acc);
      entry.stage = flag_or(entry.stage, use.stage);
      entry.is_swapchain_write |= use.is_swapchain_write;
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

    // Whole-texture uses (mip=-1) do not share subresource state with per-mip passes (e.g.
    // depth_reduce). Expand to explicit mips so barriers match what shaders actually access.
    {
      std::vector<std::pair<RgSubresourceStateKey, PassUse>> to_expand;
      to_expand.reserve(pass_uses.size());
      for (const auto& [key, use] : pass_uses) {
        if (use.mip != -1 || use.slice != -1) {
          continue;
        }
        if (use.type != RGResourceType::Texture && use.type != RGResourceType::ExternalTexture) {
          continue;
        }
        to_expand.emplace_back(key, use);
      }
      for (const auto& [old_key, use] : to_expand) {
        pass_uses.erase(old_key);
        uint32_t mip_levels = 1;
        if (use.resource.type == RGResourceType::ExternalTexture) {
          rhi::Texture* tex = device_->get_tex(get_external_tex(use.resource));
          ALWAYS_ASSERT(tex != nullptr);
          mip_levels = std::max(1u, tex->desc().mip_levels);
        } else if (use.resource.type == RGResourceType::Texture) {
          ALWAYS_ASSERT(use.resource.idx < tex_att_infos_.size());
          mip_levels = std::max(1u, tex_att_infos_[use.resource.idx].mip_levels);
        } else {
          continue;
        }
        for (uint32_t m = 0; m < mip_levels; ++m) {
          const RgSubresourceStateKey new_key{.type = use.resource.type,
                                              .idx = use.resource.idx,
                                              .mip = static_cast<int32_t>(m),
                                              .slice = -1};
          auto it = pass_uses.find(new_key);
          if (it == pass_uses.end()) {
            pass_uses.emplace(new_key, PassUse{.resource = use.resource,
                                               .debug_id = use.debug_id,
                                               .access = use.access,
                                               .stage = use.stage,
                                               .type = use.type,
                                               .is_swapchain_write = use.is_swapchain_write,
                                               .mip = static_cast<int32_t>(m),
                                               .slice = -1});
          } else {
            auto& entry = it->second;
            entry.access = flag_or(entry.access, use.access);
            entry.stage = flag_or(entry.stage, use.stage);
            entry.is_swapchain_write |= use.is_swapchain_write;
          }
        }
      }
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
              .subresource_mip = use.mip,
              .subresource_slice = use.slice,
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
              .subresource_mip = use.mip,
              .subresource_slice = use.slice,
          });

          if (resource_state.to_flush_access != rhi::AccessFlags::None || layout_change) {
            clear_invalidated(resource_state);
          }
          resource_state.to_flush_access = rhi::AccessFlags::None;
          if (!is_buffer) {
            resource_state.layout = desired.layout;
          }
        } else if (!is_buffer && resource_state.layout == rhi::ResourceLayout::Undefined) {
          resource_state.layout = desired.layout;
        }

        for_each_stage_bit(desired.stage, [&](uint32_t bit) {
          resource_state.invalidated_in_stage[bit] =
              flag_or(resource_state.invalidated_in_stage[bit], desired.access);
        });
      }
    }

    if (verbose) {
      static constexpr const char* kCyan = "\033[36m";
      static constexpr const char* kPurple = "\033[35m";
      static constexpr const char* kGreen = "\033[32m";
      static constexpr const char* kYellow = "\033[33m";
      static constexpr const char* kDim = "\033[2m";
      static constexpr const char* kReset = "\033[0m";

      barrier_total_count += barriers.size();
      LINFO("{}[exec {:>3} | pass_i {:>3}]{} {} {} — {}{} barrier(s){}", kGreen,
            barrier_pass_exec_ord, pass_i, kReset, to_string(pass.type()), pass.get_name(), kYellow,
            barriers.size(), kReset);

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
        LINFO("\t{}subresource: mip={} slice={} swapchain_write={}", kDim, barrier.subresource_mip,
              barrier.subresource_slice, barrier.is_swapchain_write);
        LINFO("\t{}why_barrier: [{}]", kDim, why.empty() ? "?" : why);
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "SRC_ACCESS:", kReset),
              to_string(barrier.src_state.access));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "DST_ACCESS:", kReset),
              to_string(barrier.dst_state.access));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "SRC_STAGE:", kReset),
              to_string(barrier.src_state.stage));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "DST_STAGE:", kReset),
              to_string(barrier.dst_state.stage));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "SRC_LAYOUT:", kReset),
              to_string(barrier.src_state.layout));
        LINFO("\t{} {}", std::format("{}{:<14}{}", kCyan, "DST_LAYOUT:", kReset),
              to_string(barrier.dst_state.layout));
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

  if (verbose) {
    LINFO("//////////// Done Baking Render Graph ////////////");
  }
}

RenderGraph::NameId RenderGraph::intern_name(std::string_view name) {
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return it->second;
  }
  const auto id = static_cast<NameId>(id_to_name_.size());
  id_to_name_.emplace_back(name);
  name_to_id_.emplace(id_to_name_.back(), id);
  return id;
}

const std::string& RenderGraph::debug_name(NameId name) const {
  static const std::string kInvalidName = "<invalid>";
  if (name == kInvalidNameId || name >= id_to_name_.size()) {
    return kInvalidName;
  }
  return id_to_name_[name];
}

std::string RenderGraph::debug_name(RGResourceId id) const {
  ASSERT(id.is_valid());
  if (!id.is_valid() || id.idx >= resources_.size()) {
    return "<invalid>";
  }
  const auto& rec = resources_[id.idx];
  const auto& base = debug_name(rec.debug_name);
  if (id.version == 0) {
    return base;
  }
  return base + "#" + std::to_string(id.version);
}

void RenderGraph::init(rhi::Device* device) {
  device_ = device;
  passes_.reserve(200);
}

RGResourceId RenderGraph::create_texture(const AttachmentInfo& att_info,
                                         std::string_view debug_name) {
  ASSERT((att_info.is_swapchain_tex || att_info.format != rhi::TextureFormat::Undefined));
  RGResourceHandle handle = {.idx = static_cast<uint32_t>(tex_att_infos_.size()),
                             .type = RGResourceType::Texture};
  tex_att_infos_.emplace_back(att_info);

  auto record = create_resource_record(RGResourceType::Texture, handle.idx, debug_name);
  resources_.push_back(record);
  return RGResourceId{.idx = static_cast<uint32_t>(resources_.size() - 1),
                      .type = RGResourceType::Texture,
                      .version = 0};
}

RGResourceId RenderGraph::create_buffer(const BufferInfo& buf_info, std::string_view debug_name) {
  RGResourceHandle handle = {.idx = static_cast<uint32_t>(buffer_infos_.size()),
                             .type = RGResourceType::Buffer};
  buffer_infos_.emplace_back(buf_info);

  auto record = create_resource_record(RGResourceType::Buffer, handle.idx, debug_name);
  resources_.push_back(record);
  return RGResourceId{.idx = static_cast<uint32_t>(resources_.size() - 1),
                      .type = RGResourceType::Buffer,
                      .version = 0};
}

RGResourceId RenderGraph::import_external_texture(rhi::TextureHandle tex_handle,
                                                  std::string_view debug_name) {
  return import_external_texture(tex_handle, RGState{}, debug_name);
}

RGResourceId RenderGraph::import_external_texture(rhi::TextureHandle tex_handle,
                                                  const RGState& initial,
                                                  std::string_view debug_name) {
  const auto key = tex_handle.to64();
  if (auto it = external_tex_handle_to_id_.find(key); it != external_tex_handle_to_id_.end()) {
    if (!debug_name.empty()) {
      auto& rec = resources_[it->second.idx];
      if (rec.debug_name == kInvalidNameId) {
        rec.debug_name = intern_name(debug_name);
      }
    }
    const auto& rec = resources_[it->second.idx];
    RGResourceId id{.idx = it->second.idx,
                    .type = RGResourceType::ExternalTexture,
                    .version = rec.latest_version};
    external_initial_states_[get_physical_handle(id).to64()] = initial;
    rg_id_to_external_texture_[id] = tex_handle;
    return id;
  }
  auto idx = external_textures_.size();
  external_textures_.emplace_back(tex_handle);
  auto record = create_resource_record(RGResourceType::ExternalTexture, idx, debug_name);
  resources_.push_back(record);
  RGResourceId id{.idx = static_cast<uint32_t>(resources_.size() - 1),
                  .type = RGResourceType::ExternalTexture,
                  .version = 0};
  external_initial_states_[RGResourceHandle{.idx = static_cast<uint32_t>(idx),
                                            .type = RGResourceType::ExternalTexture}
                               .to64()] = initial;
  external_tex_handle_to_id_.emplace(key, id);
  rg_id_to_external_texture_[id] = tex_handle;
  return id;
}

RGResourceId RenderGraph::import_external_buffer(rhi::BufferHandle buf_handle,
                                                 std::string_view debug_name) {
  return import_external_buffer(buf_handle, RGState{}, debug_name);
}

RGResourceId RenderGraph::import_external_buffer(rhi::BufferHandle buf_handle,
                                                 const RGState& initial,
                                                 std::string_view debug_name) {
  const auto key = buf_handle.to64();
  if (auto it = external_buf_handle_to_id_.find(key); it != external_buf_handle_to_id_.end()) {
    if (!debug_name.empty()) {
      auto& rec = resources_[it->second.idx];
      if (rec.debug_name == kInvalidNameId) {
        rec.debug_name = intern_name(debug_name);
      }
    }
    const auto& rec = resources_[it->second.idx];
    RGResourceId id{.idx = it->second.idx,
                    .type = RGResourceType::ExternalBuffer,
                    .version = rec.latest_version};
    external_initial_states_[get_physical_handle(id).to64()] = initial;
    rg_id_to_external_buffer_[id] = buf_handle;
    return id;
  }
  auto idx = external_buffers_.size();
  external_buffers_.emplace_back(buf_handle);
  auto record = create_resource_record(RGResourceType::ExternalBuffer, idx, debug_name);
  resources_.push_back(record);
  RGResourceId id{.idx = static_cast<uint32_t>(resources_.size() - 1),
                  .type = RGResourceType::ExternalBuffer,
                  .version = 0};
  external_initial_states_[RGResourceHandle{.idx = static_cast<uint32_t>(idx),
                                            .type = RGResourceType::ExternalBuffer}
                               .to64()] = initial;
  external_buf_handle_to_id_.emplace(key, id);
  rg_id_to_external_buffer_[id] = buf_handle;
  return id;
}

AttachmentInfo* RenderGraph::get_tex_att_info(RGResourceId id) {
  return get_tex_att_info(get_physical_handle(id));
}

AttachmentInfo* RenderGraph::get_tex_att_info(RGResourceHandle handle) {
  ALWAYS_ASSERT(handle.type == RGResourceType::Texture);
  ALWAYS_ASSERT(handle.idx < tex_att_infos_.size());
  return &tex_att_infos_[handle.idx];
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

rhi::TextureHandle RenderGraph::get_att_img(RGResourceId tex_id) const {
  return get_att_img(get_physical_handle(tex_id));
}

rhi::TextureHandle RenderGraph::get_att_img(RGResourceHandle tex_handle) const {
  ASSERT(tex_handle.idx < tex_att_handles_.size());
  ASSERT(tex_handle.type == RGResourceType::Texture);
  return tex_att_handles_[tex_handle.idx];
}

rhi::BufferHandle RenderGraph::get_buf(RGResourceId buf_id) const {
  return get_buf(get_physical_handle(buf_id));
}

rhi::BufferHandle RenderGraph::get_buf(RGResourceHandle buf_handle) const {
  ASSERT(buf_handle.idx < buffer_handles_.size());
  ASSERT(buf_handle.type == RGResourceType::Buffer);
  return buffer_handles_[buf_handle.idx];
}

rhi::TextureHandle RenderGraph::get_external_texture(RGResourceId id) const {
  ALWAYS_ASSERT(id.type == RGResourceType::ExternalTexture);
  ALWAYS_ASSERT(id.idx < resources_.size());
  auto it = rg_id_to_external_texture_.find(id);
  ALWAYS_ASSERT(it != rg_id_to_external_texture_.end());
  return it->second;
}

rhi::BufferHandle RenderGraph::get_external_buffer(RGResourceId id) const {
  ALWAYS_ASSERT(id.type == RGResourceType::ExternalBuffer);
  ALWAYS_ASSERT(id.idx < resources_.size());
  auto it = rg_id_to_external_buffer_.find(id);
  ALWAYS_ASSERT(it != rg_id_to_external_buffer_.end());
  return it->second;
}

RGResourceHandle RenderGraph::get_physical_handle(RGResourceId id) const {
  ALWAYS_ASSERT(id.idx < resources_.size());
  const auto& rec = resources_[id.idx];
  ALWAYS_ASSERT(rec.type == id.type);
  return RGResourceHandle{.idx = rec.physical_idx, .type = rec.type};
}

void RenderGraph::register_write(RGResourceId id, RGPass& pass) {
  resource_use_id_to_writer_pass_idx_[id] = pass.get_idx();
}

RGResourceId RenderGraph::next_version(RGResourceId id) {
  ALWAYS_ASSERT(id.idx < resources_.size());
  auto& rec = resources_[id.idx];
  ALWAYS_ASSERT(rec.type == id.type);
  uint32_t next = std::max(rec.latest_version, id.version) + 1;
  rec.latest_version = next;
  return RGResourceId{.idx = id.idx, .type = id.type, .version = next};
}

RGResourceId RGPass::sample_tex(RGResourceId id) {
  rhi::PipelineStage stage{};
  if (type_ == RGPassType::Compute) {
    stage = rhi::PipelineStage::ComputeShader;
  } else if (type_ == RGPassType::Graphics) {
    stage = rhi::PipelineStage::FragmentShader;
  } else {
    ASSERT(0);
  }
  return sample_tex(id, stage);
}

RGResourceId RGPass::sample_tex(RGResourceId id, rhi::PipelineStage stage, int32_t subresource_mip,
                                int32_t subresource_slice) {
  add_read_usage(id, stage, rhi::AccessFlags::ShaderSampledRead, subresource_mip,
                 subresource_slice);
  return id;
}

RGResourceId RGPass::read_tex(RGResourceId id, rhi::PipelineStage stage, int32_t subresource_mip,
                              int32_t subresource_slice) {
  add_read_usage(id, stage, rhi::AccessFlags::ShaderStorageRead, subresource_mip,
                 subresource_slice);
  return id;
}

RGResourceId RGPass::write_tex(RGResourceId id, rhi::PipelineStage stage, int32_t subresource_mip,
                               int32_t subresource_slice) {
  rhi::AccessFlags access{};
  if (id.type == RGResourceType::ExternalTexture) {
    if (type_ == RGPassType::Transfer) {
      access = rhi::AccessFlags::TransferWrite;
    } else {
      access = rhi::AccessFlags::ShaderWrite;
    }
  } else {
    access = (type_ == RGPassType::Transfer) ? rhi::AccessFlags::TransferWrite
                                             : rhi::AccessFlags::ShaderWrite;
  }
  add_write_usage(id, stage, access, false, subresource_mip, subresource_slice);
  return id;
}

RGResourceId RGPass::write_color_output(RGResourceId id) {
  add_write_usage(id, rhi::PipelineStage::ColorAttachmentOutput,
                  rhi::AccessFlags::ColorAttachmentWrite);
  return id;
}

RGResourceId RGPass::write_depth_output(RGResourceId id) {
  add_write_usage(id,
                  rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                  rhi::AccessFlags::DepthStencilWrite);
  return id;
}

RGResourceId RGPass::rw_color_output(RGResourceId input) {
  return rw_tex(input, rhi::PipelineStage::ColorAttachmentOutput,
                rhi::AccessFlags::ColorAttachmentRead,
                rhi::AccessFlags::ColorAttachmentRead | rhi::AccessFlags::ColorAttachmentWrite);
}

RGResourceId RGPass::rw_depth_output(RGResourceId input) {
  return rw_tex(input,
                rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                rhi::AccessFlags::DepthStencilRead,
                rhi::AccessFlags::DepthStencilRead | rhi::AccessFlags::DepthStencilWrite);
}

RGResourceId RGPass::read_buf(RGResourceId id, rhi::PipelineStage stage) {
  const auto access = (id.type == RGResourceType::ExternalBuffer)
                          ? rhi::AccessFlags::ShaderRead
                          : rhi::AccessFlags::ShaderStorageRead;
  add_read_usage(id, stage, access);
  return id;
}

RGResourceId RGPass::copy_from_buf(RGResourceId id) {
  add_read_usage(id, rhi::PipelineStage::AllTransfer, rhi::AccessFlags::TransferRead);
  return id;
}

RGResourceId RGPass::read_buf(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access) {
  add_read_usage(id, stage, access);
  return id;
}

RGResourceId RGPass::write_buf(RGResourceId id, rhi::PipelineStage stage) {
  rhi::AccessFlags access{};
  if (type_ == RGPassType::Transfer) {
    access = rhi::AccessFlags::TransferWrite;
  } else {
    access = rhi::AccessFlags::ShaderStorageWrite;
  }
  // if (id.type == RGResourceType::ExternalBuffer) {
  //   access = (type_ == RGPassType::Transfer) ? rhi::AccessFlags::TransferWrite
  //                                            : rhi::AccessFlags::ShaderWrite;
  // } else {
  //   access = rhi::AccessFlags::ShaderStorageWrite;
  // }
  add_write_usage(id, stage, access);
  return id;
}

RGResourceId RGPass::rw_buf(RGResourceId input, rhi::PipelineStage stage) {
  const auto read_access = (input.type == RGResourceType::ExternalBuffer)
                               ? rhi::AccessFlags::ShaderRead
                               : rhi::AccessFlags::ShaderStorageRead;
  const auto write_access = (input.type == RGResourceType::ExternalBuffer)
                                ? ((type_ == RGPassType::Transfer) ? rhi::AccessFlags::TransferWrite
                                                                   : rhi::AccessFlags::ShaderWrite)
                                : rhi::AccessFlags::ShaderStorageWrite;
  auto output = rg_->next_version(input);
  add_read_usage(input, stage, read_access);
  add_write_usage(output, stage, read_access | write_access);
  return output;
}

RGResourceId RGPass::import_external_texture(rhi::TextureHandle tex_handle,
                                             std::string_view debug_name) {
  return rg_->import_external_texture(tex_handle, debug_name);
}

RGResourceId RGPass::import_external_texture(rhi::TextureHandle tex_handle, const RGState& initial,
                                             std::string_view debug_name) {
  return rg_->import_external_texture(tex_handle, initial, debug_name);
}

RGResourceId RGPass::import_external_buffer(rhi::BufferHandle buf_handle,
                                            std::string_view debug_name) {
  return rg_->import_external_buffer(buf_handle, debug_name);
}

RGResourceId RGPass::import_external_buffer(rhi::BufferHandle buf_handle, const RGState& initial,
                                            std::string_view debug_name) {
  return rg_->import_external_buffer(buf_handle, initial, debug_name);
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

void RenderGraph::shutdown() {
  auto destroy = [this](auto& resource_map) {
    for (auto& [key, handles] : resource_map) {
      (void)key;
      for (const auto& handle : handles) {
        device_->destroy(handle);
      }
    }
    resource_map.clear();
  };
  destroy(free_bufs_);
  destroy(free_atts_);
  destroy(history_free_bufs_);
}

void RGPass::add_read_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access,
                            int32_t subresource_mip, int32_t subresource_slice) {
  int32_t mip = subresource_mip;
  int32_t slice = subresource_slice;
  if (id.type == RGResourceType::Buffer || id.type == RGResourceType::ExternalBuffer) {
    mip = -1;
    slice = -1;
  }
  if (id.type == RGResourceType::ExternalTexture || id.type == RGResourceType::ExternalBuffer) {
    rg_->add_external_read_id(id);
    external_reads_.emplace_back(NameAndAccess{id, stage, access, id.type, false, mip, slice});
  } else {
    internal_reads_.emplace_back(NameAndAccess{id, stage, access, id.type, false, mip, slice});
  }
}

void RGPass::add_write_usage(RGResourceId id, rhi::PipelineStage stage, rhi::AccessFlags access,
                             bool is_swapchain_write, int32_t subresource_mip,
                             int32_t subresource_slice) {
  int32_t mip = subresource_mip;
  int32_t slice = subresource_slice;
  if (id.type == RGResourceType::Buffer || id.type == RGResourceType::ExternalBuffer) {
    mip = -1;
    slice = -1;
  }
  rg_->register_write(id, *this);
  if (id.type == RGResourceType::ExternalTexture || id.type == RGResourceType::ExternalBuffer) {
    external_writes_.emplace_back(
        NameAndAccess{id, stage, access, id.type, is_swapchain_write, mip, slice});
  } else {
    internal_writes_.emplace_back(
        NameAndAccess{id, stage, access, id.type, is_swapchain_write, mip, slice});
  }
}

RGResourceId RGPass::rw_tex(RGResourceId input, rhi::PipelineStage stage,
                            rhi::AccessFlags read_access, rhi::AccessFlags write_access,
                            int32_t read_subresource_mip, int32_t write_subresource_mip) {
  auto output = rg_->next_version(input);
  add_read_usage(input, stage, read_access, read_subresource_mip, -1);
  add_write_usage(output, stage, write_access, false, write_subresource_mip, -1);
  return output;
}

void RenderGraph::Pass::w_swapchain_tex(rhi::Swapchain* swapchain) {
  ASSERT(swapchain);
  swapchain_write_ = swapchain;
  auto curr_tex = swapchain->get_current_texture();
  ASSERT(type_ == RGPassType::Graphics);
  // TODO: remove
  auto swapchain_id = rg_->import_external_texture(
      curr_tex,
      RGState{.stage = rhi::PipelineStage::BottomOfPipe, .layout = rhi::ResourceLayout::Undefined},
      "swapchain");
  add_write_usage(swapchain_id, rhi::PipelineStage::ColorAttachmentOutput,
                  rhi::AccessFlags::ColorAttachmentWrite, true);
}
RenderGraph::ResourceRecord RenderGraph::create_resource_record(RGResourceType type,
                                                                uint32_t physical_idx,
                                                                std::string_view debug_name) {
  return {
      .type = type,
      .physical_idx = physical_idx,
      .debug_name = debug_name.empty() ? kInvalidNameId : intern_name(debug_name),
  };
}

void add_buffer_readback_copy(RenderGraph& rg, std::string_view pass_name, RGResourceId src_buf,
                              rhi::BufferHandle dst_buf, RGResourceId dst_rg_id, size_t src_offset,
                              size_t dst_offset, size_t size_bytes) {
  auto& p = rg.add_transfer_pass(pass_name);
  p.copy_from_buf(src_buf);
  p.write_buf(dst_rg_id, rhi::PipelineStage::AllTransfer);
  p.set_ex([&rg, src_buf, dst_buf, dst_offset, src_offset, size_bytes](rhi::CmdEncoder* enc) {
    enc->copy_buffer_to_buffer(rg.get_buf(src_buf), src_offset, dst_buf, dst_offset, size_bytes);
  });
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
