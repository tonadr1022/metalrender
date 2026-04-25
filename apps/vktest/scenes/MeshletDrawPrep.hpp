#pragma once

#include <span>
#include <string_view>

#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/AlphaMaskType.hpp"
#include "gfx/renderer/BufferSuballoc.hpp"
#include "gfx/rhi/Texture.hpp"

namespace teng::gfx {

class ModelGPUMgr;
class RenderGraph;
class ShaderManager;

namespace rhi {
class Device;
}

class GenerateTaskCmdComputePass {
 public:
  GenerateTaskCmdComputePass(rhi::Device& device, RenderGraph& rg, ModelGPUMgr& model_gpu_mgr,
                             ShaderManager& shader_mgr);

  void bake(std::string_view pass_name, uint32_t max_draws, bool late, bool gpu_object_frustum_cull,
            bool gpu_object_occlusion_cull, const BufferSuballoc& view_cb_suballoc,
            const BufferSuballoc& cull_cb, RGResourceId& task_cmd_rg,
            RGResourceId& indirect_args_rg, RGResourceId& visible_object_count_rg,
            RGResourceId* instance_vis_current_rg, RGResourceId* final_depth_pyramid_rg,
            rhi::TextureHandle final_depth_pyramid_tex);

 private:
  rhi::PipelineHandleHolder prepare_meshlets_pso_;
  rhi::PipelineHandleHolder prepare_meshlets_late_pso_;
  rhi::Device& device_;
  RenderGraph& rg_;
  ModelGPUMgr& model_gpu_mgr_;
};

class MeshletDrawPrep {
 public:
  struct PassBuffers {
    RGResourceId task_cmd_rg{};
    RGResourceId indirect_args_rg{};
    RGResourceId visible_object_count_rg{};
  };

  struct TaskRequest {
    std::string_view pass_name;
    uint32_t max_draws{};
    bool late{};
    bool object_frustum_cull{};
    bool object_occlusion_cull{};
    BufferSuballoc view_cb{};
    BufferSuballoc cull_cb{};
    RGResourceId* instance_vis_current_rg{};
    RGResourceId* final_depth_pyramid_rg{};
    rhi::TextureHandle final_depth_pyramid_tex;
  };

  MeshletDrawPrep(rhi::Device& device, RenderGraph& rg, ModelGPUMgr& model_gpu_mgr,
                  ShaderManager& shader_mgr);

  [[nodiscard]] PassBuffers create_pass_buffers(std::string_view label, size_t task_cmd_count,
                                                RGResourceId visible_object_count_rg = {});
  [[nodiscard]] RGResourceId create_visible_count_buffer(std::string_view label);
  [[nodiscard]] RGResourceId create_instance_visibility_buffer(uint32_t max_draws,
                                                               std::string_view label);

  void prime_instance_visibility(RGResourceId& instance_vis_rg, uint32_t max_draws,
                                 std::string_view pass_name);
  void clear_indirect_args(std::string_view pass_name, std::span<RGResourceId> indirect_args);
  void clear_visible_count(RGResourceId& visible_count_rg, std::string_view pass_name);
  void clear_visible_count_and_stats(RGResourceId& visible_count_rg, RGResourceId& stats_rg,
                                     size_t stats_bytes, std::string_view pass_name);

  void bake_task_commands(const TaskRequest& req, PassBuffers& buffers);

 private:
  static constexpr size_t k_indirect_bytes =
      sizeof(uint32_t) * 3 * static_cast<size_t>(AlphaMaskType::Count);

  rhi::PipelineHandleHolder clear_mesh_indirect_pso_;
  GenerateTaskCmdComputePass generate_task_cmd_compute_pass_;
  rhi::Device& device_;
  RenderGraph& rg_;
};

}  // namespace teng::gfx
