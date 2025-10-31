#include "RendererMetal4.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <QuartzCore/CAMetalLayer.hpp>
#include <tracy/Tracy.hpp>

#include "WindowApple.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/metal/MetalDevice.hpp"

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

  tri_buf_ = device_->create_buf_h(
      rhi::BufferDesc{.size = sizeof(float) * 3 * 100, .alloc_gpu_slot = true});

  std::vector<Vert> verts{{
      Vert{glm::vec3{-0.5, -0.5, 0}},
      Vert{glm::vec3{0.5, -0.5, 0}},
      Vert{glm::vec3{0.0, 0.5, 0}},
  }};

  device_->copy_to_buffer(verts.data(), verts.size() * sizeof(Vert), tri_buf_.handle, 0);
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
  {
    struct {
      uint32_t vert_buf_idx;
      uint32_t color_buf_idx;
    } pc{.vert_buf_idx = device_->get_buf(tri_buf_)->bindless_idx()};
    enc->push_constants(&pc, sizeof(pc));
  }
  enc->draw_primitives(rhi::PrimitiveTopology::TriangleList, 3);

  enc->set_viewport({0, 0}, window_->get_window_size());

  enc->end_encoding();
  device_->submit_frame();

  frame_num_++;
}
