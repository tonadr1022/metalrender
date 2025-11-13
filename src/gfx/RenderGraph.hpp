#pragma once

#include <functional>
#include <unordered_set>

#include "gfx/CmdEncoder.hpp"

namespace gfx {

using ExecuteFn = std::function<void(rhi::CmdEncoder* enc)>;

class RenderGraph;

enum class RGResourceType { Texture, Buffer };

const char* to_string(RGResourceType type);

struct RGResourceHandle {
  uint32_t idx{UINT32_MAX};
  RGResourceType type{};

  bool operator==(const RGResourceHandle& other) const {
    return idx == other.idx && type == other.type;
  }

  [[nodiscard]] uint64_t to_64() const {
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
  AnyRead = ColorRead | DepthStencilRead | VertexRead | IndexRead | IndirectRead | ComputeRead |
            TransferRead | FragmentSample | FragmentStorageRead | ComputeSample,
  AnyWrite = ColorWrite | DepthStencilWrite | ComputeWrite | TransferWrite,
};

void convert_rg_access(RGAccess access, rhi::AccessFlagsBits& out_access,
                       rhi::PipelineStageBits& out_stages);

enum class SizeClass : uint8_t { Swapchain, Custom };

struct AttachmentInfo {
  glm::uvec3 dims{};
  uint32_t mip_levels{1};
  uint32_t array_layers{1};
  SizeClass size_class{SizeClass::Swapchain};
};

struct BufferInfo {
  size_t size;
};

struct ResourceAndUsage {
  RGResourceHandle handle;
  rhi::AccessFlags access;
  rhi::PipelineStage stage;
};

class RGPass {
 public:
  RGPass() = default;
  RGPass(std::string name, RenderGraph* rg, uint32_t pass_i);

  RGResourceHandle add(const std::string& name, AttachmentInfo att_info, RGAccess access);
  RGResourceHandle add(rhi::TextureHandle tex_handle, RGAccess access);

  RGResourceHandle use_buf(BufferInfo, RGAccess) { return {}; }

  [[nodiscard]] const std::vector<ResourceAndUsage>& get_resource_usages() const {
    return resource_usages_;
  }

  [[nodiscard]] const std::vector<uint32_t>& get_resource_reads() const {
    return resource_read_indices_;
  }

  [[nodiscard]] bool has_resource_writes() const {
    return resource_usages_.size() > resource_read_indices_.size();
  }

  [[nodiscard]] uint32_t get_idx() const { return pass_i_; }
  void set_execute_fn(auto&& f) { execute_fn_ = f; }
  [[nodiscard]] const std::string& get_name() const { return name_; }
  [[nodiscard]] const ExecuteFn& get_execute_fn() const { return execute_fn_; }

 private:
  std::vector<ResourceAndUsage> resource_usages_;
  std::vector<uint32_t> resource_read_indices_;
  ExecuteFn execute_fn_;
  RenderGraph* rg_;
  uint32_t pass_i_{};
  const std::string name_;
};

class RenderGraph {
 public:
  void init(rhi::Device* device);
  void depend_passes(RGPass& pre_pass, RGPass& post_pass, rhi::AccessFlags access,
                     rhi::PipelineStage stage);

  void bake(bool verbose = false);

  void execute();

  RGPass& add_pass(const std::string& name);

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

  RGResourceHandle add_tex_usage(rhi::TextureHandle handle, RGAccess access, RGPass& pass);

 private:
  struct ResourceUsage {
    rhi::AccessFlagsBits accesses;
    rhi::PipelineStageBits stages;
  };
  struct TextureUsage : public ResourceUsage {
    AttachmentInfo att_info;
    rhi::TextureHandle handle;
  };

  struct BufferUsage : ResourceUsage {
    size_t size;
  };

  void reset();

  TextureUsage* get_tex_usage(RGResourceHandle handle);
  BufferUsage* get_buf_usage(RGResourceHandle handle);
  ResourceUsage* get_resource_usage(RGResourceHandle handle);

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

  std::vector<TextureUsage> tex_usages_;
  std::vector<BufferUsage> buf_usages_;
  std::vector<ResourcePassUsages> resource_pass_usages_[2];
  std::string get_resource_name(RGResourceHandle handle) {
    auto it = resource_handle_to_name_.find(handle.to_64());
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

  std::vector<uint32_t> sink_passes_;
  std::vector<uint32_t> pass_stack_;

  // cache for intermediate calc data
  std::unordered_set<uint32_t> intermed_pass_visited_;
  std::vector<uint32_t> intermed_pass_stack_;
  rhi::Device* device_{};
  size_t external_texture_count_{};
};

}  // namespace gfx
