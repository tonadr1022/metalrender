#pragma once

#include "Buffer.hpp"
#include "RendererTypes.hpp"
#include "Texture.hpp"

namespace MTL {
class Texture;
}

namespace rhi {

struct GraphicsPipelineCreateInfo;
class Device;
class CmdEncoder;

class Device {
 public:
  virtual ~Device() = default;
  [[nodiscard]] virtual void* get_native_device() const = 0;
  virtual void shutdown() = 0;

  virtual BufferHandle create_buf(const rhi::BufferDesc& desc) = 0;
  virtual BufferHandleHolder create_buf_h(const rhi::BufferDesc& desc) = 0;
  virtual TextureHandle create_tex(const rhi::TextureDesc& desc) = 0;
  virtual TextureHandleHolder create_tex_h(const rhi::TextureDesc& desc) = 0;
  virtual Texture* get_tex(TextureHandle handle) = 0;
  virtual Texture* get_tex(const TextureHandleHolder& handle) = 0;
  virtual Buffer* get_buf(const BufferHandleHolder& handle) = 0;
  virtual Buffer* get_buf(BufferHandle handle) = 0;
  virtual rhi::Pipeline* get_pipeline(const rhi::PipelineHandleHolder& handle) = 0;
  virtual rhi::Pipeline* get_pipeline(rhi::PipelineHandle handle) = 0;
  virtual void destroy(BufferHandle handle) = 0;
  virtual void destroy(PipelineHandle handle) = 0;
  virtual CmdEncoder* begin_command_list() = 0;

  virtual rhi::PipelineHandle create_graphics_pipeline(
      const rhi::GraphicsPipelineCreateInfo& cinfo) = 0;
  virtual rhi::PipelineHandleHolder create_graphics_pipeline_h(
      const rhi::GraphicsPipelineCreateInfo& cinfo) = 0;
  virtual void destroy(TextureHandle handle) = 0;
};

}  // namespace rhi
