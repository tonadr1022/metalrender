#include "MemeRenderer123.hpp"

#include <algorithm>
#include <fstream>
#include <glm/ext/vector_integer.hpp>
#include <tracy/Tracy.hpp>

#include "GLFW/glfw3.h"
#include "UI.hpp"
#include "Window.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/MathUtil.hpp"
#include "core/StringUtil.hpp"
#include "core/Util.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Pipeline.hpp"
#include "gfx/rhi/QueryPool.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "hlsl/default_vertex.h"
#include "hlsl/depth_reduce/shared_depth_reduce.h"
#include "hlsl/material.h"
#include "hlsl/shader_constants.h"
#include "hlsl/shared_basic_indirect.h"
#include "hlsl/shared_basic_tri.h"
#include "hlsl/shared_cull_data.h"
#include "hlsl/shared_draw_cull.h"
#include "hlsl/shared_forward_meshlet.h"
#include "hlsl/shared_globals.h"
#include "hlsl/shared_indirect.h"
#include "hlsl/shared_mesh_data.h"
#include "hlsl/shared_task_cmd.h"
#include "hlsl/shared_tex_only.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"
#include "ktx.h"

using rhi::PipelineStage;
namespace {

using rhi::RenderingAttachmentInfo;
using rhi::ShaderType;

glm::mat4 infinite_perspective_proj(float fov_y, float aspect, float z_near) {
  // clang-format off
  float f = 1.0f / tanf(fov_y / 2.0f);
  return {
    f / aspect, 0.0f, 0.0f, 0.0f,
    0.0f, f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, -1.0f,
    0.0f, 0.0f, z_near, 0.0f};
  // clang-format on
}

uint32_t prev_pow2(uint32_t val) {
  uint32_t v = 1;
  while (v * 2 < val) {
    v *= 2;
  }
  return v;
}

}  // namespace

