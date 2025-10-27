#include "RendererMetal4.hpp"

#include "gfx/Pipeline.hpp"
#include "gfx/metal/MetalDevice.hpp"

void RendererMetal4::init(const CreateInfo& cinfo) {
  device_ = cinfo.device;
  window_ = cinfo.window;
  raw_device_ = device_->get_device();

  {
    device_->create_graphics_pipeline(rhi::GraphicsPipelineCreateInfo{
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

void RendererMetal4::render([[maybe_unused]] const RenderArgs& args) {}
