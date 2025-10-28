#include "RendererMetal4.hpp"

#include <Foundation/NSAutoreleasePool.hpp>
#include <QuartzCore/CAMetalLayer.hpp>
#include <tracy/Tracy.hpp>

#include "WindowApple.hpp"
#include "gfx/CmdEncoder.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/metal/MetalDevice.hpp"

void RendererMetal4::init(const CreateInfo& cinfo) {
  device_ = cinfo.device;
  window_ = cinfo.window;

  {
    test_pso_ = device_->create_graphics_pipeline_h(rhi::GraphicsPipelineCreateInfo{
        .shaders =
            {
                rhi::ShaderCreateInfo{
                    .type = rhi::ShaderType::Vertex,
                    .entry_point = "vertex_main",
                },
                rhi::ShaderCreateInfo{
                    .type = rhi::ShaderType::Fragment,
                    .entry_point = "frag_main",
                },
            },
        .rendering = {.color_formats{rhi::TextureFormat::R8G8B8A8Srgb}},
    });
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
                                              rhi::LoadOp::DontCare, {.color = {1, 0, 0, 1}}),
  });

  enc->set_viewport({0, 0}, window_->get_window_size());

  enc->end_encoding();
  device_->submit_frame();

  frame_num_++;
}