namespace gfx {

void MemeRenderer123::render([[maybe_unused]] const RenderArgs& args) {
  ZoneScoped;
  curr_frame_idx_ = frame_num_ % device_->get_info().frames_in_flight;
  frame_gpu_upload_allocator_.reset(curr_frame_idx_);
  shader_mgr_->replace_dirty_pipelines();

  indirect_cmd_buf_ids_.clear();

  set_cull_data_and_globals(args);
  static glm::uvec2 prev_fb_size{};
  auto curr_fb_size = window_->get_window_size();
  if (prev_fb_size != curr_fb_size) {
    recreate_swapchain_sized_textures();
    prev_fb_size = curr_fb_size;
  }
  add_render_graph_passes(args);
  static int i = 0;
  rg_verbose_ = i++ == -2;
  rg_.bake(window_->get_window_size(), rg_verbose_);

  if (!buffer_copy_mgr_.get_copies().empty()) {
    auto* enc = device_->begin_cmd_encoder();
    for (const auto& copy : buffer_copy_mgr_.get_copies()) {
      enc->copy_buffer_to_buffer(copy.src_buf, copy.src_offset, copy.dst_buf, copy.dst_offset,
                                 copy.size);
    }
    enc->end_encoding();
    buffer_copy_mgr_.clear_copies();
  }

  // flush textures
  if (imgui_renderer_->has_dirty_textures() || !pending_texture_uploads_.empty()) {
    auto* enc = device_->begin_cmd_encoder();
    flush_pending_texture_uploads(enc);
    enc->end_encoding();
  }

  rg_.execute();

  device_->submit_frame();

  frame_num_++;
}

uint32_t MemeRenderer123::get_bindless_idx(const rhi::BufferHandleHolder& buf) const {
  return device_->get_buf(buf)->bindless_idx();
}

void MemeRenderer123::add_render_graph_passes(const RenderArgs& args) {
  ZoneScoped;
  {
    if (instance_data_mgr_.has_pending_frees(curr_frame_idx_)) {
      auto& p = rg_.add_transfer_pass("free_instance_data");
      p.w_external_buf("instance_data_buf", instance_data_mgr_.get_instance_data_buf());
      p.set_ex([this](rhi::CmdEncoder* enc) {
        instance_data_mgr_.flush_pending_frees(curr_frame_idx_, enc);
      });
    }
  }

  if (mesh_shaders_enabled_) {
    if (!culling_paused_) {
      auto& clear_bufs_pass = rg_.add_compute_pass("clear_bufs");
      auto out_draw_count_buf_rg_handle = clear_bufs_pass.w_buf(
          "out_draw_count_buf1", rhi::PipelineStage_ComputeShader, sizeof(uint32_t) * 3);
      clear_bufs_pass.set_ex([this, out_draw_count_buf_rg_handle](rhi::CmdEncoder* enc) {
        enc->bind_pipeline(reset_counts_buf_pso_);
        uint32_t pc = device_->get_buf(rg_.get_buf(out_draw_count_buf_rg_handle))->bindless_idx();
        enc->push_constants(&pc, sizeof(pc));
        enc->dispatch_compute(glm::uvec3{1, 1, 1}, glm::uvec3{1, 1, 1});
      });
    }

    auto& prep_meshlets_pass = rg_.add_compute_pass("meshlet_draw_cull");
    if (instance_data_mgr_.has_pending_frees(curr_frame_idx_)) {
      prep_meshlets_pass.r_external_buf("instance_data_buf");
    }
    const char* output_buf_name = "out_draw_count_buf2";
    RGResourceHandle out_draw_count_buf_rg_handle;
    if (!culling_paused_) {
      out_draw_count_buf_rg_handle = prep_meshlets_pass.rw_buf(
          output_buf_name, rhi::PipelineStage_ComputeShader, "out_draw_count_buf1");
    } else {
      out_draw_count_buf_rg_handle = prep_meshlets_pass.w_buf(
          output_buf_name, rhi::PipelineStage_ComputeShader, sizeof(uint32_t) * 3);
    }
    RGResourceHandle task_cmd_buf_rg_handle{};
    if (static_draw_batch_.get_stats().vertex_count > 0) {
      task_cmd_buf_rg_handle =
          prep_meshlets_pass.w_buf("task_cmd_buf", rhi::PipelineStage_ComputeShader,
                                   static_draw_batch_.task_cmd_count * sizeof(TaskCmd));
    }
    prep_meshlets_pass.set_ex([this, task_cmd_buf_rg_handle,
                               out_draw_count_buf_rg_handle](rhi::CmdEncoder* enc) {
      if (!instance_data_mgr_.has_draws()) {
        return;
      }
      enc->bind_pipeline(draw_cull_pso_);
      auto task_cmd_buf_handle = rg_.get_buf(task_cmd_buf_rg_handle);
      if (!culling_paused_) {
        DrawCullPC pc{
            .globals_buf_idx = frame_globals_buf_info_.idx,
            .globals_buf_offset_bytes = frame_globals_buf_info_.offset_bytes,
            .cull_data_idx = frame_cull_data_buf_info_.idx,
            .cull_data_offset_bytes = frame_cull_data_buf_info_.offset_bytes,
            .task_cmd_buf_idx = device_->get_buf(task_cmd_buf_handle)->bindless_idx(),
            .draw_cnt_buf_idx =
                device_->get_buf(rg_.get_buf(out_draw_count_buf_rg_handle))->bindless_idx(),
            .instance_data_buf_idx =
                device_->get_buf(instance_data_mgr_.get_instance_data_buf())->bindless_idx(),
            .mesh_data_buf_idx = static_draw_batch_.mesh_buf.get_buffer()->bindless_idx(),
            .max_draws = all_model_data_.max_objects,
            .culling_enabled = culling_enabled_,
        };
        enc->push_constants(&pc, sizeof(pc));
        enc->dispatch_compute(glm::uvec3{align_divide_up(all_model_data_.max_objects, 64ull), 1, 1},
                              glm::uvec3{64, 1, 1});
      }
    });
  } else {
    auto& prepare_indirect_pass = rg_.add_compute_pass("prepare_indirect");
    prepare_indirect_pass.w_external_buf("indirect_buffer", instance_data_mgr_.get_draw_cmd_buf());
    prepare_indirect_pass.set_ex([this, &args](rhi::CmdEncoder* enc) {
      BasicIndirectPC pc{
          .vp = get_proj_matrix() * args.view_mat,
          .vert_buf_idx = static_draw_batch_.vertex_buf.get_buffer()->bindless_idx(),
          .instance_data_buf_idx =
              device_->get_buf(instance_data_mgr_.get_instance_data_buf())->bindless_idx(),
          .mat_buf_idx = materials_buf_.get_buffer()->bindless_idx(),
      };

      indirect_cmd_buf_ids_.emplace_back(enc->prepare_indexed_indirect_draws(
          instance_data_mgr_.get_draw_cmd_buf(), 0, all_model_data_.max_objects,
          static_draw_batch_.index_buf.get_buffer_handle(), 0, &pc, sizeof(pc),
          sizeof(DefaultVertex)));
    });
  }

  std::string final_depth_pyramid_name;
  const char* out_counts_buf_name = "out_counts_buf";
  {
    auto& p = rg_.add_transfer_pass("clear_out_counts_buf");
    p.w_external_buf(out_counts_buf_name, out_counts_buf_[curr_frame_idx_].handle);
    p.set_ex([this](rhi::CmdEncoder* enc) {
      enc->fill_buffer(out_counts_buf_[curr_frame_idx_].handle, 0, sizeof(MeshletDrawStats), 0);
    });
  }

  const char* last_gbuffer_a_name = "gbuffer_a";
  const char* last_depth_name = "depth_tex";
  auto add_draw_pass = [&args, this, &final_depth_pyramid_name, &last_gbuffer_a_name,
                        &last_depth_name, &out_counts_buf_name](bool late, const char* name) {
    auto& p = rg_.add_graphics_pass(name);
    RGResourceHandle task_cmd_buf_rg_handle;
    RGResourceHandle out_draw_count_buf_rg_handle{};
    if (mesh_shaders_enabled_) {
      if (instance_data_mgr_.has_draws()) {
        task_cmd_buf_rg_handle =
            p.r_buf("task_cmd_buf",
                    (PipelineStage)(rhi::PipelineStage_MeshShader | rhi::PipelineStage_TaskShader));
      }
      out_draw_count_buf_rg_handle = p.r_buf("out_draw_count_buf2", rhi::PipelineStage_TaskShader);
      if (late) {
        p.sample_external_tex(final_depth_pyramid_name, rhi::PipelineStage_TaskShader);
        p.rw_external_buf("meshlet_vis_buf2", "meshlet_vis_buf", rhi::PipelineStage_TaskShader);
      } else {
        p.w_external_buf("meshlet_vis_buf",
                         instance_data_mgr_.get_meshlet_vis_buf().get_buffer_handle(),
                         rhi::PipelineStage_TaskShader);
      }
    } else {
      p.r_external_buf("indirect_buffer", rhi::PipelineStage_DrawIndirect);
    }
    RGResourceHandle rg_gbuffer_a_handle;
    RGResourceHandle rg_depth_handle;
    if (late) {
      const char* new_gbuffer_a_name = "gbuffer_a2";
      rg_gbuffer_a_handle = p.rw_color_output(new_gbuffer_a_name, last_gbuffer_a_name);
      last_gbuffer_a_name = new_gbuffer_a_name;
      const char* new_depth_name = "depth_tex2";
      rg_depth_handle = p.rw_depth_output(new_depth_name, last_depth_name);
      last_depth_name = new_depth_name;
      const char* next_out_counts_buf_name = "out_counts_buf3";
      p.rw_external_buf(next_out_counts_buf_name, out_counts_buf_name,
                        rhi::PipelineStage_TaskShader);
      out_counts_buf_name = next_out_counts_buf_name;
    } else {
      const char* next_out_counts_buf_name = "out_counts_buf2";
      p.rw_external_buf(next_out_counts_buf_name, out_counts_buf_name,
                        rhi::PipelineStage_TaskShader);
      out_counts_buf_name = next_out_counts_buf_name;
      rg_gbuffer_a_handle =
          p.w_color_output(last_gbuffer_a_name, {.format = rhi::TextureFormat::R16G16B16A16Sfloat});
      rg_depth_handle = p.w_depth_output(last_depth_name, {.format = rhi::TextureFormat::D32float});
    }

    p.set_ex([this, rg_depth_handle, rg_gbuffer_a_handle, &args, late, task_cmd_buf_rg_handle,
              out_draw_count_buf_rg_handle](rhi::CmdEncoder* enc) {
      if (!instance_data_mgr_.has_draws()) {
        return;
      }
      ZoneScopedN("Execute gbuffer pass");
      auto depth_handle = rg_.get_att_img(rg_depth_handle);
      ASSERT(depth_handle.is_valid());
      auto gbuffer_a_tex = rg_.get_att_img(rg_gbuffer_a_handle);
      auto load_op = late ? rhi::LoadOp::Load : rhi::LoadOp::Clear;
      enc->begin_rendering({
          RenderingAttachmentInfo::color_att(gbuffer_a_tex, load_op, {.color = args.clear_color}),
          RenderingAttachmentInfo::depth_stencil_att(
              depth_handle, load_op, {.depth_stencil = {.depth = reverse_z_ ? 0.f : 1.f}}),
      });
      enc->set_depth_stencil_state(reverse_z_ ? rhi::CompareOp::Greater : rhi::CompareOp::Less,
                                   true);
      enc->set_wind_order(rhi::WindOrder::CounterClockwise);
      enc->set_cull_mode(rhi::CullMode::Back);
      enc->set_viewport({0, 0}, window_->get_window_size());

      if (mesh_shaders_enabled_) {
        enc->bind_pipeline(test_task_pso_);
        Task2PC pc{
            .max_draws = all_model_data_.max_objects,
            .max_meshlets = all_model_data_.max_meshlets,
            .pass = late,
            .flags = 0,
        };
        static_assert(sizeof(Task2PC) <= 80);

        auto out_draw_count_buf_handle = rg_.get_buf(out_draw_count_buf_rg_handle);
        enc->bind_uav(out_draw_count_buf_handle, 0);
        enc->bind_uav(instance_data_mgr_.get_meshlet_vis_buf().get_buffer_handle(), 1);
        enc->bind_uav(out_counts_buf_[curr_frame_idx_].handle, 2);
        if (late) {
          enc->bind_srv(depth_pyramid_tex_.handle, 3);
        }
        auto task_cmd_buf_handle = rg_.get_buf(task_cmd_buf_rg_handle);
        enc->bind_srv(task_cmd_buf_handle, 4);
        enc->bind_srv(static_draw_batch_.mesh_buf.get_buffer_handle(), 5);
        enc->bind_srv(static_draw_batch_.meshlet_buf.get_buffer_handle(), 6);
        enc->bind_srv(static_draw_batch_.meshlet_triangles_buf.get_buffer_handle(), 7);
        enc->bind_srv(static_draw_batch_.meshlet_vertices_buf.get_buffer_handle(), 8);
        enc->bind_srv(static_draw_batch_.vertex_buf.get_buffer_handle(), 9);
        enc->bind_srv(instance_data_mgr_.get_instance_data_buf(), 10);
        enc->bind_srv(materials_buf_.get_buffer_handle(), 11);
        enc->bind_cbv(frame_globals_buf_info_.buf, GLOBALS_SLOT,
                      frame_globals_buf_info_.offset_bytes);
        enc->bind_cbv(frame_cull_data_buf_info_.buf, 4, frame_cull_data_buf_info_.offset_bytes);

        if (meshlet_frustum_culling_enabled_ && culling_enabled_) {
          pc.flags |= MESHLET_FRUSTUM_CULL_ENABLED_BIT;
        }
        if (meshlet_cone_culling_enabled_ && culling_enabled_) {
          pc.flags |= MESHLET_CONE_CULL_ENABLED_BIT;
        }
        if (meshlet_occlusion_culling_enabled_ && culling_enabled_) {
          pc.flags |= MESHLET_OCCLUSION_CULL_ENABLED_BIT;
        }
        enc->push_constants(&pc, sizeof(pc));
        enc->draw_mesh_threadgroups_indirect(out_draw_count_buf_handle, 0, {K_TASK_TG_SIZE, 1, 1},
                                             {K_MESH_TG_SIZE, 1, 1});
      } else {
        enc->bind_pipeline(test2_pso_);
        ASSERT(indirect_cmd_buf_ids_.size());
        enc->draw_indexed_indirect(instance_data_mgr_.get_draw_cmd_buf(), indirect_cmd_buf_ids_[0],
                                   all_model_data_.max_objects, 0);
      }
      enc->end_rendering();
    });
  };

  bool meshet_occlusion_culling_was_enabled =
      meshlet_occlusion_culling_enabled_ && culling_enabled_;

  add_draw_pass(false, "draw_pass_early");

  if (meshet_occlusion_culling_was_enabled) {
    auto* dp_tex = device_->get_tex(depth_pyramid_tex_);
    auto dp_dims = glm::uvec2{dp_tex->desc().dims};
    uint32_t mip_levels = math::get_mip_levels(dp_dims.x, dp_dims.y);
    std::string read_name;
    uint32_t final_mip = mip_levels - 1;
    for (uint32_t mip = 0; mip < final_mip; mip++) {
      auto& p = rg_.add_compute_pass("depth_reduce_" + std::to_string(mip));
      RGResourceHandle depth_handle{};
      if (mip == 0) {
        depth_handle = p.r_tex("depth_tex");
      } else {
        p.r_external_tex(read_name);
      }
      auto write_name = "depth_pyramid_tex_reduce_" + std::to_string(mip);
      if (mip == final_mip - 1) {
        final_depth_pyramid_name = write_name;
      }

      read_name = write_name;
      p.w_external_tex(write_name, depth_pyramid_tex_.handle);
      p.set_ex([this, mip, depth_handle, dp_dims](rhi::CmdEncoder* enc) {
        enc->bind_pipeline(depth_reduce_pso_);
        glm::uvec2 in_dims = (mip == 0)
                                 ? device_->get_tex(rg_.get_att_img(depth_handle))->desc().dims
                                 : glm::uvec2{std::max(1u, dp_dims.x >> (mip - 1)),
                                              std::max(1u, dp_dims.y >> (mip - 1))};
        DepthReducePC pc{
            .in_tex_dim_x = in_dims.x,
            .in_tex_dim_y = in_dims.y,
            .out_tex_dim_x = dp_dims.x >> mip,
            .out_tex_dim_y = dp_dims.y >> mip,
        };
        enc->push_constants(&pc, sizeof(pc));

        if (mip == 0) {
          enc->bind_srv(rg_.get_att_img(depth_handle), 0);
        } else {
          enc->bind_srv(depth_pyramid_tex_.handle, 0, depth_pyramid_tex_.views[mip - 1]);
        }
        enc->bind_uav(depth_pyramid_tex_.handle, 0, depth_pyramid_tex_.views[mip]);

        constexpr size_t k_tg_size = 8;
        enc->dispatch_compute(glm::uvec3{align_divide_up(pc.out_tex_dim_x, k_tg_size),
                                         align_divide_up(pc.out_tex_dim_y, k_tg_size), 1},
                              glm::uvec3{k_tg_size, k_tg_size, 1});
      });
    }
  }

  if (meshet_occlusion_culling_was_enabled) {
    add_draw_pass(true, "draw_pass_late");
  }

  {  // readback draw counts
    auto& p = rg_.add_transfer_pass("readback_draw_counts");
    p.r_external_buf(out_counts_buf_name);
    const char* result_name = "out_counts_buf_readback";
    p.w_external_buf(result_name, out_counts_buf_readback_[curr_frame_idx_].handle);
    p.set_ex([this](rhi::CmdEncoder* enc) {
      enc->copy_buffer_to_buffer(out_counts_buf_[curr_frame_idx_].handle, 0,
                                 out_counts_buf_readback_[curr_frame_idx_].handle, 0,
                                 sizeof(MeshletDrawStats));
    });
  }

  {
    auto& p = rg_.add_graphics_pass("shade");
    auto gbuffer_a_rg_handle = p.sample_tex(last_gbuffer_a_name);
    if (meshet_occlusion_culling_was_enabled &&
        debug_render_mode_ == DebugRenderMode::DepthReduceMips) {
      p.sample_external_tex(final_depth_pyramid_name);
    }
    p.w_swapchain_tex(swapchain_);
    p.set_ex([this, gbuffer_a_rg_handle, &args](rhi::CmdEncoder* enc) {
      auto* gbuffer_a_tex = device_->get_tex(rg_.get_att_img(gbuffer_a_rg_handle));
      auto dims = gbuffer_a_tex->desc().dims;
      device_->begin_swapchain_rendering(swapchain_, enc);
      enc->bind_pipeline(tex_only_pso_);
      enc->set_wind_order(rhi::WindOrder::Clockwise);
      enc->set_cull_mode(rhi::CullMode::Back);
      enc->set_viewport({0, 0}, dims);

      uint32_t tex_idx{};
      float mult = 1.f;
      if (debug_render_mode_ != DebugRenderMode::DepthReduceMips) {
        tex_idx = gbuffer_a_tex->bindless_idx();
      } else {
        tex_idx = device_->get_tex_view_bindless_idx(depth_pyramid_tex_.handle,
                                                     depth_pyramid_tex_.views[debug_view_mip_]);
        mult = 100.f;
      }
      TexOnlyPC pc{
          .color_mult = glm::vec4{mult, mult, mult, 1},
          .tex_idx = tex_idx,
          .mip_level = static_cast<uint32_t>(debug_view_mip_),
      };
      enc->push_constants(&pc, sizeof(pc));
      enc->draw_primitives(rhi::PrimitiveTopology::TriangleList, 3);

      if (args.draw_imgui) {
        imgui_renderer_->render(enc, {swapchain_->desc_.width, swapchain_->desc_.height},
                                curr_frame_idx_);
      }
      enc->end_rendering();
    });
  }
}

void MemeRenderer123::flush_pending_texture_uploads(rhi::CmdEncoder* enc) {
  ZoneScoped;
  imgui_renderer_->flush_pending_texture_uploads(enc, frame_gpu_upload_allocator_);

  for (const auto& upload : pending_texture_uploads_) {
    const auto& tex_upload = upload.upload;
    auto* tex = device_->get_tex(upload.tex);
    ASSERT(tex);
    ASSERT(upload.upload.data);
    if (tex_upload.load_type == CPUTextureLoadType::Ktx2) {
      auto* ktx_tex = (ktxTexture2*)tex_upload.data.get();
      auto* tex = device_->get_tex(upload.tex);
      ASSERT(tex);
      ASSERT(tex_upload.data);

      const auto& desc = upload.upload.desc;
      size_t block_width = get_block_width_bytes(desc.format);
      size_t bytes_per_block = get_bytes_per_block(desc.format);
      size_t total_img_size = 0;

      for (uint32_t mip_level = 0; mip_level < desc.mip_levels; mip_level++) {
        size_t image_size = ktxTexture_GetImageSize(ktxTexture(ktx_tex), mip_level);
        total_img_size += image_size;
      }

      auto upload_buf = frame_gpu_upload_allocator_.alloc(total_img_size);
      ASSERT(upload_buf.buf.is_valid());

      size_t curr_dst_offset = 0;
      for (uint32_t mip_level = 0; mip_level < desc.mip_levels; mip_level++) {
        size_t offset;
        auto result = ktxTexture_GetImageOffset(ktxTexture(ktx_tex), mip_level, 0, 0, &offset);
        ASSERT(result == KTX_SUCCESS);
        auto img_mip_level_size_bytes = ktxTexture_GetImageSize(ktxTexture(ktx_tex), mip_level);
        ktxTexture_GetLevelSize(ktxTexture(ktx_tex), mip_level);
        uint32_t mip_width = std::max(1u, desc.dims.x >> mip_level);
        uint32_t mip_height = std::max(1u, desc.dims.y >> mip_level);
        uint32_t blocks_wide = align_divide_up(mip_width, block_width);
        auto bytes_per_row = blocks_wide * bytes_per_block;
        memcpy((uint8_t*)upload_buf.write_ptr + curr_dst_offset, (uint8_t*)ktx_tex->pData + offset,
               img_mip_level_size_bytes);
        enc->upload_texture_data(upload_buf.buf, upload_buf.offset + curr_dst_offset, bytes_per_row,
                                 upload.tex, glm::uvec3{mip_width, mip_height, 1},
                                 glm::uvec3{0, 0, 0}, mip_level);
        curr_dst_offset += img_mip_level_size_bytes;
      }
    } else {
      size_t src_bytes_per_row = tex_upload.bytes_per_row;
      size_t bytes_per_row = align_up(src_bytes_per_row, 256);
      // TODO: staging buffer pool
      size_t total_size = bytes_per_row * tex->desc().dims.y;
      auto upload_buf = frame_gpu_upload_allocator_.alloc(total_size);
      size_t dst_offset = 0;
      size_t src_offset = 0;
      for (size_t row = 0; row < tex->desc().dims.y; row++) {
        memcpy((uint8_t*)upload_buf.write_ptr + dst_offset,
               (uint8_t*)tex_upload.data.get() + src_offset, src_bytes_per_row);
        dst_offset += bytes_per_row;
        src_offset += src_bytes_per_row;
      }

      enc->upload_texture_data(upload_buf.buf, upload_buf.offset, bytes_per_row, upload.tex);
    }
  }

  pending_texture_uploads_.clear();
}

bool MemeRenderer123::load_model(const std::filesystem::path& path, const glm::mat4& root_transform,
                                 ModelInstance& model, ModelGPUHandle& out_handle) {
  ZoneScoped;
  ModelLoadResult result;
  if (!model::load_model(path, root_transform, model, result)) {
    return false;
  }

  pending_texture_uploads_.reserve(pending_texture_uploads_.size() + result.texture_uploads.size());

  size_t i = 0;
  // TODO: save elsewhere
  std::vector<uint32_t> img_upload_bindless_indices;
  std::vector<rhi::TextureHandleHolder> out_tex_handles;
  img_upload_bindless_indices.resize(result.texture_uploads.size());
  for (auto& upload : result.texture_uploads) {
    if (upload.data) {
      auto tex = device_->create_tex_h(upload.desc);
      img_upload_bindless_indices[i] = device_->get_tex(tex)->bindless_idx();
      pending_texture_uploads_.push_back(GPUTexUpload{
          .upload = std::move(upload), .tex = tex.handle, .name = get_next_tex_upload_name()});
      out_tex_handles.emplace_back(std::move(tex));
    }
    i++;
  }
  bool resized{};
  assert(!result.materials.empty());
  auto material_alloc = materials_buf_.allocate(result.materials.size(), resized);
  {
    std::vector<M4Material> mats;
    mats.reserve(result.materials.size());
    for (const auto& m : result.materials) {
      mats.push_back(M4Material{.albedo_tex_idx = img_upload_bindless_indices[m.albedo_tex],
                                .color = glm::vec4{m.albedo_factors}});
    }
    buffer_copy_mgr_.copy_to_buffer(mats.data(), mats.size() * sizeof(M4Material),
                                    materials_buf_.get_buffer_handle(),
                                    material_alloc.offset * sizeof(M4Material));
    if (resized) {
      LINFO("materials resized");
      ASSERT(0);
    }
  }

  auto draw_batch_alloc =
      upload_geometry(GeometryBatchType::Static, result.vertices, result.indices,
                      result.meshlet_process_result, result.meshes);

  std::vector<InstanceData> base_instance_datas;
  std::vector<uint32_t> instance_id_to_node;
  base_instance_datas.reserve(model.tot_mesh_nodes);
  instance_id_to_node.reserve(model.tot_mesh_nodes);

  uint32_t total_instance_vertices{};
  uint32_t total_instance_meshlets{};
  uint32_t task_cmd_count{};
  {
    uint32_t curr_meshlet_vis_buf_i{};
    for (size_t node = 0; node < model.nodes.size(); node++) {
      auto mesh_id = model.mesh_ids[node];
      if (model.mesh_ids[node] == Mesh::k_invalid_mesh_id) {
        continue;
      }
      base_instance_datas.emplace_back(InstanceData{
          .mat_id = result.meshes[mesh_id].material_id + material_alloc.offset,
          .mesh_id = draw_batch_alloc.mesh_alloc.offset + mesh_id,
          .meshlet_vis_base = curr_meshlet_vis_buf_i,
      });
      instance_id_to_node.push_back(node);
      curr_meshlet_vis_buf_i +=
          result.meshlet_process_result.meshlet_datas[mesh_id].meshlets.size();
      total_instance_vertices += result.meshes[mesh_id].vertex_count;
      total_instance_meshlets += result.meshes[mesh_id].meshlet_count;
      task_cmd_count += align_divide_up(result.meshes[mesh_id].meshlet_count, K_TASK_TG_SIZE);
    }
  }

  out_handle = model_gpu_resource_pool_.alloc(ModelGPUResources{
      .material_alloc = material_alloc,
      .static_draw_batch_alloc = draw_batch_alloc,
      .textures = std::move(out_tex_handles),
      .base_instance_datas = std::move(base_instance_datas),
      .meshes = std::move(result.meshes),
      .instance_id_to_node = instance_id_to_node,
      .totals =
          ModelGPUResources::Totals{
              .meshlets = static_cast<uint32_t>(result.meshlet_process_result.tot_meshlet_count),
              .vertices = static_cast<uint32_t>(result.vertices.size()),
              .instance_vertices = total_instance_vertices,
              .instance_meshlets = total_instance_meshlets,
              .task_cmd_count = task_cmd_count,
          },
  });
  return true;
}

GeometryBatch::Alloc MemeRenderer123::upload_geometry(
    [[maybe_unused]] GeometryBatchType type, const std::vector<DefaultVertex>& vertices,
    const std::vector<rhi::DefaultIndexT>& indices, const MeshletProcessResult& meshlets,
    std::span<Mesh> meshes) {
  ZoneScoped;
  auto& draw_batch = static_draw_batch_;
  ASSERT(!vertices.empty());
  ASSERT(!meshlets.meshlet_datas.empty());

  bool resized{};
  const auto vertex_alloc = draw_batch.vertex_buf.allocate(vertices.size(), resized);
  buffer_copy_mgr_.copy_to_buffer(vertices.data(), vertices.size() * sizeof(DefaultVertex),
                                  draw_batch.vertex_buf.get_buffer_handle(),
                                  vertex_alloc.offset * sizeof(DefaultVertex));

  OffsetAllocator::Allocation index_alloc{};
  if (!indices.empty()) {
    index_alloc = draw_batch.index_buf.allocate(indices.size(), resized);
    buffer_copy_mgr_.copy_to_buffer(indices.data(), indices.size() * sizeof(rhi::DefaultIndexT),
                                    draw_batch.index_buf.get_buffer_handle(),
                                    index_alloc.offset * sizeof(rhi::DefaultIndexT));
  }

  const auto meshlet_alloc = draw_batch.meshlet_buf.allocate(meshlets.tot_meshlet_count, resized);
  const auto meshlet_vertices_alloc =
      draw_batch.meshlet_vertices_buf.allocate(meshlets.tot_meshlet_verts_count, resized);
  const auto meshlet_triangles_alloc =
      draw_batch.meshlet_triangles_buf.allocate(meshlets.tot_meshlet_tri_count, resized);
  const auto mesh_alloc = draw_batch.mesh_buf.allocate(meshlets.meshlet_datas.size(), resized);

  size_t meshlet_offset{};
  size_t meshlet_triangles_offset{};
  size_t meshlet_vertices_offset{};
  size_t mesh_i{};

  // TODO: explicit staging buffer. this is writing all over the place and probably blowing
  // caches.
  for (const auto& meshlet_data : meshlets.meshlet_datas) {
    buffer_copy_mgr_.copy_to_buffer(meshlet_data.meshlets.data(),
                                    meshlet_data.meshlets.size() * sizeof(Meshlet),
                                    draw_batch.meshlet_buf.get_buffer_handle(),
                                    (meshlet_alloc.offset + meshlet_offset) * sizeof(Meshlet));
    meshlet_offset += meshlet_data.meshlets.size();

    buffer_copy_mgr_.copy_to_buffer(
        meshlet_data.meshlet_vertices.data(),
        meshlet_data.meshlet_vertices.size() * sizeof(uint32_t),
        draw_batch.meshlet_vertices_buf.get_buffer_handle(),
        (meshlet_vertices_alloc.offset + meshlet_vertices_offset) * sizeof(uint32_t));

    meshlet_vertices_offset += meshlet_data.meshlet_vertices.size();

    buffer_copy_mgr_.copy_to_buffer(
        meshlet_data.meshlet_triangles.data(),
        meshlet_data.meshlet_triangles.size() * sizeof(uint8_t),
        draw_batch.meshlet_triangles_buf.get_buffer_handle(),
        (meshlet_triangles_alloc.offset + meshlet_triangles_offset) * sizeof(uint8_t));

    meshlet_triangles_offset += meshlet_data.meshlet_triangles.size();

    MeshData d{
        .meshlet_base = meshlet_data.meshlet_base + meshlet_alloc.offset,
        .meshlet_count = static_cast<uint32_t>(meshlet_data.meshlets.size()),
        .meshlet_vertices_offset =
            meshlet_data.meshlet_vertices_offset + meshlet_vertices_alloc.offset,
        .meshlet_triangles_offset =
            meshlet_data.meshlet_triangles_offset + meshlet_triangles_alloc.offset,
        .vertex_base = vertex_alloc.offset,
        .center = meshes[mesh_i].center,
        .radius = meshes[mesh_i].radius,
    };
    buffer_copy_mgr_.copy_to_buffer(&d, sizeof(d), draw_batch.mesh_buf.get_buffer_handle(),
                                    (mesh_i + mesh_alloc.offset) * sizeof(MeshData));
    mesh_i++;
  }

  return GeometryBatch::Alloc{.vertex_alloc = vertex_alloc,
                              .index_alloc = index_alloc,
                              .meshlet_alloc = meshlet_alloc,
                              .mesh_alloc = mesh_alloc,
                              .meshlet_triangles_alloc = meshlet_triangles_alloc,
                              .meshlet_vertices_alloc = meshlet_vertices_alloc};
}

ModelInstanceGPUHandle MemeRenderer123::add_model_instance(const ModelInstance& model,
                                                           ModelGPUHandle model_gpu_handle) {
  ZoneScoped;
  auto* model_resources = model_gpu_resource_pool_.get(model_gpu_handle);
  ALWAYS_ASSERT(model_resources);
  auto& model_instance_datas = model_resources->base_instance_datas;
  auto& instance_id_to_node = model_resources->instance_id_to_node;
  std::vector<TRS> instance_transforms;
  instance_transforms.reserve(model_instance_datas.size());
  std::vector<InstanceData> instance_datas = {model_instance_datas.begin(),
                                              model_instance_datas.end()};
  std::vector<IndexedIndirectDrawCmd> cmds;
  if (!mesh_shaders_enabled_) cmds.reserve(instance_datas.size());

  ASSERT(instance_datas.size() == instance_id_to_node.size());
  all_model_data_.max_meshlets += model_resources->totals.meshlets;

  const InstanceDataMgr::Alloc instance_data_gpu_alloc = instance_data_mgr_.allocate(
      model_instance_datas.size(), model_resources->totals.instance_meshlets);
  all_model_data_.max_objects = instance_data_mgr_.max_seen_size();
  stats_.total_instance_meshlets += model_resources->totals.instance_meshlets;
  stats_.total_instance_vertices += model_resources->totals.instance_vertices;

  for (size_t i = 0; i < instance_datas.size(); i++) {
    auto node_i = instance_id_to_node[i];
    instance_transforms.push_back(model.global_transforms[node_i]);
    const auto& transform = model.global_transforms[node_i];
    const auto rot = transform.rotation;
    instance_datas[i].translation = transform.translation;
    instance_datas[i].rotation = glm::vec4{rot[0], rot[1], rot[2], rot[3]};
    instance_datas[i].scale = transform.scale;
    instance_datas[i].meshlet_vis_base += instance_data_gpu_alloc.meshlet_vis_alloc.offset;
    size_t mesh_id = model.mesh_ids[node_i];
    auto& mesh = model_resources->meshes[mesh_id];
    if (!mesh_shaders_enabled_) {
      cmds.push_back(IndexedIndirectDrawCmd{
          .index_count = mesh.index_count,
          .instance_count = 1,
          .first_index = static_cast<uint32_t>(
              (mesh.index_offset + model_resources->static_draw_batch_alloc.index_alloc.offset *
                                       sizeof(rhi::DefaultIndexT)) /
              sizeof(rhi::DefaultIndexT)),
          .vertex_offset = static_cast<int32_t>(
              mesh.vertex_offset_bytes +
              model_resources->static_draw_batch_alloc.vertex_alloc.offset * sizeof(DefaultVertex)),
          .first_instance =
              static_cast<uint32_t>(i + instance_data_gpu_alloc.instance_data_alloc.offset),
      });
    }
  }
  static_draw_batch_.task_cmd_count += model_resources->totals.task_cmd_count;

  stats_.total_instances += instance_datas.size();
  buffer_copy_mgr_.copy_to_buffer(
      instance_datas.data(), instance_datas.size() * sizeof(InstanceData),
      instance_data_mgr_.get_instance_data_buf(),
      instance_data_gpu_alloc.instance_data_alloc.offset * sizeof(InstanceData));
  if (!mesh_shaders_enabled_) {
    buffer_copy_mgr_.copy_to_buffer(
        cmds.data(), cmds.size() * sizeof(IndexedIndirectDrawCmd),
        instance_data_mgr_.get_draw_cmd_buf(),
        instance_data_gpu_alloc.instance_data_alloc.offset * sizeof(IndexedIndirectDrawCmd));
  }
  ASSERT(model_gpu_handle.is_valid());
  return model_instance_gpu_resource_pool_.alloc(ModelInstanceGPUResources{
      .instance_data_gpu_alloc = instance_data_gpu_alloc,
      .model_resources_handle = model_gpu_handle,
  });
}

void MemeRenderer123::free_instance(ModelInstanceGPUHandle handle) {
  auto* gpu_resources = model_instance_gpu_resource_pool_.get(handle);
  ASSERT(gpu_resources);
  if (!gpu_resources) {
    return;
  }
  instance_data_mgr_.free(gpu_resources->instance_data_gpu_alloc, curr_frame_idx_);
  auto* model_resources = model_gpu_resource_pool_.get(gpu_resources->model_resources_handle);
  static_draw_batch_.task_cmd_count -= model_resources->totals.task_cmd_count;
  stats_.total_instances -= model_resources->base_instance_datas.size();
  stats_.total_instance_meshlets -= model_resources->totals.instance_meshlets;
  stats_.total_instance_vertices -= model_resources->totals.instance_vertices;
  model_instance_gpu_resource_pool_.destroy(handle);
}

void MemeRenderer123::free_model(ModelGPUHandle handle) {
  auto* gpu_resources = model_gpu_resource_pool_.get(handle);
  ASSERT(gpu_resources);
  if (!gpu_resources) {
    return;
  }
  gpu_resources->textures.clear();
  materials_buf_.free(gpu_resources->material_alloc);
  static_draw_batch_.free(gpu_resources->static_draw_batch_alloc);
  model_gpu_resource_pool_.destroy(handle);
}

InstanceDataMgr::Alloc InstanceDataMgr::allocate(uint32_t element_count,
                                                 uint32_t meshlet_instance_count) {
  OffsetAllocator::Allocation meshlet_vis_buf_alloc{};
  if (renderer_.mesh_shaders_enabled()) {
    bool resized{};
    meshlet_vis_buf_alloc = meshlet_vis_buf_.allocate(meshlet_instance_count, resized);
  }
  return {.instance_data_alloc = allocate_instance_data(element_count),
          .meshlet_vis_alloc = meshlet_vis_buf_alloc};
}

void InstanceDataMgr::free(const Alloc& alloc, uint32_t frame_in_flight) {
  pending_frees_[frame_in_flight].push_back(alloc);
}

void InstanceDataMgr::flush_pending_frees(uint32_t curr_frame_in_flight, rhi::CmdEncoder* enc) {
  auto previous_frame_idx =
      curr_frame_in_flight == 0 ? frames_in_flight_ - 1 : curr_frame_in_flight - 1;
  for (const auto& allocs : pending_frees_[previous_frame_idx]) {
    const auto& alloc = allocs.instance_data_alloc;
    auto element_count = allocator_.allocationSize(alloc);
    enc->fill_buffer(instance_data_buf_.handle, alloc.offset * sizeof(InstanceData),
                     element_count * sizeof(InstanceData), 0xFFFFFFFF);
    if (!renderer_.mesh_shaders_enabled()) {
      enc->fill_buffer(draw_cmd_buf_.handle, alloc.offset * sizeof(IndexedIndirectDrawCmd),
                       element_count * sizeof(IndexedIndirectDrawCmd), 0xFFFFFFFF);
    }
    allocator_.free(alloc);
    curr_element_count_ -= element_count;

    if (renderer_.mesh_shaders_enabled()) {
      meshlet_vis_buf_.free(allocs.meshlet_vis_alloc);
    }
  }
  pending_frees_[previous_frame_idx].clear();
}

void InstanceDataMgr::ensure_buffer_space(size_t element_count) {
  if (element_count == 0) return;
  if (!instance_data_buf_.is_valid() ||
      device_.get_buf(instance_data_buf_)->size() < element_count * sizeof(InstanceData)) {
    auto new_buf = device_.create_buf_h({
        .storage_mode = rhi::StorageMode::Default,
        .usage = rhi::BufferUsage_Storage,
        .size = sizeof(InstanceData) * element_count,
        .name = "intance_data_buf",
    });

    if (instance_data_buf_.is_valid()) {
      buffer_copy_mgr_->copy_to_buffer(device_.get_buf(instance_data_buf_)->contents(),
                                       device_.get_buf(instance_data_buf_)->size(), new_buf.handle,
                                       0);
    }
    instance_data_buf_ = std::move(new_buf);
  }

  if (!renderer_.mesh_shaders_enabled()) {
    if (!draw_cmd_buf_.is_valid() ||
        device_.get_buf(draw_cmd_buf_)->size() < element_count * sizeof(IndexedIndirectDrawCmd)) {
      auto new_buf = device_.create_buf_h(rhi::BufferDesc{
          .storage_mode = rhi::StorageMode::Default,
          .usage = rhi::BufferUsage_Indirect,
          .size = sizeof(IndexedIndirectDrawCmd) * element_count,
          .name = "draw_indexed_indirect_cmd_buf",
      });
      if (draw_cmd_buf_.is_valid()) {
        buffer_copy_mgr_->copy_to_buffer(device_.get_buf(draw_cmd_buf_)->contents(),
                                         device_.get_buf(draw_cmd_buf_)->size(), new_buf.handle, 0);
      }
      draw_cmd_buf_ = std::move(new_buf);
    }
  }
}

void MemeRenderer123::init_imgui() {
  ZoneScoped;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  auto path = (resource_dir_ / "fonts" / "ComicMono.ttf");
  ALWAYS_ASSERT(std::filesystem::exists(path));

  const float sizes[] = {16.f, 32.f, 48.f};
  for (auto size : sizes) {
    auto* font = io.Fonts->AddFontFromFileTTF(path.string().c_str(), size, nullptr,
                                              io.Fonts->GetGlyphRangesDefault());
    add_font(font, size);
  }

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendRendererName = "imgui_impl_memes";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  ImGui_ImplGlfw_InitForOther(window_->get_handle(), true);
}

void MemeRenderer123::shutdown_imgui() { ZoneScoped; }

void MemeRenderer123::on_imgui() {
  ZoneScoped;
  if (ImGui::TreeNodeEx("Config")) {
    ImGui::Text("mesh shaders enabled: %d", mesh_shaders_enabled_);
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Total possible vertices drawn: %d\nTotal objects: %u",
                stats_.total_instance_vertices, stats_.total_instances);
    ImGui::Text("Total total instance meshlets: %d", stats_.total_instance_meshlets);
    ImGui::Text("GPU Frame Time MS %.3f", gpu_frame_time_last_ms_);

    MeshletDrawStats stats{};
    constexpr int frames_ago = 2;
    auto* readback_buf =
        device_->get_buf(out_counts_buf_readback_[get_frames_ago_idx(frames_ago)].handle);
    stats = *(MeshletDrawStats*)readback_buf->contents();
    size_t tot_drawn_meshlets = stats.meshlets_drawn_early + stats.meshlets_drawn_late;
    ImGui::Text("Meshlets drawn %1d frames ago: %5zu of %5u (%.2f %%)\nEarly: %7u\tLate %7u",
                frames_ago, tot_drawn_meshlets, stats_.total_instance_meshlets,
                tot_drawn_meshlets == 0
                    ? 0
                    : (float)tot_drawn_meshlets / (float)stats_.total_instance_meshlets * 100.f,
                stats.meshlets_drawn_early, stats.meshlets_drawn_late);
    ImGui::TreePop();
  }

