#include "VkUtil.hpp"

#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {
namespace gfx::vk {

VkImageUsageFlags convert(rhi::TextureUsageFlags usage) {
  VkImageUsageFlags flags{};

  if (usage & rhi::TextureUsageStorage) {
    flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  if (usage & rhi::TextureUsageSample) {
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (usage & rhi::TextureUsageColorAttachment) {
    flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (usage & rhi::TextureUsageDepthStencilAttachment) {
    flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  if (usage & rhi::TextureUsageTransferSrc) {
    flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if (usage & rhi::TextureUsageTransferDst) {
    flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  return flags;
}

VkPipelineStageFlags2 convert(rhi::PipelineStage stage) {
  VkPipelineStageFlags2 flags{};

  if (stage & rhi::PipelineStage_TopOfPipe) {
    flags |= VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
  }
  if (stage & rhi::PipelineStage_DrawIndirect) {
    flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
  }
  if (stage & rhi::PipelineStage_VertexInput) {
    flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
  }
  if (stage & rhi::PipelineStage_VertexShader) {
    flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
  }
  if (stage & rhi::PipelineStage_TaskShader) {
    flags |= VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
  }
  if (stage & rhi::PipelineStage_MeshShader) {
    flags |= VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
  }
  if (stage & rhi::PipelineStage_FragmentShader) {
    flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  }
  if (stage & rhi::PipelineStage_EarlyFragmentTests) {
    flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
  }
  if (stage & rhi::PipelineStage_LateFragmentTests) {
    flags |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  }
  if (stage & rhi::PipelineStage_ColorAttachmentOutput) {
    flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  }
  if (stage & rhi::PipelineStage_ComputeShader) {
    flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  }
  if (stage & rhi::PipelineStage_AllTransfer) {
    flags |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
  }
  if (stage & rhi::PipelineStage_BottomOfPipe) {
    flags |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
  }
  if (stage & rhi::PipelineStage_Host) {
    flags |= VK_PIPELINE_STAGE_2_HOST_BIT;
  }
  if (stage & rhi::PipelineStage_AllGraphics) {
    flags |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
  }
  if (stage & rhi::PipelineStage_AllCommands) {
    flags |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  }

  return flags;
}

VkAccessFlags2 convert(rhi::AccessFlags access) {
  VkAccessFlags2 flags{};

  if (access & rhi::AccessFlags_IndirectCommandRead) {
    flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
  }
  if (access & rhi::AccessFlags_IndexRead) {
    flags |= VK_ACCESS_2_INDEX_READ_BIT;
  }
  if (access & rhi::AccessFlags_VertexAttributeRead) {
    flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
  }
  if (access & rhi::AccessFlags_UniformRead) {
    flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
  }
  if (access & rhi::AccessFlags_InputAttachmentRead) {
    flags |= VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;
  }
  if (access & rhi::AccessFlags_ShaderRead) {
    flags |= VK_ACCESS_2_SHADER_READ_BIT;
  }
  if (access & rhi::AccessFlags_ShaderWrite) {
    flags |= VK_ACCESS_2_SHADER_WRITE_BIT;
  }
  if (access & rhi::AccessFlags_ColorAttachmentRead) {
    flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
  }
  if (access & rhi::AccessFlags_ColorAttachmentWrite) {
    flags |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  }
  if (access & rhi::AccessFlags_DepthStencilRead) {
    flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  }
  if (access & rhi::AccessFlags_DepthStencilWrite) {
    flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }
  if (access & rhi::AccessFlags_TransferRead) {
    flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
  }
  if (access & rhi::AccessFlags_TransferWrite) {
    flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
  }
  if (access & rhi::AccessFlags_HostRead) {
    flags |= VK_ACCESS_2_HOST_READ_BIT;
  }
  if (access & rhi::AccessFlags_HostWrite) {
    flags |= VK_ACCESS_2_HOST_WRITE_BIT;
  }
  if (access & rhi::AccessFlags_MemoryRead) {
    flags |= VK_ACCESS_2_MEMORY_READ_BIT;
  }
  if (access & rhi::AccessFlags_MemoryWrite) {
    flags |= VK_ACCESS_2_MEMORY_WRITE_BIT;
  }
  if (access & rhi::AccessFlags_ShaderSampledRead) {
    flags |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
  }
  if (access & rhi::AccessFlags_ShaderStorageRead) {
    flags |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
  }
  if (access & rhi::AccessFlags_ShaderStorageWrite) {
    flags |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
  }

  return flags;
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
