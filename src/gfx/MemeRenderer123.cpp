#include "MemeRenderer123.hpp"

#include <algorithm>
#include <glm/ext/vector_integer.hpp>
#include <string>
#include <tracy/Tracy.hpp>

#include "GLFW/glfw3.h"
#include "ResourceManager.hpp"
#include "UI.hpp"
#include "Window.hpp"
#include "core/CVar.hpp"
#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/MathUtil.hpp"
#include "core/Util.hpp"
#include "gfx/ModelLoader.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/renderer/GBufferRenderer.hpp"
#include "gfx/renderer/RendererCVars.hpp"
#include "gfx/renderer/TaskCmdBufRgIds.hpp"
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
#include "hlsl/shared_globals.h"
#include "hlsl/shared_indirect.h"
#include "hlsl/shared_mesh_data.h"
#include "hlsl/shared_meshlet_draw_stats.hlsli"
#include "hlsl/shared_task_cmd.h"
#include "hlsl/shared_test_clear_buf.h"
#include "hlsl/shared_tex_only.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"
#include "ktx.h"

namespace TENG_NAMESPACE {

using namespace rhi;

namespace {

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

namespace {

DebugRenderMode clamped_debug_render_mode() {
  int m = renderer_cv::debug_render_mode.get();
  const int max_mode = static_cast<int>(DebugRenderMode::Count) - 1;
  m = std::clamp(m, 0, max_mode);
  return static_cast<DebugRenderMode>(m);
}

}  // namespace

void MemeRenderer123::render([[maybe_unused]] const RenderArgs& args) {
  ZoneScoped;
  {
    if (frame_num_ >= device_->get_info().frames_in_flight) {
      // 3 frames in flight ago is ready to be read back to the cpu, since we waited on fence for
      // it.
      uint64_t timestamps[k_query_count]{};
      device_->resolve_query_data(query_pools_[curr_frame_idx_].handle, 0, k_query_count,
                                  timestamps);
      if (timestamps[0] != 0 && timestamps[1] != 0 && timestamps[1] > timestamps[0]) {
        auto ms_per_tick = (1.0 / device_->get_info().timestamp_frequency) * 1000.0;
        auto delta_ticks = timestamps[1] - timestamps[0];
        auto delta_ms = delta_ticks * ms_per_tick;
        stats_.gpu_frame_time_last_ms = delta_ms;
        stats_.avg_gpu_frame_time =
            glm::mix(stats_.avg_gpu_frame_time, stats_.gpu_frame_time_last_ms, 0.05f);
      }
    }
  }
  shader_mgr_->replace_dirty_pipelines();
  // clear_render_views();

  indirect_cmd_buf_ids_.clear();

  set_cull_data_and_globals(args);
  static glm::uvec2 prev_fb_size{};
  auto curr_fb_size = window_->get_window_size();
  if (prev_fb_size != curr_fb_size) {
    recreate_swapchain_sized_textures();
    prev_fb_size = curr_fb_size;
  }
  add_render_graph_passes(args);
  device_->acquire_next_swapchain_image(swapchain_);
  rg_.bake(window_->get_window_size(), renderer_cv::developer_render_graph_verbose.get() != 0);
  static std::vector<rhi::CmdEncoder*> wait_for_encoders;
  wait_for_encoders.clear();
  {
    auto* enc = device_->begin_cmd_encoder(rhi::QueueType::Copy);
    if (!buffer_copy_mgr_.get_copies().empty()) {
      for (const auto& copy : buffer_copy_mgr_.get_copies()) {
        enc->barrier(copy.src_buf, PipelineStage::AllCommands,
                     AccessFlags::AnyRead | AccessFlags::AnyWrite, PipelineStage::AllTransfer,
                     AccessFlags::TransferRead);
        enc->barrier(copy.dst_buf, PipelineStage::AllCommands,
                     AccessFlags::AnyRead | AccessFlags::AnyWrite, PipelineStage::AllTransfer,
                     AccessFlags::TransferWrite);
        enc->copy_buffer_to_buffer(copy.src_buf, copy.src_offset, copy.dst_buf, copy.dst_offset,
                                   copy.size);
        enc->barrier(copy.dst_buf, PipelineStage::AllTransfer, AccessFlags::TransferWrite,
                     copy.dst_stage, copy.dst_access);
      }
      buffer_copy_mgr_.clear_copies();
    }
    enc->end_encoding();
    wait_for_encoders.push_back(enc);
  }

  {
    auto* enc = device_->begin_cmd_encoder();
    for (auto* wait_for_enc : wait_for_encoders) {
      device_->cmd_encoder_wait_for(wait_for_enc, enc);
    }

    if (imgui_renderer_->has_dirty_textures() || !pending_texture_uploads_.empty()) {
      flush_pending_texture_uploads(enc);
    }
    rg_.execute(enc);
    enc->end_encoding();
  }

  device_->submit_frame();
  frame_num_++;
  curr_frame_idx_ = frame_num_ % device_->get_info().frames_in_flight;
  frame_gpu_upload_allocator_.set_frame_idx(curr_frame_idx_);
}

uint32_t MemeRenderer123::get_bindless_idx(const rhi::BufferHandleHolder& buf) const {
  return device_->get_buf(buf)->bindless_idx();
}

void MemeRenderer123::add_render_graph_passes(const RenderArgs&) {
  ZoneScoped;
  auto instance_data_id =
      rg_.import_external_buffer(static_instance_mgr_.get_instance_data_buf(), "instance_data_buf");
  auto indirect_buffer_id =
      rg_.import_external_buffer(static_instance_mgr_.get_draw_cmd_buf(), "indirect_buffer");
  {
    if (static_instance_mgr_.has_pending_frees(curr_frame_idx_)) {
      auto& p = rg_.add_transfer_pass("free_instance_data");
      p.write_buf(instance_data_id, rhi::PipelineStage::AllTransfer);
      p.set_ex([this](rhi::CmdEncoder* enc) {
        static_instance_mgr_.flush_pending_frees(curr_frame_idx_, enc);
      });
    }
  }
  std::vector<RenderViewId> view_ids{main_render_view_id_};
  for (auto& shadow_map_render_view : shadow_map_render_views_) {
    view_ids.emplace_back(shadow_map_render_view);
  }
  std::vector<RGResourceId> out_draw_count_ids_early(render_views_.size());
  std::vector<RGResourceId> out_draw_count_ids_late(render_views_.size());
  TaskCmdBufRgTable task_cmd_buf_rg_ids(render_views_.size());

  std::vector<RGResourceId> final_depth_pyramid_ids(render_views_.size());
  std::vector<RGResourceId> draw_cmd_count_buf_ids(render_views_.size());

  for (size_t vid = 0; vid < render_views_.size(); vid++) {
    draw_cmd_count_buf_ids[vid] =
        rg_.create_buffer({.size = sizeof(uint32_t) * static_cast<size_t>(DrawCullPhase::Count)},
                          "draw_cmd_count_buf_view");
  }
  {
    auto& p = rg_.add_transfer_pass("clear_draw_cmd_count_bufs");
    for (size_t vid = 0; vid < render_views_.size(); vid++) {
      draw_cmd_count_buf_ids[vid] =
          p.write_buf(draw_cmd_count_buf_ids[vid], rhi::PipelineStage::AllTransfer);
    }
    p.set_ex([this, draw_cmd_count_buf_ids](rhi::CmdEncoder* enc) {
      for (size_t vid = 0; vid < render_views_.size(); vid++) {
        enc->fill_buffer(rg_.get_buf(draw_cmd_count_buf_ids[vid]), 0, sizeof(uint32_t) * 2, 0);
      }
    });
  }
  auto add_draw_cull_pass = [this, instance_data_id, &view_ids, &final_depth_pyramid_ids,
                             &draw_cmd_count_buf_ids](
                                DrawCullPhase phase, TaskCmdBufRgTable& task_cmd_buf_rg_ids,
                                std::vector<RGResourceId>& out_draw_count_ids) {
    const bool late = phase == DrawCullPhase::Late;
    struct BufferClear {
      RGResourceId id;
      rhi::BufferHandle buf;
      size_t size_bytes;
      uint32_t fill_value;
    };

    std::vector<BufferClear> instance_vis_clear_ids;
    std::vector<RGResourceId> instance_vis_rg_ids;
    instance_vis_rg_ids.reserve(view_ids.size());

    auto& prep_meshlets_pass =
        rg_.add_compute_pass(late ? "meshlet_draw_cull_late" : "meshlet_draw_cull_early");
    if (static_instance_mgr_.has_pending_frees(curr_frame_idx_)) {
      prep_meshlets_pass.read_buf(instance_data_id, rhi::PipelineStage::ComputeShader);
    }

    if (renderer_cv::culling_object_occlusion.get() != 0) {
      const uint32_t max_draws = static_instance_mgr_.stats().max_instance_data_count;
      const size_t required_instance_vis_buf_size =
          static_cast<size_t>(max_draws) * sizeof(uint32_t);
      for (auto view_id : view_ids) {
        auto& render_view = get_render_view(view_id);
        auto* vis_buf = device_->get_buf(render_view.instance_vis_buf);
        bool created_or_resized = false;
        if (!vis_buf || vis_buf->size() < required_instance_vis_buf_size) {
          render_view.instance_vis_buf =
              device_->create_buf_h({.usage = BufferUsage::Storage,
                                     .size = required_instance_vis_buf_size,
                                     .name = "instance_vis_buf"});
          created_or_resized = true;
          vis_buf = device_->get_buf(render_view.instance_vis_buf);
        }
        RGResourceId vis_rg_id =
            rg_.import_external_buffer(render_view.instance_vis_buf, "instance_vis_buf");
        instance_vis_rg_ids.push_back(vis_rg_id);
        if (created_or_resized) {
          instance_vis_clear_ids.push_back({.id = vis_rg_id,
                                            .buf = render_view.instance_vis_buf.handle,
                                            .size_bytes = required_instance_vis_buf_size,
                                            .fill_value = 1});
        }
      }
    }

    for (size_t view_i = 0; view_i < view_ids.size(); ++view_i) {
      auto view_id = view_ids[view_i];
      if (late) {
        // Late draw_cull samples the pyramid for object and/or meshlet occlusion; the graph must
        // depend on depth_reduce finishing (object-only occlusion previously omitted this).
        const bool depth_pyramid_read_by_draw_cull =
            (renderer_cv::culling_object_occlusion.get() != 0 ||
             renderer_cv::culling_meshlet_occlusion.get() != 0) &&
            renderer_cv::culling_enabled.get() != 0 &&
            renderer_cv::pipeline_mesh_shaders.get() != 0 && view_id == main_render_view_id_;
        if (depth_pyramid_read_by_draw_cull) {
          prep_meshlets_pass.read_tex(final_depth_pyramid_ids[(int)view_id],
                                      rhi::PipelineStage::ComputeShader);
        }
      }
      if (!instance_vis_rg_ids.empty()) {
        if (late) {
          prep_meshlets_pass.write_buf(instance_vis_rg_ids[view_i],
                                       rhi::PipelineStage::ComputeShader);
        }
      }

      if (renderer_cv::pipeline_mesh_shaders.get() != 0) {
        auto& out_draw_count_id = out_draw_count_ids[(int)view_id];
        if (renderer_cv::culling_paused.get() == 0) {
          out_draw_count_id =
              prep_meshlets_pass.rw_buf(out_draw_count_id, rhi::PipelineStage::ComputeShader);
        } else {
          prep_meshlets_pass.write_buf(out_draw_count_id, rhi::PipelineStage::ComputeShader);
        }
      }
      draw_cmd_count_buf_ids[(int)view_id] = prep_meshlets_pass.rw_buf(
          draw_cmd_count_buf_ids[(int)view_id], rhi::PipelineStage::ComputeShader);
    }

    if (!instance_vis_clear_ids.empty() && phase == DrawCullPhase::Early) {
      auto& p = rg_.add_transfer_pass("clear_instance_vis");
      for (const auto& clear : instance_vis_clear_ids) {
        p.write_buf(clear.id, rhi::PipelineStage::AllTransfer);
      }
      p.set_ex([instance_vis_clear_ids](rhi::CmdEncoder* enc) {
        for (const auto& clear : instance_vis_clear_ids) {
          enc->fill_buffer(clear.buf, 0, clear.size_bytes, clear.fill_value);
        }
      });
    }

    for (auto view_id : view_ids) {
      auto& view_handles = task_cmd_buf_rg_ids[(int)view_id].phase(phase);
      for (size_t alpha_mask_type = 0; alpha_mask_type < AlphaMaskType::Count; alpha_mask_type++) {
        if (static_draw_batch_.get_stats().vertex_count > 0) {
          view_handles[static_cast<AlphaMaskType>(alpha_mask_type)] = rg_.create_buffer(
              {.size = static_draw_batch_.task_cmd_count * sizeof(TaskCmd), .defer_reuse = true},
              late ? (alpha_mask_type == 0 ? "task_cmd_buf_late_0" : "task_cmd_buf_late_1")
                   : (alpha_mask_type == 0 ? "task_cmd_buf_early_0" : "task_cmd_buf_early_1"));
          prep_meshlets_pass.write_buf(view_handles[static_cast<AlphaMaskType>(alpha_mask_type)],
                                       rhi::PipelineStage::ComputeShader);
        }
      }
    }

    prep_meshlets_pass.set_ex([this, task_cmd_buf_rg_ids, out_draw_count_ids, view_ids, phase,
                               draw_cmd_count_buf_ids](rhi::CmdEncoder* enc) {
      if (!static_instance_mgr_.has_draws()) {
        return;
      }
      enc->bind_pipeline(draw_cull_pso_);
      if (renderer_cv::culling_paused.get() == 0) {
        // Prepare array of ViewCullSetups to be uploaded
        auto view_cull_setup_alloc =
            frame_gpu_upload_allocator_.alloc(view_ids.size() * sizeof(ViewCullSetup));
        auto* view_cull_setups = static_cast<ViewCullSetup*>(view_cull_setup_alloc.write_ptr);

        for (size_t i = 0; i < view_ids.size(); i++) {
          auto view_id = view_ids[i];
          const auto& task_cmd_buf_rg_handles = task_cmd_buf_rg_ids[(int)view_id].phase(phase);
          auto task_cmd_buf_opaque_handle =
              rg_.get_buf(task_cmd_buf_rg_handles[AlphaMaskType::Opaque]);
          auto task_cmd_buf_alpha_test_handle =
              rg_.get_buf(task_cmd_buf_rg_handles[AlphaMaskType::Mask]);
          auto out_draw_count_buf_rg_handle = out_draw_count_ids[(int)view_id];
          const auto& render_view_data = get_render_view(view_id);

          view_cull_setups[i].view_data_buf_idx = render_view_data.data_buf_info.idx;
          view_cull_setups[i].view_data_buf_offset_bytes =
              render_view_data.data_buf_info.offset_bytes;
          view_cull_setups[i].cull_data_idx = render_view_data.cull_data_buf_info.idx;
          view_cull_setups[i].cull_data_offset_bytes =
              render_view_data.cull_data_buf_info.offset_bytes;
          view_cull_setups[i].task_cmd_buf_idx_opaque =
              device_->get_buf(task_cmd_buf_opaque_handle)->bindless_idx();
          view_cull_setups[i].task_cmd_buf_alpha_test_idx =
              device_->get_buf(task_cmd_buf_alpha_test_handle)->bindless_idx();
          view_cull_setups[i].draw_cnt_buf_idx =
              device_->get_buf(rg_.get_buf(out_draw_count_buf_rg_handle))->bindless_idx();

          auto* instance_vis_buf = device_->get_buf(render_view_data.instance_vis_buf);
          if (instance_vis_buf) {
            view_cull_setups[i].instance_vis_buf_idx = instance_vis_buf->bindless_idx();
          }
          view_cull_setups[i].pass = static_cast<uint32_t>(phase);
          view_cull_setups[i].depth_pyramid_tex_idx =
              phase == DrawCullPhase::Late
                  ? device_->get_tex(render_view_data.depth_pyramid_tex)->bindless_idx()
                  : UINT32_MAX;
          view_cull_setups[i].draw_cmd_count_buf_idx =
              device_->get_buf(rg_.get_buf(draw_cmd_count_buf_ids[(int)view_id]))->bindless_idx();
          view_cull_setups[i].flags = 0;
          if (view_id == main_render_view_id_ &&
              renderer_cv::culling_meshlet_occlusion.get() != 0 &&
              renderer_cv::culling_enabled.get() != 0 &&
              renderer_cv::pipeline_mesh_shaders.get() != 0) {
            view_cull_setups[i].flags |= MESHLET_OCCLUSION_CULL_ENABLED_BIT;
          }
          if (renderer_cv::culling_object_occlusion.get() != 0 && view_id == main_render_view_id_ &&
              renderer_cv::culling_enabled.get() != 0 &&
              renderer_cv::pipeline_mesh_shaders.get() != 0) {
            view_cull_setups[i].flags |= OBJECT_OCCLUSION_CULL_ENABLED_BIT;
          }
        }

        DrawCullPC pc{
            .view_cull_setup_buf_idx = get_bindless_idx(view_cull_setup_alloc.buf),
            .view_cull_setup_count = static_cast<uint32_t>(view_ids.size()),
            .view_cull_setup_buf_offset_bytes = view_cull_setup_alloc.offset,
            .instance_data_buf_idx =
                device_->get_buf(static_instance_mgr_.get_instance_data_buf())->bindless_idx(),
            .materials_buf_idx = materials_buf_.get_buffer()->bindless_idx(),
            .mesh_data_buf_idx = static_draw_batch_.mesh_buf.get_buffer()->bindless_idx(),
            .max_draws = static_instance_mgr_.stats().max_instance_data_count,
            .culling_enabled = renderer_cv::culling_enabled.get() != 0,
        };

        enc->push_constants(&pc, sizeof(pc));
        enc->dispatch_compute(glm::uvec3{align_divide_up(pc.max_draws, 64ull), 1, 1},
                              glm::uvec3{64, 1, 1});
      }
    });
  };

  if (renderer_cv::pipeline_mesh_shaders.get() != 0) {
    for (auto view_id : view_ids) {
      out_draw_count_ids_early[(int)view_id] = rg_.create_buffer(
          {.size = sizeof(uint32_t) * 3 * AlphaMaskType::Count}, "out_draw_count_buf_early");
      out_draw_count_ids_late[(int)view_id] = rg_.create_buffer(
          {.size = sizeof(uint32_t) * 3 * AlphaMaskType::Count}, "out_draw_count_buf_late");
    }
    if (renderer_cv::culling_paused.get() == 0) {
      auto& clear_bufs_pass = rg_.add_compute_pass("clear_bufs");
      for (auto view_id : view_ids) {
        clear_bufs_pass.write_buf(out_draw_count_ids_early[(int)view_id],
                                  rhi::PipelineStage::ComputeShader);
        clear_bufs_pass.write_buf(out_draw_count_ids_late[(int)view_id],
                                  rhi::PipelineStage::ComputeShader);
      }

      clear_bufs_pass.set_ex([this, out_draw_count_ids_early, out_draw_count_ids_late,
                              view_ids](rhi::CmdEncoder* enc) {
        enc->bind_pipeline(reset_counts_buf_pso_);
        for (auto view_id : view_ids) {
          RGResourceId early_late_ids[2]{out_draw_count_ids_early[(int)view_id],
                                         out_draw_count_ids_late[(int)view_id]};
          for (auto id : early_late_ids) {
            TestClearBufPC pc{
                .buf_idx = device_->get_buf(rg_.get_buf(id))->bindless_idx(),
            };
            enc->push_constants(&pc, sizeof(pc));
            enc->dispatch_compute(glm::uvec3{AlphaMaskType::Count, 1, 1}, glm::uvec3{1, 1, 1});
          }
        }
      });
    }

    if (renderer_cv::pipeline_mesh_shaders.get() != 0) {
      add_draw_cull_pass(DrawCullPhase::Early, task_cmd_buf_rg_ids, out_draw_count_ids_early);
    }

  } else {
    auto& prepare_indirect_pass = rg_.add_compute_pass("prepare_indirect");
    prepare_indirect_pass.write_buf(indirect_buffer_id, rhi::PipelineStage::ComputeShader);
    prepare_indirect_pass.set_ex([this](rhi::CmdEncoder* enc) {
      ZoneScopedN("Prepare indirect");
      enc->write_timestamp(get_query_pool(), 0);
      for (size_t view_id = 0; view_id < render_views_.size(); view_id++) {
        BasicIndirectPC pc{
            .view_data_buf_idx = get_render_view((RenderViewId)view_id).data_buf_info.idx,
            .view_data_buf_offset =
                get_render_view((RenderViewId)view_id).data_buf_info.offset_bytes,
            .vert_buf_idx = static_draw_batch_.vertex_buf.get_buffer()->bindless_idx(),
            .instance_data_buf_idx =
                device_->get_buf(static_instance_mgr_.get_instance_data_buf())->bindless_idx(),
            .mat_buf_idx = materials_buf_.get_buffer()->bindless_idx(),
        };
        indirect_cmd_buf_ids_.emplace_back(enc->prepare_indexed_indirect_draws(
            static_instance_mgr_.get_draw_cmd_buf(), 0,
            static_instance_mgr_.stats().max_instance_data_count,
            static_draw_batch_.index_buf.get_buffer_handle(), 0, &pc, sizeof(pc),
            sizeof(DefaultVertex)));
      }
    });
  }

  std::vector<RGResourceId> meshlet_draw_stats_buf_ids;
  std::vector<RGResourceId> depth_pyramid_ids(render_views_.size());
  std::vector<RGResourceId> depth_ids(render_views_.size());
  std::vector<RGResourceId> meshlet_vis_ids(render_views_.size());

  for (size_t view_id = 0; view_id < render_views_.size(); view_id++) {
    depth_pyramid_ids[view_id] = rg_.import_external_texture(
        render_views_[view_id].depth_pyramid_tex.handle, "depth_pyramid_tex");
  }

  if (mesh_shaders_enabled()) {
    const size_t required_meshlet_vis_buf_size =
        static_cast<size_t>(static_instance_mgr_.get_num_meshlet_vis_buf_elements()) *
        sizeof(uint32_t);
    struct BufferClear {
      RGResourceId id;
      rhi::BufferHandle buf;
      size_t size_bytes;
    };
    std::vector<BufferClear> meshlet_vis_clear_ids;
    meshlet_vis_clear_ids.reserve(render_views_.size());
    for (size_t view_id = 0; view_id < render_views_.size(); view_id++) {
      auto& render_view = get_render_view((RenderViewId)view_id);
      auto* vis_buf = device_->get_buf(render_view.meshlet_vis_buf);
      bool created_or_resized = false;
      if (!vis_buf || vis_buf->size() < required_meshlet_vis_buf_size) {
        render_view.meshlet_vis_buf = device_->create_buf_h({.usage = BufferUsage::Storage,
                                                             .size = required_meshlet_vis_buf_size,
                                                             .name = "meshlet_vis_buf"});
        created_or_resized = true;
      }
      meshlet_vis_ids[view_id] =
          rg_.import_external_buffer(render_view.meshlet_vis_buf, "meshlet_vis_buf");
      if (created_or_resized) {
        meshlet_vis_clear_ids.push_back({.id = meshlet_vis_ids[view_id],
                                         .buf = render_view.meshlet_vis_buf.handle,
                                         .size_bytes = required_meshlet_vis_buf_size});
      }
    }
    if (!meshlet_vis_clear_ids.empty()) {
      auto& p = rg_.add_transfer_pass("clear_meshlet_vis");
      for (const auto& clear : meshlet_vis_clear_ids) {
        p.write_buf(clear.id, rhi::PipelineStage::AllTransfer);
      }
      p.set_ex([meshlet_vis_clear_ids](rhi::CmdEncoder* enc) {
        for (const auto& clear : meshlet_vis_clear_ids) {
          enc->fill_buffer(clear.buf, 0, clear.size_bytes, 0);
        }
      });
    }

    meshlet_draw_stats_buf_ids.reserve(render_views_.size());
    for (size_t view_id = 0; view_id < render_views_.size(); view_id++) {
      meshlet_draw_stats_buf_ids.push_back(
          rg_.create_buffer({.size = sizeof(MeshletDrawStats)}, "meshlet_draw_stats"));
      RGResourceId meshlet_stats_clear_id = meshlet_draw_stats_buf_ids[view_id];
      auto& p = rg_.add_transfer_pass("clear_meshlet_draw_stats");
      p.write_buf(meshlet_stats_clear_id, rhi::PipelineStage::AllTransfer);
      p.set_ex([this, view_id, meshlet_stats_clear_id](rhi::CmdEncoder* enc) {
        if (view_id == 0) enc->write_timestamp(get_query_pool(), 0);
        rhi::BufferHandle buf = rg_.get_buf(meshlet_stats_clear_id);
        enc->fill_buffer(buf, 0, sizeof(MeshletDrawStats), 0);
      });
    }
  }

  GBufferRenderer::GbufferPassInfo gbuffer_pass_info{};

  bool obj_or_meshlet_occlusion_culling_enabled =
      (renderer_cv::culling_object_occlusion.get() != 0 ||
       renderer_cv::culling_meshlet_occlusion.get() != 0) &&
      renderer_cv::culling_enabled.get() != 0 && renderer_cv::pipeline_mesh_shaders.get() != 0;

  if (get_shadows_enabled() && mesh_shaders_enabled()) {
    const GBufferRenderer::SceneBindings gbuffer_scene{
        static_draw_batch_, materials_buf_.get_buffer_handle(), frame_globals_buf_info_};
    for (size_t i = 0; i < shadow_cascade_count_; i++) {
      const int svid = static_cast<int>(shadow_map_render_views_[i]);
      GBufferRenderer::ShadowDepthPassInfo shadow_depth{};
      const GBufferRenderer::GBufferViewBindingsMeshlet shadow_view{
          task_cmd_buf_rg_ids[svid],
          get_render_view(shadow_map_render_views_[i]),
          {meshlet_vis_ids[svid], out_draw_count_ids_early[svid], final_depth_pyramid_ids[svid],
           meshlet_draw_stats_buf_ids[svid]}};
      gbuffer_renderer_->bake_shadow_depth(std::string("csm_pass_early_") + std::to_string(i),
                                           shadow_depth, DrawCullPhase::Early, gbuffer_scene,
                                           shadow_view);
      depth_ids[svid] = shadow_depth.depth_id;
    }
  }

  {
    auto vid = (int)main_render_view_id_;
    const GBufferRenderer::SceneBindings gbuffer_scene{
        static_draw_batch_, materials_buf_.get_buffer_handle(), frame_globals_buf_info_};
    RenderView& main_render_view = get_render_view(main_render_view_id_);
    if (renderer_cv::pipeline_mesh_shaders.get() != 0) {
      const GBufferRenderer::GBufferViewBindingsMeshlet gbuffer_view{
          task_cmd_buf_rg_ids[vid],
          main_render_view,
          {meshlet_vis_ids[vid], out_draw_count_ids_early[vid], final_depth_pyramid_ids[vid],
           meshlet_draw_stats_buf_ids[vid]}};
      gbuffer_renderer_->bake(gbuffer_pass_info, DrawCullPhase::Early, gbuffer_scene, gbuffer_view);
    } else {
      const GBufferRenderer::IndexedIndirectGBufferView indirect_gbuf_view{
          main_render_view,
          indirect_buffer_id,
          // main only, this is hardcoded but corresponds to indirect_cmd_buf_ids_
          0,
          static_instance_mgr_.stats().max_instance_data_count,
      };
      const GBufferRenderer::GBufferViewBindings gbuffer_view{main_render_view};
      gbuffer_renderer_->bake(gbuffer_pass_info, DrawCullPhase::Early, gbuffer_scene, gbuffer_view,
                              indirect_gbuf_view);
    }
    depth_ids[vid] = gbuffer_pass_info.depth_id;
  }

  if (obj_or_meshlet_occlusion_culling_enabled) {
    for (auto view_id : view_ids) {
      if (view_id != main_render_view_id_) continue;
      const auto& render_view = get_render_view(view_id);
      auto* dp_tex = device_->get_tex(render_view.depth_pyramid_tex);
      auto dp_dims = glm::uvec2{dp_tex->desc().dims};
      uint32_t mip_levels = math::get_mip_levels(dp_dims.x, dp_dims.y);
      auto depth_pyramid_id = depth_pyramid_ids[(int)view_id];
      uint32_t final_mip = mip_levels - 1;
      for (uint32_t mip = 0; mip < final_mip; mip++) {
        auto& p = rg_.add_compute_pass("depth_reduce_" + std::to_string(mip) +
                                       "_view:" + std::to_string((int)view_id));
        RGResourceId depth_handle{};
        if (mip == 0) {
          depth_handle = p.read_tex(depth_ids[(int)view_id], rhi::PipelineStage::ComputeShader);
        } else {
          p.read_tex(depth_pyramid_id, rhi::PipelineStage::ComputeShader);
        }
        if (mip == 0) {
          p.write_tex(depth_pyramid_id, rhi::PipelineStage::ComputeShader);
        } else {
          depth_pyramid_id =
              p.rw_tex(depth_pyramid_id, rhi::PipelineStage::ComputeShader,
                       rhi::AccessFlags::ShaderStorageRead | rhi::AccessFlags::ShaderWrite);
        }
        if (mip == final_mip - 1) {
          final_depth_pyramid_ids[(int)view_id] = depth_pyramid_id;
        }

        p.set_ex([this, mip, depth_handle, dp_dims, &render_view](rhi::CmdEncoder* enc) {
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
            enc->bind_srv(render_view.depth_pyramid_tex.handle, 0,
                          render_view.depth_pyramid_tex.views[mip - 1]);
          }
          enc->bind_uav(render_view.depth_pyramid_tex.handle, 0,
                        render_view.depth_pyramid_tex.views[mip]);

          constexpr size_t k_tg_size = 8;
          enc->dispatch_compute(glm::uvec3{align_divide_up(pc.out_tex_dim_x, k_tg_size),
                                           align_divide_up(pc.out_tex_dim_y, k_tg_size), 1},
                                glm::uvec3{k_tg_size, k_tg_size, 1});
        });
      }
    }
  }

