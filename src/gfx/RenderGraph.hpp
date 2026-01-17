#pragma once

#include <algorithm>
#include <functional>
#include <unordered_set>

#include "core/Hash.hpp"
#include "gfx/CmdEncoder.hpp"

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

}  // namespace gfx

namespace std {

template <>
struct hash<gfx::AttachmentInfo> {
  size_t operator()(const gfx::AttachmentInfo& att_info) const {
    auto h = std::make_tuple((uint32_t)att_info.size_class, att_info.array_layers,
                             att_info.mip_levels, att_info.dims.x, att_info.dims.y,
                             (uint32_t)att_info.format, att_info.is_swapchain_tex);
    return util::hash::tuple_hash<decltype(h)>{}(h);
  }
};

}  // namespace std

namespace gfx {

using ExecuteFn = std::function<void(rhi::CmdEncoder* enc)>;

class RenderGraph;

enum class RGResourceType { Texture, Buffer };

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

void convert_rg_access(RGAccess access, rhi::AccessFlagsBits& out_access,
                       rhi::PipelineStageBits& out_stages);

struct BufferInfo {
  size_t size;
};

struct ResourceAndUsage {
  RGResourceHandle handle;
  rhi::AccessFlags access;
  rhi::PipelineStage stage;
};

enum class RGPassType { None, Compute, Graphics, Transfer };

class RGPass {
 public:
  RGPass() = default;
  RGPass(std::string name, RenderGraph* rg, uint32_t pass_i, RGPassType type);

  RGResourceHandle sample_tex(const std::string& name);
  RGResourceHandle sample_external_tex(rhi::TextureHandle tex_handle);
  RGResourceHandle write_external_tex(rhi::TextureHandle tex_handle);
  RGResourceHandle write_external_tex(const std::string& name, rhi::TextureHandle tex_handle);
  RGResourceHandle read_tex(const std::string& name);
  RGResourceHandle write_tex(const std::string& name);
  RGResourceHandle add_color_output(const std::string& name, const AttachmentInfo& att_info);
  RGResourceHandle add_depth_output(const std::string& name, const AttachmentInfo& att_info);
  void read_buf(const std::string& name);
  void read_indirect_buf(const std::string& name);
  RGResourceHandle write_buf(const std::string& name, rhi::BufferHandle buf_handle);
  RGResourceHandle read_write_buf(const std::string& name, rhi::BufferHandle buf_handle,
                                  const std::string& input_name);

  [[nodiscard]] const std::vector<ResourceAndUsage>& get_resource_usages() const {
    return resource_usages_;
  }

  [[nodiscard]] const std::vector<uint32_t>& get_resource_reads() const {
    return resource_read_indices_;
  }

  [[nodiscard]] const std::vector<std::string>& get_resource_read_names() const {
    return resource_read_names_;
  }

  [[nodiscard]] bool has_resource_writes() const {
    return std::ranges::any_of(resource_usages_, [](const ResourceAndUsage& usage) {
      return usage.access & rhi::AccessFlags_AnyWrite;
    });
  }

  [[nodiscard]] uint32_t get_idx() const { return pass_i_; }
  void set_ex(auto&& f) { execute_fn_ = f; }
  [[nodiscard]] const std::string& get_name() const { return name_; }
  [[nodiscard]] const ExecuteFn& get_execute_fn() const { return execute_fn_; }
  [[nodiscard]] RGPassType type() const { return type_; }

 private:
  RGResourceHandle read_tex(const std::string& name, RGAccess access);
  std::vector<ResourceAndUsage> resource_usages_;
  std::vector<std::string> resource_read_names_;  // same as resource_usages_.size();
  std::vector<uint32_t> resource_read_indices_;
  ExecuteFn execute_fn_;
  RenderGraph* rg_;
  uint32_t pass_i_{};
  const std::string name_;
  RGPassType type_{};
};

class RenderGraph {
 public:
  void init(rhi::Device* device);
  void depend_passes(RGPass& pre_pass, RGPass& post_pass, rhi::AccessFlags access,
                     rhi::PipelineStage stage);

  void bake(glm::uvec2 fb_size, bool verbose = false);

  void execute();

  RGPass& add_compute_pass(const std::string& name) { return add_pass(name, RGPassType::Compute); }
  RGPass& add_transfer_pass(const std::string& name) {
    return add_pass(name, RGPassType::Transfer);
  }
  RGPass& add_graphics_pass(const std::string& name) {
    return add_pass(name, RGPassType::Graphics);
  }

  struct ResourcePassUsages {
    // TODO: NOOOOOOOOOOOOOOOOOOOOOOO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // PLEAAAAAAAAAASEEEEEEEEEEEEEEEE STOPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP
    // PLEAAAAAAAAAASEEEEEEEEEEEEEEEE
    std::vector<uint32_t> read_pass_indices;
    std::vector<uint32_t> write_pass_indices;
    // TODO: NOOOOOOOOOOOOOOOOOOOOOOO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // PLEAAAAAAAAAASEEEEEEEEEEEEEEEE STOPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP
    // PLEAAAAAAAAAASEEEEEEEEEEEEEEEE
  };