  ImGui::Text("Culling paused: %d", culling_paused_);
  ImGui::Text("Culling enabled: %d", culling_enabled_);

  auto dp_dims = device_->get_tex(depth_pyramid_tex_)->desc().dims;
  auto mip_levels = math::get_mip_levels(dp_dims.x, dp_dims.y);
  ImGui::SliderInt("mip view", &debug_view_mip_, 0, mip_levels - 1);

  if (ImGui::TreeNodeEx("Device", ImGuiTreeNodeFlags_DefaultOpen)) {
    device_->on_imgui();
    if (ImGui::TreeNode("GPU Buffers")) {
      static std::vector<rhi::Buffer*> buffers;
      buffers.clear();
      device_->get_all_buffers(buffers);
      std::ranges::sort(
          buffers, [](rhi::Buffer* a, rhi::Buffer* b) { return a->desc().size > b->desc().size; });
      for (auto& b : buffers) {
        ImGui::Text("%s: %.1f mb", b->desc().name, b->desc().size / 1024.f / 1024.f);
      }
      ImGui::TreePop();
    }
    ImGui::TreePop();
  }
  if (ImGui::TreeNodeEx("Culling")) {
    ImGui::Checkbox("Culling paused", &culling_paused_);
    ImGui::Checkbox("Culling enabled", &culling_enabled_);
    ImGui::Checkbox("Meshlet frustum culling enabled", &meshlet_frustum_culling_enabled_);
    ImGui::Checkbox("Meshlet cone culling enabled", &meshlet_cone_culling_enabled_);
    ImGui::Checkbox("Meshlet occlusion culling enabled", &meshlet_occlusion_culling_enabled_);
    ImGui::TreePop();
  }
}