  // Meshlet occlusion's late meshlet pass relies on the late draw_cull phase to populate
  // `out_draw_count_ids_late` and late `TaskCmd` buffers.
  if (renderer_cv::pipeline_mesh_shaders.get() != 0 &&
      (renderer_cv::culling_object_occlusion.get() != 0 ||
       renderer_cv::culling_meshlet_occlusion.get() != 0)) {
    add_draw_cull_pass(DrawCullPhase::Late, task_cmd_buf_rg_ids, out_draw_count_ids_late);
  }

  if (obj_or_meshlet_occlusion_culling_enabled) {
    auto vid = (int)main_render_view_id_;
    const GBufferRenderer::SceneBindings gbuffer_scene{
        static_draw_batch_, materials_buf_.get_buffer_handle(), frame_globals_buf_info_};
    const GBufferRenderer::GBufferViewBindingsMeshlet gbuffer_view{
        task_cmd_buf_rg_ids[vid],
        get_render_view(main_render_view_id_),
        {meshlet_vis_ids[vid], out_draw_count_ids_late[vid], final_depth_pyramid_ids[vid],
         meshlet_draw_stats_buf_ids[vid]}};
    gbuffer_renderer_->bake(gbuffer_pass_info, DrawCullPhase::Late, gbuffer_scene, gbuffer_view);
  }

