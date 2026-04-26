#pragma once

#include <string_view>

#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/rhi/Texture.hpp"
#include "glm/ext/vector_uint2.hpp"

namespace teng::gfx {

class RenderGraph;
class ShaderManager;

namespace rhi {
class Device;
}

class MeshletDepthPyramid {
 public:
  MeshletDepthPyramid(rhi::Device& device, RenderGraph& rg, ShaderManager& shader_mgr);

  void resize(glm::uvec2 source_dims);
  void shutdown();
  [[nodiscard]] bool is_valid() const { return tex_.is_valid(); }
  [[nodiscard]] rhi::TextureHandle texture() const { return tex_.handle; }
  [[nodiscard]] const rhi::TexAndViewHolder& texture_holder() const { return tex_; }
  [[nodiscard]] uint32_t debug_view_bindless_idx() const;
  [[nodiscard]] uint32_t mip_count() const;
  [[nodiscard]] glm::uvec2 dims() const;

  RGResourceId bake(RGResourceId depth_src_rg, std::string_view import_name,
                    std::string_view pass_prefix);
  void add_debug_imgui();

 private:
  static uint32_t prev_pow2(uint32_t val);

  rhi::PipelineHandleHolder depth_reduce_pso_;
  rhi::TexAndViewHolder tex_;
  int debug_mip_{0};
  rhi::Device& device_;
  RenderGraph& rg_;
};

}  // namespace teng::gfx