glm::mat4 MemeRenderer123::get_proj_matrix(float fov) {
  auto win_dims = window_->get_window_size();
  return infinite_perspective_proj(glm::radians(fov), (float)win_dims.x / win_dims.y, k_z_near);
}

void MemeRenderer123::set_cull_data_and_globals(const RenderArgs& args) {
  glm::mat4 proj_mat = get_proj_matrix();
  GlobalData global_data{
      .vp = proj_mat * args.view_mat,
      .view = args.view_mat,
      .proj = proj_mat,
      .render_mode = (uint32_t)debug_render_mode_,
      .frame_num = (uint32_t)frame_num_,
      .camera_pos = glm::vec4{args.camera_pos, 0},
  };
  {
    auto [buf, offset, write_ptr] =
        frame_gpu_upload_allocator_.alloc(sizeof(GlobalData), &global_data);
    frame_globals_buf_info_.buf = buf;
    frame_globals_buf_info_.idx = device_->get_buf(buf)->bindless_idx();
    frame_globals_buf_info_.offset_bytes = offset;
  }
  {
    // set cull data buf
    CullData cull_data{};
    const glm::mat4 projection_transpose = glm::transpose(proj_mat);
    const auto normalize_plane = [](const glm::vec4& p) {
      const auto n = glm::vec3(p);
      const float inv_len = 1.0f / glm::length(n);
      return glm::vec4(n * inv_len, p.w * inv_len);
    };
    const glm::vec4 frustum_x =
        normalize_plane(projection_transpose[0] + projection_transpose[3]);  // x + w < 0
    const glm::vec4 frustum_y =
        normalize_plane(projection_transpose[1] + projection_transpose[3]);  // y + w < 0
    cull_data.frustum[0] = frustum_x.x;
    cull_data.frustum[1] = frustum_x.z;
    cull_data.frustum[2] = frustum_y.y;
    cull_data.frustum[3] = frustum_y.z;
    cull_data.z_near = k_z_near;
    cull_data.z_far = k_z_far;
    const auto& dp_tex_desc = device_->get_tex(depth_pyramid_tex_)->desc();
    cull_data.pyramid_width = dp_tex_desc.dims.x;
    cull_data.pyramid_height = dp_tex_desc.dims.y;
    cull_data.pyramid_mip_count = dp_tex_desc.mip_levels;
    cull_data.p00 = proj_mat[0][0];
    cull_data.p11 = proj_mat[1][1];
    cull_data.paused = culling_paused_;

    auto [buf, offset, write_ptr] = frame_gpu_upload_allocator_.alloc(sizeof(CullData), &cull_data);
    frame_cull_data_buf_info_.buf = buf;
    frame_cull_data_buf_info_.idx = device_->get_buf(buf)->bindless_idx();
    frame_cull_data_buf_info_.offset_bytes = offset;
  }
}