  if (renderer_cv::pipeline_mesh_shaders.get() != 0) {  // readback draw counts
    const auto fif_i = static_cast<uint32_t>(curr_frame_idx_);
    for (size_t view_id = 0; view_id < render_views_.size(); view_id++) {
      if (view_id > 0) break;
      RGResourceId meshlet_stats_read_src = meshlet_draw_stats_buf_ids[view_id];
      rhi::BufferHandle meshlet_dst = meshlet_draw_stats_readback_[view_id][fif_i].handle;
      RGResourceId meshlet_dst_rg =
          rg_.import_external_buffer(meshlet_dst, "meshlet_stats_readback_curr");
      add_buffer_readback_copy(rg_, "readback_meshlet_draw_stats", meshlet_stats_read_src,
                               meshlet_dst, meshlet_dst_rg, 0, 0, sizeof(MeshletDrawStats));
    }
    for (size_t rb_view = 0; rb_view < render_views_.size(); rb_view++) {
      rhi::BufferHandle counts_dst = draw_cmd_counts_readback_[rb_view][fif_i].handle;
      RGResourceId counts_dst_rg = rg_.import_external_buffer(counts_dst, "draw_cmd_readback");
      add_buffer_readback_copy(rg_, "readback_draw_cmd_counts", draw_cmd_count_buf_ids[rb_view],
                               counts_dst, counts_dst_rg, 0, 0,
                               sizeof(uint32_t) * static_cast<size_t>(DrawCullPhase::Count));
    }
  }

