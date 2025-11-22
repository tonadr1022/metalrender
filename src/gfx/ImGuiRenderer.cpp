#include "ImGuiRenderer.hpp"

#include <cstddef>

#include "RenderGraph.hpp"
#include "core/EAssert.hpp"
#include "core/Util.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/RendererTypes.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "hlsl/shared_imgui.h"
#include "imgui.h"

namespace gfx {

ImGuiRenderer::ImGuiRenderer(rhi::Device* device) : device_(device) {
  pso_ = device_->create_graphics_pipeline_h(rhi::GraphicsPipelineCreateInfo{
      .shaders = {{
          {"imgui", rhi::ShaderType::Vertex},
          {"imgui", rhi::ShaderType::Fragment},
      }},
      // TODO: parameterize texture formats
      .rendering = {.color_formats{rhi::TextureFormat::B8G8R8A8Unorm},
                    .depth_format = rhi::TextureFormat::D32float},
      .blend = {.attachments = {{
                    .enable = true,
                    .src_color_factor = rhi::BlendFactor::SrcAlpha,
                    .dst_color_factor = rhi::BlendFactor::OneMinusSrcAlpha,
                    .color_blend_op = rhi::BlendOp::Add,
                    .src_alpha_factor = rhi::BlendFactor::One,
                    .dst_alpha_factor = rhi::BlendFactor::OneMinusSrcAlpha,
                    .alpha_blend_op = rhi::BlendOp::Add,
                }}},
  });
}

void ImGuiRenderer::render(rhi::CmdEncoder* enc, glm::uvec2 fb_size, size_t frame_in_flight) {
  auto* draw_data = ImGui::GetDrawData();
  ASSERT(draw_data);
  if (draw_data->TotalVtxCount == 0 || draw_data->CmdLists.empty()) {
    return;
  }
  ASSERT(pso_.is_valid());
  enc->bind_pipeline(pso_);
  enc->set_cull_mode(rhi::CullMode::None);
  enc->set_depth_stencil_state(rhi::CompareOp::Always, false);
  enc->set_viewport(glm::uvec2{}, fb_size);

  size_t vert_buf_len = (size_t)draw_data->TotalVtxCount * sizeof(ImDrawVert);
  size_t index_buf_len = (size_t)draw_data->TotalIdxCount * sizeof(ImDrawIdx);
  auto vert_buf_handle = get_buffer_of_size(vert_buf_len, frame_in_flight, "imgui_vertex_buf");
  auto index_buf_handle = get_buffer_of_size(index_buf_len, frame_in_flight, "imgui_index_buf");
  auto* vert_buf = device_->get_buf(vert_buf_handle);
  auto* index_buf = device_->get_buf(index_buf_handle);

  float L = draw_data->DisplayPos.x;
  float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
  float T = draw_data->DisplayPos.y;
  float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
  float N = 0.0;
  float F = 1.0;
  auto proj = glm::orthoZO(L, R, B, T, N, F);
  [[maybe_unused]] ImGuiPC pc{
      .proj = proj,
      .vert_buf_idx = vert_buf->bindless_idx(),
      .tex_idx = 0,
  };
  enc->push_constants(&pc, sizeof(pc));

  ImVec2 clip_off = draw_data->DisplayPos;  // (0,0) unless using multi-viewports
  ImVec2 clip_scale =
      draw_data->FramebufferScale;  // (1,1) unless using retina display which are often (2,2)

  size_t vertexBufferOffset = 0;
  size_t indexBufferOffset = 0;
  for (const ImDrawList* draw_list : draw_data->CmdLists) {
    memcpy((char*)vert_buf->contents() + vertexBufferOffset, draw_list->VtxBuffer.Data,
           (size_t)draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
    memcpy((char*)index_buf->contents() + indexBufferOffset, draw_list->IdxBuffer.Data,
           (size_t)draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));

    for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++) {
      const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
      if (pcmd->UserCallback) {
        ALWAYS_ASSERT(0 && "user callback not handled");
      } else {
        ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                        (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
        ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                        (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

        clip_min.x = std::max(clip_min.x, 0.0f);
        clip_min.y = std::max(clip_min.y, 0.0f);

        clip_max.x = std::min<float>(clip_max.x, fb_size.x);
        clip_max.y = std::min<float>(clip_max.y, fb_size.y);
        if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;
        if (pcmd->ElemCount == 0) {
          continue;
        }

        enc->set_scissor(glm::uvec2{clip_min.x, clip_min.y},
                         glm::uvec2{clip_max.x - clip_min.x, clip_max.y - clip_min.y});

        if (ImTextureID tex_id = pcmd->GetTexID()) {
          pc.tex_idx = device_->get_tex(rhi::TextureHandle{tex_id})->bindless_idx();
        }
        pc.vert_buf_idx = vert_buf->bindless_idx();
        enc->push_constants(&pc, sizeof(pc));
        enc->draw_indexed_primitives(
            rhi::PrimitiveTopology::TriangleList, index_buf_handle.handle,
            indexBufferOffset + pcmd->IdxOffset * sizeof(ImDrawIdx), pcmd->ElemCount, 1,
            (vertexBufferOffset + pcmd->VtxOffset * sizeof(ImDrawVert)) / sizeof(ImDrawVert), 0,
            sizeof(ImDrawIdx) == 2 ? rhi::IndexType::Uint16 : rhi::IndexType::Uint32);
      }
    }

    vertexBufferOffset += (size_t)draw_list->VtxBuffer.Size * sizeof(ImDrawVert);
    indexBufferOffset += (size_t)draw_list->IdxBuffer.Size * sizeof(ImDrawIdx);
  }
  return_buffer(std::move(vert_buf_handle), frame_in_flight);
  return_buffer(std::move(index_buf_handle), frame_in_flight);
}

rhi::BufferHandleHolder ImGuiRenderer::get_buffer_of_size(size_t size, size_t frame_in_flight,
                                                          const char* name) {
  auto& bufs = buffers_[frame_in_flight];
  size_t best_i{SIZE_T_MAX};

  for (size_t i = 0; i < bufs.size(); i++) {
    auto* buf = device_->get_buf(bufs[i]);
    ASSERT(buf);
    if (buf->size() >= size) {
      best_i = i;
      break;
    }
  }

  if (best_i != SIZE_T_MAX) {
    auto buf_handle = std::move(bufs[best_i]);
    if (best_i != bufs.size() - 1) {
      bufs[best_i] = std::move(bufs.back());
    }
    bufs.pop_back();
    device_->set_name(buf_handle.handle, name);
    return buf_handle;
  }

  return device_->create_buf_h({
      .storage_mode = rhi::StorageMode::CPUAndGPU,
      .usage = (rhi::BufferUsage)(rhi::BufferUsage_Storage | rhi::BufferUsage_Index),
      .size = size * 2,
      .bindless = true,
      .name = name,
  });
}

void ImGuiRenderer::return_buffer(rhi::BufferHandleHolder&& handle, size_t frame_in_flight) {
  buffers_[frame_in_flight].push_back(std::move(handle));
}

void ImGuiRenderer::flush_pending_texture_uploads(rhi::CmdEncoder* enc) {
  auto* draw_data = ImGui::GetDrawData();
  ASSERT(draw_data);
  if (draw_data->Textures) {
    for (ImTextureData* im_tex : *draw_data->Textures) {
      if (im_tex->Status != ImTextureStatus_OK) {
        if (im_tex->Status == ImTextureStatus_WantCreate) {
          auto tex_handle = rhi::TextureHandle{im_tex->GetTexID()};
          ALWAYS_ASSERT(tex_handle.is_valid());
          IM_ASSERT(im_tex->BackendUserData == nullptr);
          IM_ASSERT(im_tex->Format == ImTextureFormat_RGBA32);

          auto* tex = device_->get_tex(tex_handle);
          size_t bytes_per_element = im_tex->BytesPerPixel;
          size_t src_bytes_per_row = tex->desc().dims.x * bytes_per_element;
          size_t bytes_per_row = align_up(src_bytes_per_row, 256);
          // TODO: staging buffer pool
          auto upload_buf_handle = device_->create_buf({.storage_mode = rhi::StorageMode::CPUAndGPU,
                                                        .size = bytes_per_row * tex->desc().dims.y,
                                                        .name = "tex upload buf"});
          auto* upload_buf = device_->get_buf(upload_buf_handle);
          size_t dst_offset = 0;
          size_t src_offset = 0;
          for (size_t row = 0; row < tex->desc().dims.y; row++) {
            memcpy((uint8_t*)upload_buf->contents() + dst_offset,
                   (uint8_t*)im_tex->Pixels + src_offset, src_bytes_per_row);
            dst_offset += bytes_per_row;
            src_offset += src_bytes_per_row;
          }

          enc->upload_texture_data(upload_buf_handle, 0, bytes_per_row, tex_handle);
          im_tex->SetTexID(tex_handle.to64());
          im_tex->SetStatus(ImTextureStatus_OK);
        } else if (im_tex->Status == ImTextureStatus_WantUpdates) {
          for (ImTextureRect& r : im_tex->Updates) {
            size_t bytes_per_row = r.w * 4ull;
            auto upload_buf_handle =
                device_->create_buf({.storage_mode = rhi::StorageMode::CPUAndGPU,
                                     .size = static_cast<size_t>(r.h * r.w) * 4ull,
                                     .name = "tex upload buf"});
            auto* upload_buf = device_->get_buf(upload_buf_handle);
            size_t offset = 0;
            for (size_t y = r.y; y < r.y + r.h; y++) {
              memcpy((uint8_t*)upload_buf->contents() + offset,
                     (uint8_t*)im_tex->GetPixelsAt(r.x, y), bytes_per_row);
              offset += r.w * 4ull;
            }
            enc->upload_texture_data(upload_buf_handle, 0, bytes_per_row,
                                     rhi::TextureHandle{im_tex->GetTexID()},
                                     glm::uvec3{r.w, r.h, 1}, glm::uvec3{r.x, r.y, 0});
          }
          im_tex->SetStatus(ImTextureStatus_OK);
        } else if (im_tex->Status == ImTextureStatus_WantDestroy && im_tex->UnusedFrames > 0) {
          auto id = rhi::TextureHandle{im_tex->GetTexID()};
          device_->destroy(id);
          im_tex->SetTexID(ImTextureID_Invalid);
          im_tex->SetStatus(ImTextureStatus_Destroyed);
          im_tex->BackendUserData = nullptr;
        }
      }
    }
  }
}

bool ImGuiRenderer::has_dirty_textures() {
  auto* draw_data = ImGui::GetDrawData();
  if (!draw_data->Textures) {
    return false;
  }

  bool has_dirty = false;
  for (auto* im_tex : *draw_data->Textures) {
    auto handle = rhi::TextureHandle{im_tex->GetTexID()};
    if (im_tex->Status == ImTextureStatus_WantCreate ||
        im_tex->Status == ImTextureStatus_WantUpdates) {
      has_dirty = true;
    }
    if (im_tex->Status == ImTextureStatus_WantCreate) {
      if (!handle.is_valid()) {
        auto tex_handle = device_->create_tex({
            .format = rhi::TextureFormat::R8G8B8A8Unorm,
            .usage = rhi::TextureUsageSample,
            .dims = glm::uvec3{im_tex->Width, im_tex->Height, 1},
            .mip_levels = 1,
            .bindless = true,
            .name = "imgui_tex",
        });
        im_tex->SetTexID(tex_handle.to64());
      }
    }
  }
  return has_dirty;
}

void ImGuiRenderer::add_dirty_textures_to_pass(gfx::RGPass& pass, bool read_access) {
  auto* draw_data = ImGui::GetDrawData();
  if (!draw_data->Textures) {
    return;
  }
  auto access = read_access ? RGAccess::FragmentSample
                            : (RGAccess)(RGAccess::ComputeWrite | RGAccess::TransferWrite);
  for (const auto* t : *draw_data->Textures) {
    if (t->Status == ImTextureStatus_WantUpdates || t->Status == ImTextureStatus_WantCreate) {
      pass.add_tex(rhi::TextureHandle{t->GetTexID()}, access);
    }
  }
}
}  // namespace gfx
