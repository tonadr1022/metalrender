#include "VkUtil.hpp"

#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {
namespace gfx::vk {

VkImageUsageFlags convert(rhi::TextureUsage usage) {
  VkImageUsageFlags flags{};

  if (has_flag(usage, rhi::TextureUsage::Storage)) {
    flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  if (has_flag(usage, rhi::TextureUsage::ShaderWrite)) {
    flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  if (has_flag(usage, rhi::TextureUsage::Sample)) {
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (has_flag(usage, rhi::TextureUsage::ColorAttachment)) {
    flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (has_flag(usage, rhi::TextureUsage::DepthStencilAttachment)) {
    flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  if (has_flag(usage, rhi::TextureUsage::TransferSrc)) {
    flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if (has_flag(usage, rhi::TextureUsage::TransferDst)) {
    flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  return flags;
}

VkPipelineStageFlags2 convert(rhi::PipelineStage stage) {
  VkPipelineStageFlags2 flags{};

  if (has_flag(stage, rhi::PipelineStage::TopOfPipe)) {
    flags |= VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::DrawIndirect)) {
    flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::VertexInput)) {
    flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::IndexInput)) {
    flags |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::VertexShader)) {
    flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::TaskShader)) {
    flags |= VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
  }
  if (has_flag(stage, rhi::PipelineStage::MeshShader)) {
    flags |= VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
  }
  if (has_flag(stage, rhi::PipelineStage::FragmentShader)) {
    flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::EarlyFragmentTests)) {
    flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::LateFragmentTests)) {
    flags |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::ColorAttachmentOutput)) {
    flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::ComputeShader)) {
    flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::AllTransfer)) {
    flags |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::BottomOfPipe)) {
    flags |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::Host)) {
    flags |= VK_PIPELINE_STAGE_2_HOST_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::AllGraphics)) {
    flags |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
  }
  if (has_flag(stage, rhi::PipelineStage::AllCommands)) {
    flags |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  }

  return flags;
}

VkAccessFlags2 convert(rhi::AccessFlags access) {
  VkAccessFlags2 flags{};
  if (has_flag(access, rhi::AccessFlags::IndirectCommandRead)) {
    flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::IndexRead)) {
    flags |= VK_ACCESS_2_INDEX_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::VertexAttributeRead)) {
    flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::UniformRead)) {
    flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::InputAttachmentRead)) {
    flags |= VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderRead)) {
    flags |= VK_ACCESS_2_SHADER_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderWrite)) {
    flags |= VK_ACCESS_2_SHADER_WRITE_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentRead)) {
    flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentWrite)) {
    flags |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilRead)) {
    flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilWrite)) {
    flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::TransferRead)) {
    flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::TransferWrite)) {
    flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::HostRead)) {
    flags |= VK_ACCESS_2_HOST_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::HostWrite)) {
    flags |= VK_ACCESS_2_HOST_WRITE_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::MemoryRead)) {
    flags |= VK_ACCESS_2_MEMORY_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::MemoryWrite)) {
    flags |= VK_ACCESS_2_MEMORY_WRITE_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderSampledRead)) {
    flags |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageRead)) {
    flags |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageWrite)) {
    flags |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
  }

  return flags;
}

VkPrimitiveTopology convert_prim_topology(rhi::PrimitiveTopology top) {
  switch (top) {
    default:
    case rhi::PrimitiveTopology::PointList:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case rhi::PrimitiveTopology::LineList:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case rhi::PrimitiveTopology::TriangleList:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case rhi::PrimitiveTopology::LineStrip:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case rhi::PrimitiveTopology::TriangleStrip:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case rhi::PrimitiveTopology::TriangleFan:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case rhi::PrimitiveTopology::PatchList:
      assert(0);
      return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
  }
}
VkCullModeFlags convert(rhi::CullMode cull_mode) {
  switch (cull_mode) {
    case rhi::CullMode::None:
    default:
      return VK_CULL_MODE_NONE;
    case rhi::CullMode::Front:
      return VK_CULL_MODE_FRONT_BIT;
    case rhi::CullMode::Back:
      return VK_CULL_MODE_BACK_BIT;
  }
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
