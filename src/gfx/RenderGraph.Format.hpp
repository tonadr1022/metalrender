#pragma once

#include <string>

#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {
namespace gfx {

enum class RGPassType;

namespace rg_fmt {

std::string to_string(rhi::PipelineStage stage);
std::string to_string(rhi::AccessFlags access);
std::string to_string(rhi::ResourceLayout layout);
std::string to_string(RGPassType type);

}  // namespace rg_fmt

}  // namespace gfx
}  // namespace TENG_NAMESPACE