bool MemeRenderer123::on_key_event(int key, int action, int mods) {
  ZoneScoped;
  bool is_shift = mods & GLFW_MOD_SHIFT;
  if (action == GLFW_PRESS || action == GLFW_REPEAT) {
    if (key == GLFW_KEY_P && is_shift) {
      culling_paused_ = !culling_paused_;
      return true;
    }
    if (key == GLFW_KEY_E && is_shift) {
      culling_enabled_ = !culling_enabled_;
      return true;
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_CONTROL) {
      if (mods & GLFW_MOD_SHIFT) {
        if (debug_render_mode_ == DebugRenderMode::None) {
          debug_render_mode_ = (DebugRenderMode)((int)DebugRenderMode::Count - 1);
        } else {
          debug_render_mode_ =
              (DebugRenderMode)(((int)debug_render_mode_ - 1) % (int)DebugRenderMode::Count);
        }
      } else {
        debug_render_mode_ =
            (DebugRenderMode)(((int)debug_render_mode_ + 1) % (int)DebugRenderMode::Count);
      }
      return true;
    }

    if (action == GLFW_PRESS && key == GLFW_KEY_R && mods & GLFW_MOD_CONTROL) {
      LINFO("Recompiling shaders.");
      shader_mgr_->recompile_shaders();
      return true;
    }
  }
  return false;
}