  RGResourceHandle add_tex_usage(const std::string& name, const AttachmentInfo& att_info,
                                 RGAccess access, RGPass& pass);
  RGResourceHandle add_tex_usage(const std::string& name, RGAccess access, RGPass& pass);
  RGResourceHandle add_buf_usage(std::string name, rhi::BufferHandle buf_handle, RGAccess access,
                                 RGPass& pass, const std::string& input_name);
  RGResourceHandle add_buf_read_usage(const std::string& name, RGAccess access, RGPass& pass);
  RGResourceHandle add_tex_usage(rhi::TextureHandle handle, RGAccess access, RGPass& pass);
  RGResourceHandle add_external_tex_write_usage(const std::string& name, rhi::TextureHandle handle,
                                                RGAccess access, RGPass& pass);

  rhi::TextureHandle get_att_img(RGResourceHandle tex_handle);

 private:
  RGPass& add_pass(const std::string& name, RGPassType type);
  struct TextureUsage {
    AttachmentInfo att_info;
    rhi::TextureHandle handle;
    std::vector<RGResourceHandle> handles_using;
  };

  struct BufferUsage {
    rhi::BufferHandle handle;
    std::vector<std::string> name_stack;
  };

  void reset();

  TextureUsage* get_tex_usage(RGResourceHandle handle);
  BufferUsage* get_buf_usage(RGResourceHandle handle);

  void find_deps_recursive(uint32_t pass_i, uint32_t stack_size);

  std::vector<uint32_t>& get_resource_write_pass_usages(RGResourceHandle handle) {
    return resource_pass_usages_[(int)handle.type][handle.idx].write_pass_indices;
  }

  std::vector<uint32_t>& get_resource_read_pass_usages(RGResourceHandle handle) {
    return resource_pass_usages_[(int)handle.type][handle.idx].read_pass_indices;
  }
  void add_resource_to_pass_writes(RGResourceHandle handle, RGPass& pass) {
    resource_pass_usages_[(int)handle.type][handle.idx].write_pass_indices.emplace_back(
        pass.get_idx());
  }

  void add_resource_to_pass_reads(RGResourceHandle handle, RGPass& pass) {
    resource_pass_usages_[(int)handle.type][handle.idx].read_pass_indices.emplace_back(
        pass.get_idx());
  }

  template <typename... Args>
  void emplace_back_tex_usage(Args&&... args) {
    tex_usages_.emplace_back(std::forward<Args>(args)...);
    resource_pass_usages_[(int)RGResourceType::Texture].emplace_back();
  }

  template <typename... Args>
  void emplace_back_buf_usage(Args&&... args) {
    buf_usages_.emplace_back(std::forward<Args>(args)...);
    resource_pass_usages_[(int)RGResourceType::Buffer].emplace_back();
  }

  std::vector<TextureUsage> tex_usages_;
  std::vector<BufferUsage> buf_usages_;
  std::vector<ResourcePassUsages> resource_pass_usages_[2];
  std::string get_resource_name(RGResourceHandle handle) {
    auto it = resource_handle_to_name_.find(handle.to64());
    return it == resource_handle_to_name_.end() ? "" : it->second;
  }

  struct BarrierInfo {
    RGResourceHandle resource;
    rhi::PipelineStage src_stage;
    rhi::PipelineStage dst_stage;
    rhi::AccessFlags src_access;
    rhi::AccessFlags dst_access;
  };
  std::vector<RGPass> passes_;
  std::vector<std::vector<BarrierInfo>> pass_barrier_infos_;
  std::vector<std::unordered_set<uint32_t>> pass_dependencies_;
  std::unordered_map<std::string, RGResourceHandle> resource_name_to_handle_;
  std::unordered_map<uint64_t, std::string> resource_handle_to_name_;
  std::unordered_map<uint64_t, RGResourceHandle> tex_handle_to_handle_;
  std::unordered_map<std::string, uint32_t> resource_read_name_to_writer_pass_;

  std::unordered_map<gfx::AttachmentInfo, std::vector<rhi::TextureHandle>> free_atts_;
  struct AttInfoAndTex {
    AttachmentInfo att_info;
    rhi::TextureHandleHolder tex_handle;
  };

  std::vector<AttInfoAndTex> actual_atts_;
  std::unordered_map<uint64_t, rhi::TextureHandle> rg_resource_handle_to_actual_att_;

  std::vector<uint32_t> sink_passes_;
  std::vector<uint32_t> pass_stack_;

  // cache for intermediate calc data
  std::unordered_set<uint32_t> intermed_pass_visited_;
  std::vector<uint32_t> intermed_pass_stack_;
  rhi::Device* device_{};
  size_t external_texture_count_{};
};

}  // namespace gfx