  {
    struct ShadePassInfo {
      RGResourceId gbuffer_a_id{};
      RGResourceId gbuffer_b_id{};
    };

    ShadePassInfo shade_pass_info{
        .gbuffer_a_id = gbuffer_pass_info.gbuffer_a_id,
        .gbuffer_b_id = gbuffer_pass_info.gbuffer_b_id,
    };

    auto& p = rg_.add_graphics_pass("shade");
    auto gbuffer_a_id = p.sample_tex(shade_pass_info.gbuffer_a_id);
    auto gbuffer_b_id = p.sample_tex(shade_pass_info.gbuffer_b_id);
    RGResourceId secondary_view_debug_depth_rg_handle{};
    const DebugRenderMode debug_mode = clamped_debug_render_mode();
    bool secondary_view_debug_enabled =
        debug_mode == DebugRenderMode::SecondaryView && !shadow_map_render_views_.empty();
    if (secondary_view_debug_enabled) {
      p.sample_tex(depth_ids[(int)shadow_map_render_views_[0]]);
      secondary_view_debug_depth_rg_handle = p.read_tex(depth_ids[(int)shadow_map_render_views_[0]],
                                                        rhi::PipelineStage::FragmentShader);
    }
    if (obj_or_meshlet_occlusion_culling_enabled &&
        debug_mode == DebugRenderMode::DepthReduceMips) {
      p.sample_tex(final_depth_pyramid_ids[(int)main_render_view_id_]);
    }
    p.w_swapchain_tex(swapchain_);
    p.set_ex([this, gbuffer_a_id, gbuffer_b_id, secondary_view_debug_depth_rg_handle,
              secondary_view_debug_enabled,
              obj_or_meshlet_occlusion_culling_enabled](rhi::CmdEncoder* enc) {
      ZoneScopedN("Final pass");
      auto* gbuffer_a_tex = device_->get_tex(rg_.get_att_img(gbuffer_a_id));
      auto* gbuffer_b_tex = device_->get_tex(rg_.get_att_img(gbuffer_b_id));
      auto dims = gbuffer_a_tex->desc().dims;
      device_->begin_swapchain_rendering(swapchain_, enc);
      enc->bind_pipeline(tex_only_pso_);
      enc->set_wind_order(rhi::WindOrder::Clockwise);
      enc->set_cull_mode(rhi::CullMode::Back);
      enc->set_viewport({0, 0}, dims);
      enc->bind_cbv(frame_globals_buf_info_.buf, GLOBALS_SLOT,
                    frame_globals_buf_info_.offset_bytes);

      uint32_t tex_idx{};
      float mult = 1.f;
      if (secondary_view_debug_enabled) {
        mult = 100.f;
        tex_idx =
            device_->get_tex(rg_.get_att_img(secondary_view_debug_depth_rg_handle))->bindless_idx();
      } else if (!obj_or_meshlet_occlusion_culling_enabled ||
                 clamped_debug_render_mode() != DebugRenderMode::DepthReduceMips) {
        tex_idx = gbuffer_a_tex->bindless_idx();
      } else {
        const int mip_i = renderer_cv::debug_depth_pyramid_mip.get();
        tex_idx =
            device_->get_tex_view_bindless_idx(render_views_[0].depth_pyramid_tex.handle,
                                               render_views_[0].depth_pyramid_tex.views[mip_i]);
        mult = 100.f;
      }
      TexOnlyPC pc{
          .color_mult = glm::vec4{mult, mult, mult, 1},
          .tex_idx = tex_idx,
          .gbuffer_b_idx = gbuffer_b_tex->bindless_idx(),
          .mip_level = static_cast<uint32_t>(renderer_cv::debug_depth_pyramid_mip.get()),
      };
      enc->push_constants(&pc, sizeof(pc));
      enc->draw_primitives(rhi::PrimitiveTopology::TriangleList, 3);

      if (renderer_cv::ui_imgui_enabled.get() != 0) {
        imgui_renderer_->render(enc, {swapchain_->desc_.width, swapchain_->desc_.height},
                                curr_frame_idx_);
      }
      enc->end_rendering();
      enc->write_timestamp(get_query_pool(), 1);
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
      auto& mat = mats.emplace_back();
      if (m.albedo_tex != INVALID_TEX_ID) {
        mat.albedo_tex_idx = img_upload_bindless_indices[m.albedo_tex];
      } else {
        mat.albedo_tex_idx = INVALID_TEX_ID;
      }
      if (m.normal_tex != INVALID_TEX_ID) {
        mat.normal_tex_idx = img_upload_bindless_indices[m.normal_tex];
      } else {
        mat.normal_tex_idx = INVALID_TEX_ID;
      }
      mat.color = m.albedo_factors;
      mat.flags = m.flags;
    }
    buffer_copy_mgr_.copy_to_buffer(
        mats.data(), mats.size() * sizeof(M4Material), materials_buf_.get_buffer_handle(),
        material_alloc.offset * sizeof(M4Material), rhi::PipelineStage::FragmentShader,
        rhi::AccessFlags::ShaderRead);
    if (resized) {
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
      total_instance_vertices +=
          result.meshlet_process_result.meshlet_datas[mesh_id].meshlet_vertices.size();
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
                                  vertex_alloc.offset * sizeof(DefaultVertex),
                                  rhi::PipelineStage::VertexShader | rhi::PipelineStage::MeshShader,
                                  rhi::AccessFlags::ShaderRead);

  OffsetAllocator::Allocation index_alloc{};
  if (!indices.empty()) {
    index_alloc = draw_batch.index_buf.allocate(indices.size(), resized);
    buffer_copy_mgr_.copy_to_buffer(indices.data(), indices.size() * sizeof(rhi::DefaultIndexT),
                                    draw_batch.index_buf.get_buffer_handle(),
                                    index_alloc.offset * sizeof(rhi::DefaultIndexT),
                                    rhi::PipelineStage::IndexInput, rhi::AccessFlags::IndexRead);
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
  std::vector<MeshData> mesh_datas;
  mesh_datas.reserve(meshlets.meshlet_datas.size());
  for (const auto& meshlet_data : meshlets.meshlet_datas) {
    ASSERT(!meshlet_data.meshlets.empty());
    buffer_copy_mgr_.copy_to_buffer(meshlet_data.meshlets.data(),
                                    meshlet_data.meshlets.size() * sizeof(Meshlet),
                                    draw_batch.meshlet_buf.get_buffer_handle(),
                                    (meshlet_alloc.offset + meshlet_offset) * sizeof(Meshlet),
                                    rhi::PipelineStage::MeshShader | rhi::PipelineStage::TaskShader,
                                    rhi::AccessFlags::ShaderRead);
    meshlet_offset += meshlet_data.meshlets.size();

    ASSERT(!meshlet_data.meshlet_vertices.empty());
    buffer_copy_mgr_.copy_to_buffer(
        meshlet_data.meshlet_vertices.data(),
        meshlet_data.meshlet_vertices.size() * sizeof(uint32_t),
        draw_batch.meshlet_vertices_buf.get_buffer_handle(),
        (meshlet_vertices_alloc.offset + meshlet_vertices_offset) * sizeof(uint32_t),
        rhi::PipelineStage::MeshShader | rhi::PipelineStage::TaskShader,
        rhi::AccessFlags::ShaderRead);

    meshlet_vertices_offset += meshlet_data.meshlet_vertices.size();

    ASSERT(!meshlet_data.meshlet_triangles.empty());
    buffer_copy_mgr_.copy_to_buffer(
        meshlet_data.meshlet_triangles.data(),
        meshlet_data.meshlet_triangles.size() * sizeof(uint8_t),
        draw_batch.meshlet_triangles_buf.get_buffer_handle(),
        (meshlet_triangles_alloc.offset + meshlet_triangles_offset) * sizeof(uint8_t),
        rhi::PipelineStage::MeshShader | rhi::PipelineStage::TaskShader,
        rhi::AccessFlags::ShaderRead);

    meshlet_triangles_offset += meshlet_data.meshlet_triangles.size();

    ASSERT(mesh_i < meshes.size());
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
    mesh_i++;
    mesh_datas.push_back(d);
  }
  buffer_copy_mgr_.copy_to_buffer(
      mesh_datas.data(), mesh_datas.size() * sizeof(MeshData),
      draw_batch.mesh_buf.get_buffer_handle(), mesh_alloc.offset * sizeof(MeshData),
      rhi::PipelineStage::ComputeShader | rhi::PipelineStage::MeshShader |
          rhi::PipelineStage::TaskShader,
      rhi::AccessFlags::ShaderRead);

  return GeometryBatch::Alloc{.vertex_alloc = vertex_alloc,
                              .index_alloc = index_alloc,
                              .meshlet_alloc = meshlet_alloc,
                              .mesh_alloc = mesh_alloc,
                              .meshlet_triangles_alloc = meshlet_triangles_alloc,
                              .meshlet_vertices_alloc = meshlet_vertices_alloc};
}

void MemeRenderer123::reserve_space_for(std::span<std::pair<ModelGPUHandle, uint32_t>> models) {
  size_t total_instance_datas{};
  for (auto& [model_handle, instance_count] : models) {
    auto* model_resources = model_gpu_resource_pool_.get(model_handle);
    ASSERT(model_resources);
    total_instance_datas += model_resources->base_instance_datas.size() * instance_count;
  }
  static_instance_mgr_.reserve_space(total_instance_datas);
}

ModelInstanceGPUHandle MemeRenderer123::add_model_instance(ModelInstance& model,
                                                           ModelGPUHandle model_gpu_handle) {
  ZoneScoped;
  auto* model_resources = model_gpu_resource_pool_.get(model_gpu_handle);
  ASSERT(model_resources);
  auto& model_instance_datas = model_resources->base_instance_datas;
  auto& instance_id_to_node = model_resources->instance_id_to_node;
  std::vector<InstanceData> instance_datas = {model_instance_datas.begin(),
                                              model_instance_datas.end()};
  std::vector<IndexedIndirectDrawCmd> cmds;
  if (renderer_cv::pipeline_mesh_shaders.get() == 0) cmds.reserve(instance_datas.size());

  ASSERT(instance_datas.size() == instance_id_to_node.size());

  const InstanceMgr::Alloc instance_data_gpu_alloc = static_instance_mgr_.allocate(
      model_instance_datas.size(), model_resources->totals.instance_meshlets);
  stats_.total_instance_meshlets += model_resources->totals.instance_meshlets;
  stats_.total_instance_vertices += model_resources->totals.instance_vertices;

  if (static_instance_mgr_.need_draw_cmds_on_cpu() &&
      static_instance_mgr_.cpu_draw_cmds().size() <
          instance_data_gpu_alloc.instance_data_alloc.offset + instance_datas.size()) {
    static_instance_mgr_.cpu_draw_cmds().resize(instance_data_gpu_alloc.instance_data_alloc.offset +
                                                instance_datas.size());
  }
  for (size_t i = 0; i < instance_datas.size(); i++) {
    auto node_i = instance_id_to_node[i];
    const auto& transform = model.global_transforms[node_i];
    instance_datas[i].translation = transform.translation;
    instance_datas[i].rotation = transform.rotation;
    instance_datas[i].scale = transform.scale;
    instance_datas[i].meshlet_vis_base += instance_data_gpu_alloc.meshlet_vis_alloc.offset;
    size_t mesh_id = model.mesh_ids[node_i];
    auto& mesh = model_resources->meshes[mesh_id];
    IndexedIndirectDrawCmd cmd{
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
    };
    if (static_instance_mgr_.need_draw_cmds_on_cpu()) {
      auto final_instance_i = instance_data_gpu_alloc.instance_data_alloc.offset + i;
      ASSERT(final_instance_i < static_instance_mgr_.cpu_draw_cmds().size());
      static_instance_mgr_.cpu_draw_cmds()[final_instance_i] = cmd;
    }
    if (renderer_cv::pipeline_mesh_shaders.get() == 0) {
      cmds.push_back(cmd);
    }
  }
  static_draw_batch_.task_cmd_count += model_resources->totals.task_cmd_count;

  stats_.total_instances += instance_datas.size();
  buffer_copy_mgr_.copy_to_buffer(
      instance_datas.data(), instance_datas.size() * sizeof(InstanceData),
      static_instance_mgr_.get_instance_data_buf(),
      instance_data_gpu_alloc.instance_data_alloc.offset * sizeof(InstanceData),
      rhi::PipelineStage::AllCommands, rhi::AccessFlags::ShaderRead);
  if (renderer_cv::pipeline_mesh_shaders.get() == 0) {
    buffer_copy_mgr_.copy_to_buffer(
        cmds.data(), cmds.size() * sizeof(IndexedIndirectDrawCmd),
        static_instance_mgr_.get_draw_cmd_buf(),
        instance_data_gpu_alloc.instance_data_alloc.offset * sizeof(IndexedIndirectDrawCmd),
        rhi::PipelineStage::ComputeShader | rhi::PipelineStage::DrawIndirect,
        rhi::AccessFlags::IndirectCommandRead);
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
  static_instance_mgr_.free(gpu_resources->instance_data_gpu_alloc, curr_frame_idx_);
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

void MemeRenderer123::init_imgui() {
  ZoneScoped;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  for (const auto& entry : std::filesystem::directory_iterator(resource_dir_ / "fonts")) {
    if (entry.path().extension() == ".ttf") {
      auto* font = io.Fonts->AddFontFromFileTTF(entry.path().string().c_str(), 16, nullptr,
                                                io.Fonts->GetGlyphRangesDefault());
      // strip .ttf
      add_font(entry.path().stem().string(), font);
    }
  }

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendRendererName = "imgui_impl_memes";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  ImGui_ImplGlfw_InitForOther(window_->get_handle(), true);
}

void MemeRenderer123::shutdown_imgui() { ZoneScoped; }

void MemeRenderer123::on_imgui() {
  ZoneScoped;
  if (ImGui::BeginTabBar("MemeRenderer123##MainTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
    if (ImGui::BeginTabItem("Overview")) {
      on_imgui_tab_overview();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Stats")) {
      on_imgui_tab_stats();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Culling")) {
      on_imgui_tab_culling();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Debug")) {
      on_imgui_tab_debug();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Textures")) {
      on_imgui_tab_textures();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Device")) {
      on_imgui_tab_device();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
}

void MemeRenderer123::on_imgui_tab_overview() {
  ImGui::Text("Mesh shaders enabled: %d", renderer_cv::pipeline_mesh_shaders.get() != 0);
  ImGui::Text("Render Mode %s", to_string(clamped_debug_render_mode()));
  ImGui::Separator();
}

void MemeRenderer123::on_imgui_tab_stats() {
  ImGui::Text("Total possible vertices drawn: %d\nTotal objects: %u",
              stats_.total_instance_vertices, stats_.total_instances);
  ImGui::Text("Total total instance meshlets: %d", stats_.total_instance_meshlets);
  ImGui::Text("GPU Frame Time: %.2f (%.2f FPS)", stats_.avg_gpu_frame_time,
              1000.f / stats_.avg_gpu_frame_time);

  // frames_in_flight - 1 frames ago is guaranteed to be copied back to the cpu (see readback
  // passes above)
  if (frame_num_ >= device_->get_info().frames_in_flight) {
    const size_t frames_ago = device_->get_info().frames_in_flight - 1;
    for (size_t v = 0; v < render_views_.size(); v++) {
      if (v >= draw_cmd_counts_readback_.size()) continue;
      auto* counts = static_cast<uint32_t*>(
          device_->get_buf(draw_cmd_counts_readback_[v][get_frames_ago_idx(frames_ago)])
              ->contents());
      ImGui::Text("Draw cull cmd counts (view %zu, early/late): %u %u", v, counts[0], counts[1]);
    }
  }

  meshlet_stats_imgui(ResourceManager::get().get_tot_instances_loaded());
}

void MemeRenderer123::on_imgui_tab_culling() {
  auto bool_cvar_checkbox = [](const char* label, AutoCVarInt& cv) {
    bool v = cv.get() != 0;
    if (ImGui::Checkbox(label, &v)) {
      cv.set(v ? 1 : 0);
    }
  };
  bool_cvar_checkbox("Culling paused", renderer_cv::culling_paused);
  bool_cvar_checkbox("Culling enabled", renderer_cv::culling_enabled);
  bool_cvar_checkbox("Meshlet frustum culling enabled", renderer_cv::culling_meshlet_frustum);
  bool_cvar_checkbox("Meshlet cone culling enabled", renderer_cv::culling_meshlet_cone);
  bool_cvar_checkbox("Meshlet occlusion culling enabled", renderer_cv::culling_meshlet_occlusion);
  bool_cvar_checkbox("Object occlusion culling enabled", renderer_cv::culling_object_occlusion);
}

void MemeRenderer123::on_imgui_tab_debug() {
  if (render_views_.empty()) {
    ImGui::TextUnformatted("No render views; depth pyramid mip unavailable.");
    return;
  }
  auto dp_dims = device_->get_tex(render_views_[0].depth_pyramid_tex)->desc().dims;
  auto mip_levels = math::get_mip_levels(dp_dims.x, dp_dims.y);
  int mip = renderer_cv::debug_depth_pyramid_mip.get();
  if (ImGui::SliderInt("Depth pyramid mip view", &mip, 0, mip_levels - 1)) {
    renderer_cv::debug_depth_pyramid_mip.set(mip);
  }
}

void MemeRenderer123::on_imgui_tab_textures() {
  model_gpu_resource_pool_.for_each([this](const ModelGPUResources& gpu_resource) {
    for (const auto& tex : gpu_resource.textures) {
      ImGui::Text("%s", device_->get_tex(tex)->desc().name);
      ImGui::Image((ImTextureRef)tex.handle.to64(), ImVec2{64, 64}, ImVec2{0, 0}, ImVec2{1, 1});
    }
  });
}

void MemeRenderer123::on_imgui_tab_device() {
  device_->on_imgui();
  if (ImGui::TreeNodeEx("GPU Buffers")) {
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
}

glm::mat4 MemeRenderer123::get_proj_matrix(float fov) {
  auto win_dims = window_->get_window_size();
  return infinite_perspective_proj(glm::radians(fov), (float)win_dims.x / win_dims.y, k_z_near);
}

void MemeRenderer123::set_cull_data_and_globals(const RenderArgs& args) {
  glm::mat4 proj_mat = get_proj_matrix();
  GlobalData global_data{
      .render_mode = static_cast<uint32_t>(clamped_debug_render_mode()),
      .frame_num = (uint32_t)frame_num_,
      .meshlet_stats_enabled =
          renderer_cv::developer_collect_meshlet_draw_stats.get() != 0 ? 1u : 0u,
  };
  {
    auto [buf, offset, write_ptr] =
        frame_gpu_upload_allocator_.alloc(sizeof(GlobalData), &global_data);
    frame_globals_buf_info_.buf = buf;
    frame_globals_buf_info_.idx = device_->get_buf(buf)->bindless_idx();
    frame_globals_buf_info_.offset_bytes = offset;
  }
  {
    ViewData view_data{
        .vp = proj_mat * args.view_mat,
        .view = args.view_mat,
        .proj = proj_mat,
        .camera_pos = glm::vec4{args.camera_pos, 0},
    };
    auto& main_view = get_render_view(main_render_view_id_);
    auto [buf, offset, write_ptr] = frame_gpu_upload_allocator_.alloc(sizeof(ViewData), &view_data);
    main_view.data_buf_info.buf = buf;
    main_view.data_buf_info.idx = device_->get_buf(buf)->bindless_idx();
    main_view.data_buf_info.offset_bytes = offset;
  }
  auto set_cull_data = [](GPUFrameAllocator3& frame_gpu_upload_allocator, rhi::Device* device,
                          RenderView& view, const glm::mat4& proj_mat, bool culling_paused) {
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
    const auto& dp_tex_desc = device->get_tex(view.depth_pyramid_tex)->desc();
    cull_data.pyramid_width = dp_tex_desc.dims.x;
    cull_data.pyramid_height = dp_tex_desc.dims.y;
    cull_data.pyramid_mip_count = dp_tex_desc.mip_levels;
    cull_data.p00 = proj_mat[0][0];
    cull_data.p11 = proj_mat[1][1];
    cull_data.paused = culling_paused;

    auto [buf, offset, write_ptr] = frame_gpu_upload_allocator.alloc(sizeof(CullData), &cull_data);
    // auto& view = get_render_view(main_render_view_id_);
    view.cull_data_buf_info.buf = buf;
    view.cull_data_buf_info.idx = device->get_buf(buf)->bindless_idx();
    view.cull_data_buf_info.offset_bytes = offset;
  };

  set_cull_data(frame_gpu_upload_allocator_, device_, get_render_view(main_render_view_id_),
                proj_mat, renderer_cv::culling_paused.get() != 0);
  if (get_shadows_enabled()) {
    for (size_t i = 0; i < shadow_cascade_count_; i++) {
      float shadow_fov = 170.0f;
      glm::mat4 shadow_proj_mat = get_proj_matrix(shadow_fov);
      ViewData shadow_view_data{
          .vp = shadow_proj_mat * args.view_mat,
          .view = args.view_mat,
          .proj = shadow_proj_mat,
          .camera_pos = glm::vec4{args.camera_pos, 0},
      };
      auto [buf, offset, write_ptr] =
          frame_gpu_upload_allocator_.alloc(sizeof(ViewData), &shadow_view_data);
      auto& view = get_render_view(shadow_map_render_views_[i]);
      view.data_buf_info.buf = buf;
      view.data_buf_info.idx = device_->get_buf(buf)->bindless_idx();
      view.data_buf_info.offset_bytes = offset;

      set_cull_data(frame_gpu_upload_allocator_, device_, view, shadow_proj_mat,
                    renderer_cv::culling_paused.get() != 0);
    }
  }
}

bool MemeRenderer123::on_key_event(int key, int action, int mods) {
  ZoneScoped;
  bool is_shift = mods & GLFW_MOD_SHIFT;
  if (action == GLFW_PRESS || action == GLFW_REPEAT) {
    if (key == GLFW_KEY_P && is_shift) {
      renderer_cv::culling_paused.set(renderer_cv::culling_paused.get() == 0 ? 1 : 0);
      return true;
    }
    if (key == GLFW_KEY_E && is_shift) {
      renderer_cv::culling_enabled.set(renderer_cv::culling_enabled.get() == 0 ? 1 : 0);
      return true;
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_CONTROL) {
      const int count = static_cast<int>(DebugRenderMode::Count);
      int m = static_cast<int>(clamped_debug_render_mode());
      if (mods & GLFW_MOD_SHIFT) {
        if (m == 0) {
          m = count - 1;
        } else {
          m = m - 1;
        }
      } else {
        m = (m + 1) % count;
      }
      renderer_cv::debug_render_mode.set(m);
      return true;
    }

    if (key == GLFW_KEY_M && mods & GLFW_MOD_CONTROL && mods & GLFW_MOD_SHIFT) {
      renderer_cv::culling_meshlet_occlusion.set(
          renderer_cv::culling_meshlet_occlusion.get() == 0 ? 1 : 0);
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
  for (size_t view_id = 0; view_id < render_views_.size(); view_id++) {
    make_depth_pyramid_tex((RenderViewId)view_id, main_size);
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

MemeRenderer123::MemeRenderer123(const CreateInfo& cinfo)
    : device_(cinfo.device),
      frame_gpu_upload_allocator_(device_),
      buffer_copy_mgr_(device_, frame_gpu_upload_allocator_),
      materials_buf_(*device_, buffer_copy_mgr_,
                     {.usage = rhi::BufferUsage::Storage,
                      .size = k_max_materials * sizeof(M4Material),
                      // .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
                      .name = "all materials buf"},
                     sizeof(M4Material)),
      static_instance_mgr_(*device_, buffer_copy_mgr_, device_->get_info().frames_in_flight, *this),
      static_draw_batch_(GeometryBatchType::Static, *device_, buffer_copy_mgr_,
                         GeometryBatch::CreateInfo{
                             .initial_vertex_capacity = 1'000,
                             .initial_index_capacity = 1'000,
                             .initial_meshlet_capacity = 1'000,
                             .initial_mesh_capacity = 100'0,
                             .initial_meshlet_triangle_capacity = 1'000,
                             .initial_meshlet_vertex_capacity = 1'000,
                         }) {
  ZoneScoped;
  window_ = cinfo.window;
  resource_dir_ = cinfo.resource_dir;
  swapchain_ = cinfo.swapchain;

  // TODO: renderer shouldn't own this
  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(
      device_, gfx::ShaderManager::Options{.targets = device_->get_supported_shader_targets()});

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
    csm_no_frag_pso_ = shader_mgr_->create_graphics_pipeline({
        .shaders = {{{"basic_indirect", ShaderType::Vertex}}},
    });

    tex_only_pso_ = shader_mgr_->create_graphics_pipeline({
        .shaders = {{{"fullscreen_quad", ShaderType::Vertex}, {"tex_only", ShaderType::Fragment}}},
    });

    draw_cull_pso_ = shader_mgr_->create_compute_pipeline({"draw_cull"});
    reset_counts_buf_pso_ = shader_mgr_->create_compute_pipeline({"test_clear_cnt_buf"});
    depth_reduce_pso_ = shader_mgr_->create_compute_pipeline({"depth_reduce/depth_reduce"});
    shade_pso_ = shader_mgr_->create_compute_pipeline({"shade"});
  }
  gbuffer_renderer_ =
      std::make_unique<gfx::GBufferRenderer>(device_, static_instance_mgr_, rg_, true);
  gbuffer_renderer_->load_pipelines(*shader_mgr_);

  rg_.init(device_);

  init_imgui();
  imgui_renderer_.emplace(*shader_mgr_, device_);

  main_render_view_id_ = create_render_view();

  CVarSystem::get().add_change_callback(
      renderer_cv::shadows_enabled,
      [this] { on_shadows_enabled_change(renderer_cv::shadows_enabled.get() != 0); });
  on_shadows_enabled_change(renderer_cv::shadows_enabled.get() != 0);

  recreate_swapchain_sized_textures();

  for (size_t i = 0; i < device_->get_info().frames_in_flight; i++) {
    query_pools_[i] = device_->create_query_pool_h(
        rhi::QueryPoolDesc{.count = k_query_count, .name = "query_pool_[i]"});
    query_resolve_bufs_[curr_frame_idx_] =
        device_->create_buf_h({.size = sizeof(uint64_t) * k_query_count,
                               .flags = rhi::BufferDescFlags::CPUAccessible,
                               .name = "query"});
  }
}

void MemeRenderer123::meshlet_stats_imgui(size_t total_scene_models) {
  if (renderer_cv::pipeline_mesh_shaders.get() != 0) {
    ImGui::Separator();
    bool collect_stats = renderer_cv::developer_collect_meshlet_draw_stats.get() != 0;
    if (ImGui::Checkbox("Collect meshlet draw stats (GPU atomics)", &collect_stats)) {
      renderer_cv::developer_collect_meshlet_draw_stats.set(collect_stats ? 1 : 0);
    }
    auto add_commas = [](uint64_t n) -> std::string {
      std::string s = std::to_string(n);
      int pos = s.length() - 3;
      while (pos > 0) {
        s.insert(pos, ",");
        pos -= 3;
      }
      return s;
    };

    constexpr int frames_ago = 2;
    ImGui::Text(
        "Culling Paused:             %d\n"
        "Culling Enabled:            %d\n"
        "Meshlet Occlusion Enabled:  %d",
        renderer_cv::culling_paused.get() != 0, renderer_cv::culling_enabled.get() != 0,
        renderer_cv::culling_meshlet_occlusion.get() != 0);

    for (size_t view_id = 0; view_id < render_views_.size(); view_id++) {
      if (view_id >= meshlet_draw_stats_readback_.size()) continue;
      MeshletDrawStats view_stats{};
      if (frame_num_ >= device_->get_info().frames_in_flight) {
        view_stats = *static_cast<MeshletDrawStats*>(
            device_->get_buf(meshlet_draw_stats_readback_[view_id][get_frames_ago_idx(frames_ago)])
                ->contents());
      }
      size_t tot_drawn_meshlets = view_stats.meshlets_drawn_early + view_stats.meshlets_drawn_late;
      ImGui::Text(
          "(View %zu) Meshlets drawn: %10s of %10s \n"
          "Culled: (%5.2f %%)\n"
          "Early: %7s\tLate %7s",
          view_id, add_commas(tot_drawn_meshlets).c_str(),
          add_commas(stats_.total_instance_meshlets).c_str(),
          tot_drawn_meshlets == 0
              ? 0
              : 100.f - (float)tot_drawn_meshlets / (float)stats_.total_instance_meshlets * 100.f,
          add_commas(view_stats.meshlets_drawn_early).c_str(),
          add_commas(view_stats.meshlets_drawn_late).c_str());
    }

    MeshletDrawStats stats{};
    if (frame_num_ >= device_->get_info().frames_in_flight) {
      auto main_vid = static_cast<size_t>(main_render_view_id_);
      if (main_vid < meshlet_draw_stats_readback_.size()) {
        stats = *static_cast<MeshletDrawStats*>(
            device_->get_buf(meshlet_draw_stats_readback_[main_vid][get_frames_ago_idx(frames_ago)])
                ->contents());
      }
    }

    ImGui::Text(
        "Triangles: %12s\n"
        "Vertices:  %12s\n"
        "Objects:   %12s\n"
        "Models:    %12s",
        add_commas(stats.triangles_drawn_early + stats.triangles_drawn_late).c_str(),
        add_commas(stats_.total_instance_vertices).c_str(),
        add_commas(stats_.total_instances).c_str(), add_commas(total_scene_models).c_str());
  }
}

void MemeRenderer123::update_model_instance_transforms(const ModelInstance& model) {
  auto instance_resources =
      model_instance_gpu_resource_pool_.get(model.instance_gpu_handle)->instance_data_gpu_alloc;
  auto* model_resources = model_gpu_resource_pool_.get(model.model_gpu_handle);
  auto& model_instance_datas = model_resources->base_instance_datas;
  std::vector<InstanceData> instance_datas = {model_instance_datas.begin(),
                                              model_instance_datas.end()};
  for (size_t i = 0; i < instance_datas.size(); i++) {
    auto node_i = model_resources->instance_id_to_node[i];
    const auto& transform = model.global_transforms[node_i];
    instance_datas[i].translation = transform.translation;
    instance_datas[i].rotation = transform.rotation;
    instance_datas[i].scale = transform.scale;
  }

  buffer_copy_mgr_.copy_to_buffer(
      instance_datas.data(), instance_datas.size() * sizeof(InstanceData),
      static_instance_mgr_.get_instance_data_buf(),
      instance_resources.instance_data_alloc.offset * sizeof(InstanceData),
      rhi::PipelineStage::AllCommands, rhi::AccessFlags::ShaderRead);
}

void MemeRenderer123::on_shadows_enabled_change(bool shadows_enabled) {
  renderer_cv::shadows_enabled.set(shadows_enabled ? 1 : 0);

  if (!shadows_enabled) {
    for (auto& view : shadow_map_render_views_) {
      destroy_render_view(view);
    }
    shadow_map_render_views_.clear();
  }

  if (shadows_enabled && shadow_map_render_views_.empty()) {
    for (size_t i = 0; i < k_max_shadow_cascades; i++) {
      shadow_map_render_views_.emplace_back(create_render_view());
    }
  }
}

void MemeRenderer123::ensure_per_view_readback_buffers() {
  const auto fif = static_cast<uint32_t>(device_->get_info().frames_in_flight);
  meshlet_draw_stats_readback_.resize(render_views_.size());
  draw_cmd_counts_readback_.resize(render_views_.size());
  const size_t meshlet_sz = sizeof(MeshletDrawStats);
  const size_t draw_cnt_sz = sizeof(uint32_t) * static_cast<size_t>(DrawCullPhase::Count);
  for (size_t v = 0; v < render_views_.size(); ++v) {
    for (uint32_t f = 0; f < fif; ++f) {
      if (!meshlet_draw_stats_readback_[v][f].handle.is_valid()) {
        meshlet_draw_stats_readback_[v][f] =
            device_->create_buf_h({.size = meshlet_sz,
                                   .flags = rhi::BufferDescFlags::CPUAccessible,
                                   .name = "meshlet_stats_rb"});
      }
      if (!draw_cmd_counts_readback_[v][f].handle.is_valid()) {
        draw_cmd_counts_readback_[v][f] =
            device_->create_buf_h({.size = draw_cnt_sz,
                                   .flags = rhi::BufferDescFlags::CPUAccessible,
                                   .name = "draw_cmd_counts_rb"});
      }
    }
    for (uint32_t f = fif; f < k_max_frames_in_flight; ++f) {
      meshlet_draw_stats_readback_[v][f] = {};
      draw_cmd_counts_readback_[v][f] = {};
    }
  }
}

RenderViewId MemeRenderer123::create_render_view() {
  RenderViewId view_id;
  if (!free_render_view_ids_.empty()) {
    view_id = free_render_view_ids_.back();
    free_render_view_ids_.pop_back();
  } else {
    view_id = (RenderViewId)render_views_.size();
    render_views_.push_back(RenderView{});
  }

  ensure_per_view_readback_buffers();
  auto main_size = window_->get_window_size();
  make_depth_pyramid_tex(view_id, main_size);
  return view_id;
}

void MemeRenderer123::make_depth_pyramid_tex(RenderViewId view_id, glm::uvec2 main_size) {
  auto size = glm::uvec3{prev_pow2(main_size.x), prev_pow2(main_size.y), 1};
  auto& render_view = get_render_view(view_id);
  auto& depth_pyramid_tex = render_view.depth_pyramid_tex;
  if (depth_pyramid_tex.is_valid()) {
    auto* existing = device_->get_tex(depth_pyramid_tex);
    if (existing->desc().dims == size) {
      return;
    }
  }
  for (auto& v : depth_pyramid_tex.views) {
    device_->destroy(depth_pyramid_tex.handle, v);
  }
  depth_pyramid_tex.views.clear();
  uint32_t mip_levels = math::get_mip_levels(size.x, size.y);
  depth_pyramid_tex = TexAndViewHolder{device_->create_tex_h(
      rhi::TextureDesc{.format = rhi::TextureFormat::R32float,
                       .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::ShaderWrite,
                       .dims = size,
                       .mip_levels = mip_levels,
                       .name = "depth_pyramid_tex"})};
  depth_pyramid_tex.views.reserve(mip_levels);
  for (size_t i = 0; i < mip_levels; i++) {
    depth_pyramid_tex.views.emplace_back(
        device_->create_tex_view(depth_pyramid_tex.handle, i, 1, 0, 1));
  }
}

void MemeRenderer123::destroy_render_view(RenderViewId view_id) {
  auto& depth_pyramid_tex = get_render_view(view_id).depth_pyramid_tex;
  for (auto& v : depth_pyramid_tex.views) {
    device_->destroy(depth_pyramid_tex.handle, v);
  }
  render_views_[(int)view_id] = {};
  free_render_view_ids_.emplace_back(view_id);
}

}  // namespace gfx

}  // namespace TENG_NAMESPACE
