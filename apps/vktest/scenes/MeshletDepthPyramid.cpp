#include "MeshletDepthPyramid.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/MathUtil.hpp"
#include "core/Util.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "hlsl/depth_reduce/shared_depth_reduce.h"
#include "imgui.h"

namespace teng::gfx {

MeshletDepthPyramid::MeshletDepthPyramid(rhi::Device& device, RenderGraph& rg,
                                         ShaderManager& shader_mgr)
    : device_(device), rg_(rg) {
  depth_reduce_pso_ = shader_mgr.create_compute_pipeline(
      {.path = "depth_reduce/depth_reduce", .type = rhi::ShaderType::Compute});
}

uint32_t MeshletDepthPyramid::prev_pow2(uint32_t val) {
  uint32_t v = 1;
  while (v * 2 < val) {
    v *= 2;
  }
  return v;
}

void MeshletDepthPyramid::resize(glm::uvec2 source_dims) {
  if (source_dims.x == 0 || source_dims.y == 0) {
    return;
  }

  const glm::uvec3 size{prev_pow2(source_dims.x), prev_pow2(source_dims.y), 1u};
  if (tex_.is_valid()) {
    if (auto* existing = device_.get_tex(tex_); existing && existing->desc().dims == size) {
      return;
    }
  }

  shutdown();
  const auto mip_levels = static_cast<uint32_t>(math::get_mip_levels(size.x, size.y));
  tex_ = rhi::TexAndViewHolder{device_.create_tex_h(rhi::TextureDesc{
      .format = rhi::TextureFormat::R32float,
      .usage =
          rhi::TextureUsage::Storage | rhi::TextureUsage::ShaderWrite | rhi::TextureUsage::Sample,
      .dims = size,
      .mip_levels = mip_levels,
      .name = "meshlet_test_depth_pyramid",
  })};
  tex_.views.reserve(mip_levels);
  for (uint32_t i = 0; i < mip_levels; i++) {
    tex_.views.push_back(device_.create_tex_view(tex_.handle, i, 1, 0, 1));
  }
  debug_mip_ = std::clamp(debug_mip_, 0, std::max(0, static_cast<int>(mip_levels) - 1));
}

void MeshletDepthPyramid::shutdown() {
  if (!tex_.handle.is_valid()) {
    return;
  }
  for (auto v : tex_.views) {
    device_.destroy(tex_.handle, v);
  }
  tex_.views.clear();
  tex_ = {};
}

uint32_t MeshletDepthPyramid::mip_count() const {
  if (!tex_.is_valid()) {
    return 0;
  }
  return device_.get_tex(tex_)->desc().mip_levels;
}

glm::uvec2 MeshletDepthPyramid::dims() const {
  if (!tex_.is_valid()) {
    return {};
  }
  return glm::uvec2{device_.get_tex(tex_)->desc().dims};
}

uint32_t MeshletDepthPyramid::debug_view_bindless_idx() const {
  if (!tex_.is_valid() || tex_.views.empty()) {
    return UINT32_MAX;
  }
  const int mip = std::clamp(debug_mip_, 0, static_cast<int>(tex_.views.size()) - 1);
  return device_.get_tex_view_bindless_idx(tex_.handle, tex_.views[static_cast<size_t>(mip)]);
}

RGResourceId MeshletDepthPyramid::bake(RGResourceId depth_src_rg, std::string_view import_name,
                                       std::string_view pass_prefix) {
  if (!tex_.is_valid()) {
    return {};
  }

  RGResourceId depth_pyramid_id = rg_.import_external_texture(tex_.handle, import_name);
  const glm::uvec2 dp_dims = dims();
  const uint32_t final_mip = mip_count() - 1;
  RGResourceId final_depth_pyramid_rg{};

  for (uint32_t mip = 0; mip <= final_mip; mip++) {
    auto& p = rg_.add_compute_pass(std::string(pass_prefix) + std::to_string(mip));
    RGResourceId depth_handle{};
    if (mip == 0) {
      depth_handle = p.sample_tex(depth_src_rg, rhi::PipelineStage::ComputeShader,
                                  RgSubresourceRange::single_mip(0));
      p.write_tex(depth_pyramid_id, rhi::PipelineStage::ComputeShader,
                  RgSubresourceRange::single_mip(mip));
    } else {
      depth_pyramid_id =
          p.rw_tex(depth_pyramid_id, rhi::PipelineStage::ComputeShader,
                   rhi::AccessFlags::ShaderSampledRead, rhi::AccessFlags::ShaderWrite,
                   RgSubresourceRange::single_mip(mip - 1), RgSubresourceRange::single_mip(mip));
    }
    if (mip == final_mip) {
      final_depth_pyramid_rg = depth_pyramid_id;
    }

    p.set_ex([this, mip, depth_handle, dp_dims](rhi::CmdEncoder* enc) {
      enc->bind_pipeline(depth_reduce_pso_);
      glm::uvec2 in_dims = (mip == 0) ? device_.get_tex(rg_.get_att_img(depth_handle))->desc().dims
                                      : glm::uvec2{std::max(1u, dp_dims.x >> (mip - 1)),
                                                   std::max(1u, dp_dims.y >> (mip - 1))};
      DepthReducePC pc{.in_tex_dim_x = in_dims.x,
                       .in_tex_dim_y = in_dims.y,
                       .out_tex_dim_x = std::max(1u, dp_dims.x >> mip),
                       .out_tex_dim_y = std::max(1u, dp_dims.y >> mip)};
      enc->push_constants(&pc, sizeof(pc));

      if (mip == 0) {
        enc->bind_srv(rg_.get_att_img(depth_handle), 0);
      } else {
        enc->bind_srv(tex_.handle, 0, tex_.views[static_cast<size_t>(mip - 1)]);
      }
      enc->bind_uav(tex_.handle, 0, tex_.views[static_cast<size_t>(mip)]);

      constexpr size_t k_tg_size = 8;
      enc->dispatch_compute(glm::uvec3{align_divide_up(pc.out_tex_dim_x, k_tg_size),
                                       align_divide_up(pc.out_tex_dim_y, k_tg_size), 1},
                            glm::uvec3{k_tg_size, k_tg_size, 1});
    });
  }

  return final_depth_pyramid_rg;
}

void MeshletDepthPyramid::add_debug_imgui() {
  if (!tex_.is_valid()) {
    return;
  }

  const glm::uvec2 dp_dims = dims();
  const int levels = static_cast<int>(mip_count());
  ImGui::Separator();
  if (levels > 1) {
    ImGui::SliderInt("Depth pyramid mip", &debug_mip_, 0, std::max(0, levels - 1));
    const int mip = std::clamp(debug_mip_, 0, std::max(0, levels - 1));
    const auto mip_u = static_cast<uint32_t>(mip);
    const uint32_t mv_w = std::max(1u, dp_dims.x >> mip_u);
    const uint32_t mv_h = std::max(1u, dp_dims.y >> mip_u);
    const uint32_t view_bindless =
        device_.get_tex_view_bindless_idx(tex_.handle, tex_.views[static_cast<size_t>(mip)]);
    const float disp_w = 240.f;
    const float disp_h = disp_w * static_cast<float>(mv_h) / static_cast<float>(mv_w);
    ImGui::Image(MakeImGuiTexRefBindlessFloatView(view_bindless), ImVec2(disp_w, disp_h),
                 ImVec2(0, 0), ImVec2(1, 1));
  } else {
    ImGui::TextUnformatted("Depth pyramid (single mip; reduce skipped)");
  }
}

}  // namespace teng::gfx