std::string MemeRenderer123::get_next_tex_upload_name() {
  return "ext_tex_" + std::to_string(tex_upload_i_++);
}

void MemeRenderer123::recreate_swapchain_sized_textures() {
  auto main_size = window_->get_window_size();
  {  // depth pyramid
    auto size = glm::uvec3{prev_pow2(main_size.x), prev_pow2(main_size.y), 1};
    uint32_t mip_levels = math::get_mip_levels(size.x, size.y);
    if (depth_pyramid_tex_.is_valid()) {
      auto* existing = device_->get_tex(depth_pyramid_tex_);
      if (existing->desc().dims == size) {
        return;
      }
    }
    for (auto& v : depth_pyramid_tex_.views) {
      device_->destroy(depth_pyramid_tex_.handle, v);
    }
    depth_pyramid_tex_.views.clear();
    depth_pyramid_tex_ = TexAndViewHolder{device_->create_tex_h(rhi::TextureDesc{
        .format = rhi::TextureFormat::R32float,
        .usage = (rhi::TextureUsage)(rhi::TextureUsageStorage | rhi::TextureUsageShaderWrite),
        .dims = size,
        .mip_levels = mip_levels,
        .bindless = true,
        .name = "depth_pyramid_tex"})};
    depth_pyramid_tex_.views.reserve(mip_levels);
    for (size_t i = 0; i < mip_levels; i++) {
      depth_pyramid_tex_.views.emplace_back(
          device_->create_tex_view(depth_pyramid_tex_.handle, i, 1, 0, 1));
    }
  }
}

