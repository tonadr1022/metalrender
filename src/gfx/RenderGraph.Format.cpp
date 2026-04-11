#include "gfx/RenderGraph.Format.hpp"

#include "gfx/RenderGraph.hpp"

namespace TENG_NAMESPACE {
namespace gfx {
namespace rg_fmt {

std::string to_string(RGPassType type) {
  switch (type) {
    case RGPassType::Compute:
      return "Compute";
    case RGPassType::Graphics:
      return "Graphics";
    case RGPassType::Transfer:
      return "Transfer";
    case RGPassType::None:
      return "None";
  }
  return "None";
}

std::string to_string(rhi::PipelineStage stage) {
  std::string result;
  if (has_flag(stage, rhi::PipelineStage::TopOfPipe)) {
    result += "PipelineStage_TopOfPipe | ";
  }
  if (has_flag(stage, rhi::PipelineStage::DrawIndirect)) {
    result += "PipelineStage_DrawIndirect | ";
  }
  if (has_flag(stage, rhi::PipelineStage::VertexInput)) {
    result += "PipelineStage_VertexInput | ";
  }
  if (has_flag(stage, rhi::PipelineStage::VertexShader)) {
    result += "PipelineStage_VertexShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::TaskShader)) {
    result += "PipelineStage_TaskShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::MeshShader)) {
    result += "PipelineStage_MeshShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::FragmentShader)) {
    result += "PipelineStage_FragmentShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::EarlyFragmentTests)) {
    result += "PipelineStage_EarlyFragmentTests | ";
  }
  if (has_flag(stage, rhi::PipelineStage::LateFragmentTests)) {
    result += "PipelineStage_LateFragmentTests | ";
  }
  if (has_flag(stage, rhi::PipelineStage::ColorAttachmentOutput)) {
    result += "PipelineStage_ColorAttachmentOutput | ";
  }
  if (has_flag(stage, rhi::PipelineStage::ComputeShader)) {
    result += "PipelineStage_ComputeShader | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllTransfer)) {
    result += "PipelineStage_AllTransfer | ";
  }
  if (has_flag(stage, rhi::PipelineStage::BottomOfPipe)) {
    result += "PipelineStage_BottomOfPipe | ";
  }
  if (has_flag(stage, rhi::PipelineStage::Host)) {
    result += "PipelineStage_Host | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllGraphics)) {
    result += "PipelineStage_AllGraphics | ";
  }
  if (has_flag(stage, rhi::PipelineStage::AllCommands)) {
    result += "PipelineStage_AllCommands | ";
  }
  if (result.size()) {
    result = result.substr(0, result.size() - 3);
  }
  return result;
}

std::string to_string(rhi::AccessFlags access) {
  std::string result;
  if (has_flag(access, rhi::AccessFlags::IndirectCommandRead)) {
    result += "IndirectCommandRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::IndexRead)) {
    result += "IndexRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::VertexAttributeRead)) {
    result += "VertexAttributeRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::UniformRead)) {
    result += "UniformRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::InputAttachmentRead)) {
    result += "InputAttachmentRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderRead)) {
    result += "ShaderRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderWrite)) {
    result += "ShaderWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentRead)) {
    result += "ColorAttachmentRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ColorAttachmentWrite)) {
    result += "ColorAttachmentWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilRead)) {
    result += "DepthStencilRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::DepthStencilWrite)) {
    result += "DepthStencilWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::TransferRead)) {
    result += "TransferRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::TransferWrite)) {
    result += "TransferWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::MemoryRead)) {
    result += "MemoryRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::MemoryWrite)) {
    result += "MemoryWrite | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderSampledRead)) {
    result += "ShaderSampledRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageRead)) {
    result += "ShaderStorageRead | ";
  }
  if (has_flag(access, rhi::AccessFlags::ShaderStorageWrite)) {
    result += "ShaderStorageWrite | ";
  }
  if (result.size()) {
    result = result.substr(0, result.size() - 3);
  }
  return result;
}

std::string to_string(rhi::ResourceLayout layout) {
  switch (layout) {
    case rhi::ResourceLayout::Undefined:
      return "Layout_Undefined";
    case rhi::ResourceLayout::General:
      return "Layout_General";
    case rhi::ResourceLayout::ShaderReadOnly:
      return "Layout_ShaderReadOnly";
    case rhi::ResourceLayout::ColorAttachment:
      return "Layout_ColorAttachment";
    case rhi::ResourceLayout::DepthStencil:
      return "Layout_DepthStencil";
    case rhi::ResourceLayout::TransferSrc:
      return "Layout_TransferSrc";
    case rhi::ResourceLayout::TransferDst:
      return "Layout_TransferDst";
    case rhi::ResourceLayout::Present:
      return "Layout_Present";
    case rhi::ResourceLayout::ComputeRW:
      return "Layout_ComputeRW";
    case rhi::ResourceLayout::InputAttachment:
      return "Layout_InputAttachment";
  }
  return "Layout_Unknown";
}

}  // namespace rg_fmt
}  // namespace gfx
}  // namespace TENG_NAMESPACE
