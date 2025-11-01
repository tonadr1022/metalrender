#include "RendererMetal4.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <QuartzCore/CAMetalLayer.hpp>
#include <tracy/Tracy.hpp>

#include "WindowApple.hpp"
#include "core/Util.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "hlsl/material.h"

namespace {

struct Vert {
  glm::vec3 position;
  glm::vec2 uv;
};

}  // namespace

void RendererMetal4::init(const CreateInfo& cinfo) {
  device_ = cinfo.device;
  window_ = cinfo.window;

  {
    test_pso_ = device_->create_graphics_pipeline_h(rhi::GraphicsPipelineCreateInfo{
        .shaders =
            {
                rhi::ShaderCreateInfo{
                    .type = rhi::ShaderType::Vertex,
                    .entry_point = "vert_main",
                },
                rhi::ShaderCreateInfo{
                    .type = rhi::ShaderType::Fragment,
                    .entry_point = "frag_main",
                },
            },
        .rendering = {.color_formats{rhi::TextureFormat::R8G8B8A8Srgb}},
    });
  }

  material_buf_ = device_->create_buf_h(rhi::BufferDesc{
      .size = k_max_materials * sizeof(M4Material),
      .bindless = true,
      .name = "all materials buf",
  });

  std::vector<std::vector<Vert>> geos{
      {
          Vert{glm::vec3{-0.5, -0.5, 0}},
          Vert{glm::vec3{0.5, -0.5, 0}},
          Vert{glm::vec3{0.0, 0.5, 0}},
      },
      {

          Vert{glm::vec3{-1., -1., 0}},
          Vert{glm::vec3{1.0, -1.0, 0}},
          Vert{glm::vec3{0.0, 0.0, 0}},
      },
  };

  const char* names[] = {
      "tri 1 buf",
      "tri 2 buf",
  };

  M4Material materials[] = {
      M4Material{.color = glm::vec4{1, 0, 0, 0}},
      M4Material{.color = glm::vec4{0, 1, 0, 0}},
  };

  device_->copy_to_buffer(materials, sizeof(M4Material) * ARRAY_SIZE(materials),
                          material_buf_.handle, 0);

  size_t i = 0;
  for (auto& verts : geos) {
    auto buf = device_->create_buf_h(
        rhi::BufferDesc{.size = sizeof(float) * 3 * 100, .bindless = true, .name = names[i]});
    device_->copy_to_buffer(verts.data(), verts.size() * sizeof(Vert), buf.handle, 0);

    meshes_.emplace_back(std::move(buf), i, verts.size());
    i++;
  }
}

void RendererMetal4::render([[maybe_unused]] const RenderArgs& args) {
  ZoneScoped;
  curr_frame_idx_ = frame_num_ % device_->get_info().frames_in_flight;

  if (!device_->begin_frame(window_->get_window_size())) {
    return;
  }

  rhi::CmdEncoder* enc = device_->begin_command_list();
  enc->begin_rendering({
      rhi::RenderingAttachmentInfo::color_att(device_->get_swapchain().get_texture(curr_frame_idx_),
                                              rhi::LoadOp::Clear, {.color = {1, 0, 0, 1}}),
  });

  enc->bind_pipeline(test_pso_);
  enc->set_viewport({0, 0}, window_->get_window_size());

  for (auto& mesh : meshes_) {
    {
      struct {
        uint32_t vert_buf_idx;
        uint32_t mat_buf_idx;
        uint32_t mat_buf_id;
      } pc{.vert_buf_idx = device_->get_buf(mesh.vertex_buf)->bindless_idx(),
           .mat_buf_idx = device_->get_buf(material_buf_)->bindless_idx(),
           .mat_buf_id = (uint32_t)mesh.material_id};
      enc->push_constants(&pc, sizeof(pc));
    }
    enc->draw_primitives(rhi::PrimitiveTopology::TriangleList, mesh.vertex_count);
  }

  enc->end_encoding();

  device_->submit_frame();

  frame_num_++;
}
