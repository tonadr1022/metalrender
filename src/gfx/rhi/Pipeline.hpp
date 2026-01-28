#pragma once

#include "gfx/rhi/GFXTypes.hpp"

namespace rhi {

enum class ShaderType : uint8_t { None, Vertex, Fragment, Task, Mesh, Compute };

const char* to_string(ShaderType type);

struct ShaderCreateInfo {
  std::string path;
  ShaderType type;
  std::vector<std::string> defines;
  std::string entry_point{"main"};
};

struct GraphicsPipelineCreateInfo {
  struct Rasterization {
    bool depth_clamp{false};
    bool depth_bias{false};
    bool rasterize_discard_enable{false};
    PolygonMode polygon_mode{PolygonMode::Fill};
    float line_width{1.};
    float depth_bias_constant_factor{};
    float depth_bias_clamp{};
    float depth_bias_slope_factor{};
  };
  struct ColorBlendAttachment {
    bool enable{false};
    BlendFactor src_color_factor;
    BlendFactor dst_color_factor;
    BlendOp color_blend_op;
    BlendFactor src_alpha_factor;
    BlendFactor dst_alpha_factor;
    BlendOp alpha_blend_op;
    ColorComponentFlags color_write_mask{ColorComponentRBit | ColorComponentGBit |
                                         ColorComponentBBit | ColorComponentABit};
  };
  struct Blend {
    bool logic_op_enable{false};
    LogicOp logic_op{LogicOpCopy};
    // TODO: fixed vector
    std::vector<ColorBlendAttachment> attachments;
    float blend_constants[4]{};
  };
  struct Multisample {
    // TODO: flesh out, for now not caring about it
    SampleCountFlagBits rasterization_samples{SampleCount1Bit};
    float min_sample_shading{0.};
    bool sample_shading_enable{false};
    bool alpha_to_coverage_enable{false};
    bool alpha_to_one_enable{false};
  };

  struct StencilOpState {
    StencilOp fail_op{};
    StencilOp pass_op{};
    StencilOp depth_fail_op{};
    CompareOp compare_op{};
    uint32_t compare_mask{};
    uint32_t write_mask{};
    uint32_t reference{};
  };

  struct RenderingInfo {
    std::array<TextureFormat, 5> color_formats{TextureFormat::Undefined, TextureFormat::Undefined,
                                               TextureFormat::Undefined, TextureFormat::Undefined,
                                               TextureFormat::Undefined};
    TextureFormat depth_format{TextureFormat::Undefined};
    TextureFormat stencil_format{TextureFormat::Undefined};
  };

  struct DepthStencil {
    StencilOpState stencil_front{};
    StencilOpState stencil_back{};
    float min_depth_bounds{0.};
    float max_depth_bounds{1.};
    bool depth_test_enable{false};
    bool depth_write_enable{false};
    CompareOp depth_compare_op{CompareOp::Never};
    bool depth_bounds_test_enable{false};
    bool stencil_test_enable{false};
  };

  std::array<ShaderCreateInfo, 3> shaders{};

  PrimitiveTopology topology{PrimitiveTopology::TriangleList};
  RenderingInfo rendering{};
  Rasterization rasterization{};
  Blend blend{};
  Multisample multisample{};
  DepthStencil depth_stencil{};

  static constexpr DepthStencil depth_disable() { return DepthStencil{.depth_test_enable = false}; }
  static constexpr DepthStencil depth_enable(bool write_enable, CompareOp op) {
    return DepthStencil{
        .depth_test_enable = true, .depth_write_enable = write_enable, .depth_compare_op = op};
  }
  std::string name;
};

// struct ComputePipelineCreateInfo {
//   ShaderCreateInfo info;
//   std::string name;
// };

class Pipeline {
 public:
  Pipeline() = default;
  virtual ~Pipeline() = default;
};

}  // namespace rhi