MemeRenderer123::~MemeRenderer123() {
  shader_mgr_->shutdown();
  rg_.shutdown();

  {  // imgui
    imgui_renderer_->shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
}

TexAndViewHolder::~TexAndViewHolder() {
  for (auto v : views) {
    context->destroy(handle, v);
  }
  views.clear();
}

[[nodiscard]] bool InstanceDataMgr::has_pending_frees(uint32_t curr_frame_in_flight) const {
  return !pending_frees_[curr_frame_in_flight == 0 ? frames_in_flight_ - 1
                                                   : curr_frame_in_flight - 1]
              .empty();
}

MemeRenderer123::MemeRenderer123(const CreateInfo& cinfo)
    : device_(cinfo.device),
      frame_gpu_upload_allocator_(device_),
      buffer_copy_mgr_(device_, frame_gpu_upload_allocator_),
      materials_buf_(*device_, buffer_copy_mgr_,
                     rhi::BufferDesc{.storage_mode = rhi::StorageMode::Default,
                                     .usage = rhi::BufferUsage_Storage,
                                     .size = k_max_materials * sizeof(M4Material),
                                     .bindless = true,
                                     .name = "all materials buf"},
                     sizeof(M4Material)),
      instance_data_mgr_(*device_, &buffer_copy_mgr_, device_->get_info().frames_in_flight, *this),
      static_draw_batch_(GeometryBatchType::Static, *device_, buffer_copy_mgr_,
                         GeometryBatch::CreateInfo{
                             .initial_vertex_capacity = 10'000'000,
                             .initial_index_capacity = 10'000'000,
                             .initial_meshlet_capacity = 1'000'000,
                             .initial_mesh_capacity = 100'000,
                             .initial_meshlet_triangle_capacity = 10'000'000,
                             .initial_meshlet_vertex_capacity = 10'000'000,
                         }),
      mesh_shaders_enabled_(cinfo.mesh_shaders_enabled) {
  ZoneScoped;
  window_ = cinfo.window;
  resource_dir_ = cinfo.resource_dir;
  config_file_path_ = cinfo.config_file_path;
  swapchain_ = cinfo.swapchain;
  {
    constexpr const char* key_mesh_shaders_enabled = "mesh_shaders_enabled";
    std::ifstream file(config_file_path_);
    if (file.is_open()) {
      std::string line;
      while (std::getline(file, line)) {
        auto kv = core::split_string_at_first(line, '=');
        if (kv.first == key_mesh_shaders_enabled) {
          mesh_shaders_enabled_ = kv.second == "1";
        }
      }
    } else {
      // write the default config.
      std::ofstream file(config_file_path_);
      if (file.is_open()) {
        file << key_mesh_shaders_enabled << '=' << mesh_shaders_enabled_ << '\n';
      } else {
        ASSERT(0 && "Failed to open config file for writing");
        LINFO("Failed to open config file for writing: {}", config_file_path_.string());
      }
    }
  }

  // TODO: renderer shouldn't own this
  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(device_);

  {
    samplers_.emplace_back(device_->create_sampler_h({
        .min_filter = rhi::FilterMode::Nearest,
        .mag_filter = rhi::FilterMode::Nearest,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::Repeat,
    }));

    samplers_.emplace_back(device_->create_sampler_h({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::Repeat,
    }));
    samplers_.emplace_back(device_->create_sampler_h({
        .min_filter = rhi::FilterMode::Nearest,
        .mag_filter = rhi::FilterMode::Nearest,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::ClampToEdge,
    }));
  }

  {
    test2_pso_ = shader_mgr_->create_graphics_pipeline({
        .shaders = {{{"basic_indirect", ShaderType::Vertex},
                     {"basic_indirect", ShaderType::Fragment}}},
    });
    test_task_pso_ = shader_mgr_->create_graphics_pipeline({
        .shaders = {{{"forward_meshlet", ShaderType::Task},
                     {"forward_meshlet", ShaderType::Mesh},
                     {"forward_meshlet", ShaderType::Fragment}}},
    });
    tex_only_pso_ = shader_mgr_->create_graphics_pipeline({
        .shaders = {{{"fullscreen_quad", ShaderType::Vertex}, {"tex_only", ShaderType::Fragment}}},
    });

    draw_cull_pso_ = shader_mgr_->create_compute_pipeline({"draw_cull"});
    reset_counts_buf_pso_ = shader_mgr_->create_compute_pipeline({"test_clear_cnt_buf"});
    depth_reduce_pso_ = shader_mgr_->create_compute_pipeline({"depth_reduce/depth_reduce"});
    shade_pso_ = shader_mgr_->create_compute_pipeline({"shade"});
  }

  rg_.init(device_);

  init_imgui();
  imgui_renderer_.emplace(*shader_mgr_, device_);

  recreate_swapchain_sized_textures();

  for (size_t i = 0; i < device_->get_info().frames_in_flight; i++) {
    // TODO: render graph this?
    out_counts_buf_[i] = device_->create_buf_h(rhi::BufferDesc{
        .storage_mode = rhi::StorageMode::GPUOnly,
        .usage = rhi::BufferUsage_Storage,
        .size = sizeof(MeshletDrawStats),
        .name = "out_counts_buf",
    });
    out_counts_buf_readback_[i] = device_->create_buf_h(rhi::BufferDesc{
        .storage_mode = rhi::StorageMode::CPUAndGPU,
        .size = sizeof(MeshletDrawStats),
        .name = "out_counts_buf_readback",
    });
  }

  for (size_t i = 0; i < device_->get_info().frames_in_flight; i++) {
    query_pools_[i] = device_->create_query_pool_h(
        rhi::QueryPoolDesc{.count = k_query_count, .name = "my_query_pool_" + std::to_string(i)});
    query_resolve_bufs_[i] = device_->create_buf_h(rhi::BufferDesc{
        .storage_mode = rhi::StorageMode::CPUAndGPU,
        .size = sizeof(uint64_t) * k_query_count,
        .name = "query_resolve_buf",
    });
  }
}

InstanceDataMgr::InstanceDataMgr(rhi::Device& device, BufferCopyMgr* buffer_copy_mgr,
                                 uint32_t frames_in_flight, MemeRenderer123& renderer)
    : allocator_(0),
      meshlet_vis_buf_(device, *buffer_copy_mgr,
                       {.storage_mode = rhi::StorageMode::GPUOnly,
                        .usage = rhi::BufferUsage_Storage,
                        .name = "instance meshlet vis buf"},
                       sizeof(uint32_t)),
      buffer_copy_mgr_(buffer_copy_mgr),
      frames_in_flight_(frames_in_flight),
      device_(device),
      renderer_(renderer) {}

OffsetAllocator::Allocation InstanceDataMgr::allocate_instance_data(uint32_t element_count) {
  const OffsetAllocator::Allocation alloc = allocator_.allocate(element_count);
  if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
    auto old_capacity = allocator_.capacity();
    auto new_capacity = std::max(old_capacity * 2, element_count);
    allocator_.grow(new_capacity - old_capacity);
    ASSERT(new_capacity <= allocator_.capacity());
    ensure_buffer_space(new_capacity);
    return allocate_instance_data(element_count);
  }
  ensure_buffer_space(allocator_.capacity());
  curr_element_count_ += element_count;
  max_seen_size_ = std::max<uint32_t>(max_seen_size_, alloc.offset + element_count);
  return alloc;
}

}  // namespace gfx
