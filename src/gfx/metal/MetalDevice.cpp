#include "MetalDevice.hpp"

#include <QuartzCore/QuartzCore.h>

#include <Foundation/NSObject.hpp>
#include <Metal/MTLRenderPipeline.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>

#include "Window.hpp"
#include "gfx/metal/Metal3CmdEncoder.hpp"
#include "imgui.h"
#include "shader_constants.h"

#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>

#include "MetalUtil.hpp"
#include "core/EAssert.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/metal/MetalPipeline.hpp"
#include "gfx/metal/MetalUtil.hpp"

namespace {
MTL::BlendFactor convert(rhi::BlendFactor factor) {
  switch (factor) {
    case rhi::BlendFactor::Zero:
      return MTL::BlendFactorZero;
    case rhi::BlendFactor::One:
      return MTL::BlendFactorOne;
    case rhi::BlendFactor::SrcColor:
      return MTL::BlendFactorSourceColor;
    case rhi::BlendFactor::OneMinusSrcColor:
      return MTL::BlendFactorOneMinusSourceColor;
    case rhi::BlendFactor::DstColor:
      return MTL::BlendFactorDestinationColor;
    case rhi::BlendFactor::OneMinusDstColor:
      return MTL::BlendFactorOneMinusDestinationColor;
    case rhi::BlendFactor::SrcAlpha:
      return MTL::BlendFactorSourceAlpha;
    case rhi::BlendFactor::OneMinusSrcAlpha:
      return MTL::BlendFactorOneMinusSourceAlpha;
    case rhi::BlendFactor::DstAlpha:
      return MTL::BlendFactorDestinationAlpha;
    case rhi::BlendFactor::OneMinusDstAlpha:
      return MTL::BlendFactorOneMinusDestinationAlpha;
    case rhi::BlendFactor::ConstantColor:
      return MTL::BlendFactorBlendColor;
    case rhi::BlendFactor::OneMinusConstantColor:
      return MTL::BlendFactorOneMinusBlendColor;
    case rhi::BlendFactor::ConstantAlpha:
      return MTL::BlendFactorBlendAlpha;
    case rhi::BlendFactor::OneMinusConstantAlpha:
      return MTL::BlendFactorOneMinusBlendAlpha;
    case rhi::BlendFactor::SrcAlphaSaturate:
      return MTL::BlendFactorSourceAlphaSaturated;
    case rhi::BlendFactor::OneMinusSrc1Color:
      return MTL::BlendFactorOneMinusSource1Color;
    case rhi::BlendFactor::OneMinusSrc1Alpha:
      return MTL::BlendFactorOneMinusSource1Alpha;
    case rhi::BlendFactor::Src1Color:
      return MTL::BlendFactorSource1Color;
    case rhi::BlendFactor::Src1Alpha:
      return MTL::BlendFactorSource1Alpha;
      break;
  }
}

MTL::BlendOperation convert(rhi::BlendOp op) {
  switch (op) {
    case rhi::BlendOp::Add:
      return MTL::BlendOperationAdd;
    case rhi::BlendOp::Subtract:
      return MTL::BlendOperationSubtract;
    case rhi::BlendOp::ReverseSubtract:
      return MTL::BlendOperationReverseSubtract;
    case rhi::BlendOp::Min:
      return MTL::BlendOperationMin;
    case rhi::BlendOp::Max:
      return MTL::BlendOperationMax;
      break;
  }
}

MTL::SamplerAddressMode convert(rhi::AddressMode m) {
  using rhi::AddressMode;
  switch (m) {
    case AddressMode::Repeat:
      return MTL::SamplerAddressModeRepeat;
    case AddressMode::MirroredRepeat:
      return MTL::SamplerAddressModeMirrorRepeat;
    case AddressMode::MirrorClampToEdge:
      return MTL::SamplerAddressModeMirrorClampToEdge;
    case AddressMode::ClampToBorder:
      return MTL::SamplerAddressModeClampToBorderColor;
    case AddressMode::ClampToEdge:
      return MTL::SamplerAddressModeClampToEdge;
  }
}

MTL::SamplerBorderColor convert(rhi::BorderColor c) {
  using rhi::BorderColor;
  switch (c) {
    case BorderColor::FloatOpaqueWhite:
    case BorderColor::IntOpaqueWhite:
      return MTL::SamplerBorderColorOpaqueWhite;
    case BorderColor::FloatOpaqueBlack:
    case BorderColor::IntOpaqueBlack:
      return MTL::SamplerBorderColorOpaqueBlack;
    case BorderColor::FloatTransparentBlack:
    case BorderColor::IntTransparentBlack:
      return MTL::SamplerBorderColorTransparentBlack;
  }
}

MTL::SamplerMinMagFilter convert_filter(rhi::FilterMode m) {
  using rhi::FilterMode;
  switch (m) {
    case FilterMode::Linear:
      return MTL::SamplerMinMagFilterLinear;
    case FilterMode::Nearest:
      return MTL::SamplerMinMagFilterNearest;
  }
}
MTL::SamplerMipFilter convert_mip_filter(rhi::FilterMode m) {
  using rhi::FilterMode;
  switch (m) {
    case FilterMode::Linear:
      return MTL::SamplerMipFilterLinear;
    case FilterMode::Nearest:
      return MTL::SamplerMipFilterNearest;
    default:
      return MTL::SamplerMipFilterNotMipmapped;
  }
}

}  // namespace

void MetalDevice::init(const InitInfo& init_info, const MetalDeviceInitInfo& metal_init_info) {
  // TODO: actually check for mtl4 support
  mtl4_enabled_ = metal_init_info.prefer_mtl4;
  shader_lib_dir_ = init_info.shader_lib_dir;
  shader_lib_dir_ /= "metal";

  auto* win = dynamic_cast<Window*>(init_info.window);
  if (!win) {
    LERROR("invalid window pointer");
    return;
  }
  device_ = MTL::CreateSystemDefaultDevice();
  metal_layer_ =
      init_metal_window(init_info.window->get_handle(), device_, init_info.transparent_window);
  ALWAYS_ASSERT(metal_layer_);

  // TODO: parameterize
  info_.frames_in_flight = 2;

  main_res_set_ = make_residency_set();

  if (mtl4_enabled_) {
    mtl4_resources_.emplace(MTL4_Resources{});
    NS::Error* err{};
    {
      MTL4::CompilerDescriptor* compiler_desc = MTL4::CompilerDescriptor::alloc()->init();
      mtl4_resources_->shader_compiler = device_->newCompiler(compiler_desc, &err);
      compiler_desc->release();
    }
    for (size_t i = 0; i < info_.frames_in_flight; i++) {
      mtl4_resources_->cmd_allocators[i] = device_->newCommandAllocator();
    }
    mtl4_resources_->main_cmd_q = device_->newMTL4CommandQueue();
    mtl4_resources_->main_cmd_q->addResidencySet(main_res_set_);
    mtl4_resources_->main_cmd_buf = device_->newCommandBuffer();
    mtl4_resources_->cmd_lists_.reserve(10);
  } else {
    mtl3_resources_.emplace(MTL3_Resources{});
    mtl3_resources_->main_cmd_q = device_->newCommandQueue();
    mtl3_resources_->cmd_lists_.reserve(10);
  }

  push_constant_allocator_.emplace(1024ul * 1024, this, info_.frames_in_flight);
  test_allocator_.emplace(1024ul * 1024 * 100, this, info_.frames_in_flight);
  arg_buf_allocator_.emplace(1024ul * 1024, this, info_.frames_in_flight);

  init_bindless();

  main_res_set_->requestResidency();

  psos_.dispatch_indirect_pso =
      compile_mtl_compute_pipeline(shader_lib_dir_ / "dispatch_indirect.metallib");
  psos_.dispatch_mesh_pso =
      compile_mtl_compute_pipeline(shader_lib_dir_ / "dispatch_mesh.metallib");

  shared_event_ = device_->newSharedEvent();
}

void MetalDevice::init(const InitInfo& init_info) {
  init(init_info, MetalDeviceInitInfo{.prefer_mtl4 = false});
}

void MetalDevice::shutdown() { device_->release(); }

rhi::BufferHandle MetalDevice::create_buf(const rhi::BufferDesc& desc) {
  auto options = mtl::util::convert(desc.storage_mode);
  // options |= MTL::ResourceHazardTrackingModeUntracked;
  auto* mtl_buf = device_->newBuffer(desc.size, options);
  ALWAYS_ASSERT(mtl_buf);
  if (desc.name) {
    mtl_buf->setLabel(mtl::util::string(desc.name));
  }
  if (mtl4_enabled_) {
    mtl_buf->retain();
  }

  uint32_t idx = rhi::k_invalid_bindless_idx;
  if (desc.bindless) {
    idx = resource_desc_heap_allocator_.alloc_idx();
    IRBufferView bview{};
    bview.buffer = mtl_buf;
    bview.bufferOffset = 0;
    bview.bufferSize = desc.size;
    bview.typedBuffer = false;
    auto metadata = IRDescriptorTableGetBufferMetadata(&bview);
    auto* pResourceTable =
        (IRDescriptorTableEntry*)(get_mtl_buf(resource_descriptor_table_))->contents();
    IRDescriptorTableSetBuffer(&pResourceTable[idx], mtl_buf->gpuAddress(), metadata);
  }
  main_res_set_->addAllocation(mtl_buf);
  main_res_set_->commit();
  req_alloc_sizes_.total_buffer_space_allocated += desc.size;

  return buffer_pool_.alloc(desc, mtl_buf, idx);
}

namespace {

MTL::TextureType get_texture_type(glm::uvec3 dims, size_t array_length) {
  if (dims.z > 1) {
    return MTL::TextureType3D;
  }
  if (array_length > 1) {
    return MTL::TextureType2DArray;
  }
  return MTL::TextureType2D;
}

}  // namespace

rhi::TextureHandle MetalDevice::create_tex(const rhi::TextureDesc& desc) {
  MTL::TextureDescriptor* texture_desc = MTL::TextureDescriptor::alloc()->init();
  texture_desc->setWidth(desc.dims.x);
  texture_desc->setHeight(desc.dims.y);
  texture_desc->setDepth(desc.dims.z);
  texture_desc->setTextureType(get_texture_type(desc.dims, desc.array_length));
  texture_desc->setPixelFormat(mtl::util::convert(desc.format));
  texture_desc->setStorageMode(mtl::util::convert_storage_mode(desc.storage_mode));
  texture_desc->setMipmapLevelCount(desc.mip_levels);
  texture_desc->setArrayLength(desc.array_length);
  // TODO: parameterize this?
  texture_desc->setAllowGPUOptimizedContents(true);
  auto usage = mtl::util::convert(desc.usage);
  if (desc.flags & rhi::TextureDescFlags_PixelFormatView) {
    usage |= MTL::TextureUsagePixelFormatView;
  }
  texture_desc->setUsage(usage);
  auto* tex = device_->newTexture(texture_desc);
  ALWAYS_ASSERT(tex);
  if (mtl4_enabled_) {
    tex->retain();
  }
  if (desc.name) {
    tex->setLabel(mtl::util::string(desc.name));
  }
  texture_desc->release();
  uint32_t idx = rhi::k_invalid_bindless_idx;
  if (desc.bindless) {
    idx = resource_desc_heap_allocator_.alloc_idx();
    auto* resource_table =
        (IRDescriptorTableEntry*)(get_mtl_buf(resource_descriptor_table_))->contents();
    IRDescriptorTableSetTexture(&resource_table[idx], tex, 0.0f, 0);
  }
  main_res_set_->addAllocation(tex);
  main_res_set_->commit();

  return texture_pool_.alloc(desc, idx, tex);
}

void MetalDevice::destroy(rhi::BufferHandle handle) {
  auto* buf = buffer_pool_.get(handle);
  ASSERT(buf);
  if (!buf) {
    return;
  }

  ASSERT(buf->buffer());
  if (buf->buffer()) {
    main_res_set_->removeAllocation(buf->buffer());
    req_alloc_sizes_.total_buffer_space_allocated -= buf->desc().size;
    buf->buffer()->release();
    if (mtl4_enabled_) {
      buf->buffer()->release();
    }
  }

  if (buf->desc().usage & rhi::BufferUsage_Indirect) {
    // TODO: improve this cringe
    icb_mgr_draw_indexed_.remove(handle);
    icb_mgr_draw_mesh_threadgroups_.remove(handle);
  }

  buffer_pool_.destroy(handle);
}

void MetalDevice::destroy(rhi::TextureHandle handle) {
  auto* tex = texture_pool_.get(handle);
  ASSERT(tex);
  if (!tex) {
    return;
  }
  ASSERT(tex->texture());
  if (tex->texture() && !tex->is_drawable_tex()) {
    main_res_set_->removeAllocation(tex->texture());
    tex->texture()->release();
  }
  texture_pool_.destroy(handle);
}

void MetalDevice::destroy(rhi::PipelineHandle handle) {
  auto* e = pipeline_pool_.get(handle);
  if (e) {
    if (e->render_pso) {
      e->render_pso->release();
    }
    if (e->compute_pso) {
      e->compute_pso->release();
    }
  }
}

void MetalDevice::destroy(rhi::SamplerHandle handle) {
  auto* s = sampler_pool_.get(handle);
  if (s) {
    s->sampler()->release();
  }
}

MTL::ResidencySet* MetalDevice::make_residency_set() {
  MTL::ResidencySetDescriptor* desc = MTL::ResidencySetDescriptor::alloc()->init();
  desc->setLabel(mtl::util::string("main residency set"));
  NS::Error* err{};
  MTL::ResidencySet* set = device_->newResidencySet(desc, &err);
  if (err) {
    LERROR("Failed to create residency set");
  }
  return set;
}

namespace {

template <typename T>
void set_shader_stage_functions(const rhi::ShaderCreateInfo& shader_info, T& desc,
                                const char* entry_point_name, MTL::Library* lib) {
  if constexpr (std::is_same_v<T, MTL4::RenderPipelineDescriptor*> ||
                std::is_same_v<T, MTL4::MeshRenderPipelineDescriptor*>) {
    MTL4::LibraryFunctionDescriptor* func_desc = MTL4::LibraryFunctionDescriptor::alloc()->init();
    ASSERT(func_desc);
    func_desc->setLibrary(lib);
    func_desc->setName(mtl::util::string(entry_point_name));
    switch (shader_info.type) {
      case rhi::ShaderType::Fragment: {
        desc->setFragmentFunctionDescriptor(func_desc);
        break;
      }
      case rhi::ShaderType::Vertex: {
        if constexpr (std::is_same_v<T, MTL4::RenderPipelineDescriptor*>) {
          desc->setVertexFunctionDescriptor(func_desc);
        }
        break;
      }
      case rhi::ShaderType::Mesh: {
        if constexpr (std::is_same_v<T, MTL4::MeshRenderPipelineDescriptor*>) {
          desc->setMeshFunctionDescriptor(func_desc);
        }
        break;
      }
      case rhi::ShaderType::Task: {
        if constexpr (std::is_same_v<T, MTL4::MeshRenderPipelineDescriptor*>) {
          desc->setObjectFunctionDescriptor(func_desc);
        }
        break;
      }
      default: {
        LERROR("Invalid shader type for GraphicsPipeline creation: {}",
               to_string(shader_info.type));
      }
    }
  } else {
    MTL::Function* func = lib->newFunction(mtl::util::string(entry_point_name));
    ASSERT(func);
    switch (shader_info.type) {
      case rhi::ShaderType::Fragment:
        desc->setFragmentFunction(func);
        break;
      case rhi::ShaderType::Vertex:
        if constexpr (std::is_same_v<T, MTL::RenderPipelineDescriptor*>) {
          desc->setVertexFunction(func);
        }
        break;
      case rhi::ShaderType::Mesh:
        if constexpr (std::is_same_v<T, MTL::MeshRenderPipelineDescriptor*>) {
          desc->setMeshFunction(func);
        }
        break;
      case rhi::ShaderType::Task:
        if constexpr (std::is_same_v<T, MTL::MeshRenderPipelineDescriptor*>) {
          desc->setObjectFunction(func);
        }
        break;
      default:
        ALWAYS_ASSERT(0 && "unsupported type rn");
    }
  }
  lib->release();
}

}  // namespace

rhi::PipelineHandle MetalDevice::create_graphics_pipeline(
    const rhi::GraphicsPipelineCreateInfo& cinfo) {
  using ShaderType = rhi::ShaderType;

  bool is_vertex_pipeline{};
  for (const auto& shader_info : cinfo.shaders) {
    if (shader_info.type == ShaderType::Vertex) {
      is_vertex_pipeline = true;
      break;
    }
  }

  auto set_color_blend_atts = [&cinfo](auto& desc) {
    int color_format_cnt = 0;
    for (auto format : cinfo.rendering.color_formats) {
      if (format != rhi::TextureFormat::Undefined) {
        color_format_cnt++;
      } else {
        break;
      }
    }
    for (int i = 0; i < color_format_cnt; i++) {
      rhi::TextureFormat format = cinfo.rendering.color_formats[i];
      desc->colorAttachments()->object(i)->setPixelFormat(mtl::util::convert(format));
    }
    if (cinfo.blend.attachments.size() > 0) {
      ALWAYS_ASSERT(cinfo.blend.attachments.size() == (size_t)color_format_cnt);
    }

    for (size_t i = 0; i < cinfo.blend.attachments.size(); i++) {
      const auto& info_att = cinfo.blend.attachments[i];
      auto* att = desc->colorAttachments()->object(i);
      att->setSourceRGBBlendFactor(convert(info_att.src_color_factor));
      att->setDestinationRGBBlendFactor(convert(info_att.dst_color_factor));
      att->setRgbBlendOperation(convert(info_att.color_blend_op));
      att->setSourceAlphaBlendFactor(convert(info_att.src_alpha_factor));
      att->setDestinationAlphaBlendFactor(convert(info_att.dst_alpha_factor));
      att->setAlphaBlendOperation(convert(info_att.alpha_blend_op));
    }
  };

  MTL::RenderPipelineState* result{};
  NS::Error* err{};

  if (mtl4_enabled_) {
    MTL4::RenderPipelineDescriptor* desc = MTL4::RenderPipelineDescriptor::alloc()->init();
    for (const auto& shader_info : cinfo.shaders) {
      if (shader_info.type == ShaderType::None) {
        break;
      }
      set_shader_stage_functions(
          shader_info, desc, "main",
          create_or_get_lib(get_metallib_path_from_shader_info(shader_info)));
    }

    set_color_blend_atts(desc);

    for (size_t i = 0; i < cinfo.blend.attachments.size(); i++) {
      desc->colorAttachments()->object(i)->setBlendingState(
          cinfo.blend.attachments[i].enable ? MTL4::BlendStateEnabled : MTL4::BlendStateDisabled);
    }
    desc->setSupportIndirectCommandBuffers(MTL4::IndirectCommandBufferSupportStateEnabled);

    result = mtl4_resources_->shader_compiler->newRenderPipelineState(desc, nullptr, &err);

  } else {
    auto set_shader_stage_functions_for_desc = [this, &cinfo](auto desc) {
      for (const auto& shader_info : cinfo.shaders) {
        if (shader_info.type == ShaderType::None) {
          break;
        }
        set_shader_stage_functions(
            shader_info, desc, "main",
            create_or_get_lib(get_metallib_path_from_shader_info(shader_info)));
      }
    };

    auto create_pso = [this, &set_shader_stage_functions_for_desc, &set_color_blend_atts, &cinfo](
                          auto desc, NS::Error** err) {
      desc->setDepthAttachmentPixelFormat(mtl::util::convert(cinfo.rendering.depth_format));
      set_shader_stage_functions_for_desc(desc);
      set_color_blend_atts(desc);
      for (size_t i = 0; i < cinfo.blend.attachments.size(); i++) {
        desc->colorAttachments()->object(i)->setBlendingEnabled(cinfo.blend.attachments[i].enable);
      }
      desc->setSupportIndirectCommandBuffers(true);
      if constexpr (std::is_same_v<decltype(desc), MTL::RenderPipelineDescriptor*>) {
        return device_->newRenderPipelineState(desc, err);
      } else {
        return device_->newRenderPipelineState(desc, MTL::PipelineOptionNone, nullptr, err);
      }
    };

    if (!is_vertex_pipeline) {
      MTL::MeshRenderPipelineDescriptor* desc = MTL::MeshRenderPipelineDescriptor::alloc()->init();
      desc->setDepthAttachmentPixelFormat(mtl::util::convert(cinfo.rendering.depth_format));
      result = create_pso(desc, &err);
    } else {
      MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
      result = create_pso(desc, &err);
    }
  }

  if (!result) {
    LERROR("Failed to create MTL3 render pipeline {}", mtl::util::get_err_string(err));
  }

  return pipeline_pool_.alloc(MetalPipeline{result, nullptr});
}

rhi::PipelineHandle MetalDevice::create_compute_pipeline(const rhi::ShaderCreateInfo& cinfo) {
  auto path = (shader_lib_dir_ / cinfo.path).concat(".comp.metallib");
  MTL::ComputePipelineState* pso = compile_mtl_compute_pipeline(path, "main");
  ASSERT(pso);
  return pipeline_pool_.alloc(MetalPipeline{nullptr, pso});
}

rhi::SamplerHandle MetalDevice::create_sampler(const rhi::SamplerDesc& desc) {
  MTL::SamplerDescriptor* samp_desc = MTL::SamplerDescriptor::alloc()->init();
  samp_desc->setBorderColor(convert(desc.border_color));
  if (desc.compare_enable) {
    samp_desc->setCompareFunction(mtl::util::convert(desc.compare_op));
  }
  // TODO: lod handling
  samp_desc->setMipFilter(convert_mip_filter(desc.mipmap_mode));
  samp_desc->setMinFilter(convert_filter(desc.min_filter));
  samp_desc->setMagFilter(convert_filter(desc.mag_filter));
  samp_desc->setSAddressMode(convert(desc.address_mode));
  samp_desc->setTAddressMode(convert(desc.address_mode));
  samp_desc->setRAddressMode(convert(desc.address_mode));
  samp_desc->setSupportArgumentBuffers(true);
  MTL::SamplerState* sampler = device_->newSamplerState(samp_desc);
  samp_desc->release();

  uint32_t bindless_idx{rhi::k_invalid_bindless_idx};
  if (desc.bindless) {
    bindless_idx = sampler_desc_heap_allocator_.alloc_idx();
    auto* resource_table =
        (IRDescriptorTableEntry*)(get_mtl_buf(sampler_descriptor_table_))->contents();
    IRDescriptorTableSetSampler(&resource_table[bindless_idx], sampler, 0.0);
  }
  return sampler_pool_.alloc(desc, sampler, bindless_idx);
}

MTL::Library* MetalDevice::create_or_get_lib(const std::filesystem::path& path) {
  NS::Error* err{};
  auto it = path_to_lib_.find(path.string());
  if (it != path_to_lib_.end()) {
    return it->second;
  }
  auto* lib = device_->newLibrary(mtl::util::string(path), &err);
  if (err) {
    mtl::util::print_err(err);
    return nullptr;
  }
  return lib;
}

rhi::CmdEncoder* MetalDevice::begin_command_list() {
  if (mtl4_enabled_) {
    if (curr_cmd_list_idx_ < mtl4_resources_->cmd_lists_.size()) {
      auto* ret = mtl4_resources_->cmd_lists_[curr_cmd_list_idx_].get();
      curr_cmd_list_idx_++;
      return ret;
    }
    mtl4_resources_->cmd_lists_.emplace_back(
        std::make_unique<MetalCmdEncoder>(this, mtl4_resources_->main_cmd_buf));
    curr_cmd_list_idx_++;
    return mtl4_resources_->cmd_lists_.back().get();
  }

  // MTL3 path
  if (curr_cmd_list_idx_ < mtl3_resources_->cmd_lists_.size()) {
    auto* ret = (Metal3CmdEncoder*)mtl3_resources_->cmd_lists_[curr_cmd_list_idx_].get();
    ret->cmd_buf_ = mtl3_resources_->main_cmd_buf;
    curr_cmd_list_idx_++;
    return ret;
  }

  mtl3_resources_->cmd_lists_.emplace_back(
      std::make_unique<Metal3CmdEncoder>(this, mtl3_resources_->main_cmd_buf));
  curr_cmd_list_idx_++;
  return mtl3_resources_->cmd_lists_.back().get();
}

bool MetalDevice::begin_frame(glm::uvec2 window_dims) {
  main_res_set_->commit();
  frame_ar_pool_ = NS::AutoreleasePool::alloc()->init();
  curr_cmd_list_idx_ = 0;

  {
    // wait on shared event
    if (frame_num_ > info_.frames_in_flight) {
      auto prev_frame = frame_num_ - info_.frames_in_flight;
      if (!shared_event_->waitUntilSignaledValue(prev_frame, 1000ull * 1000)) {
        LERROR("No signaled value from shared event for previous frame: {}", prev_frame);
      }
    }
  }

  icb_mgr_draw_indexed_.reset_for_frame();
  icb_mgr_draw_mesh_threadgroups_.reset_for_frame();

  push_constant_allocator_->reset(frame_idx());
  test_allocator_->reset(frame_idx());
  arg_buf_allocator_->reset(frame_idx());
  ASSERT(metal_layer_);
  curr_drawable_ = metal_layer_->nextDrawable();

  if (!curr_drawable_) {
    return false;
  }

  metal_layer_->setDrawableSize(CGSizeMake(window_dims.x, window_dims.y));

  rhi::TextureDesc swap_img_desc{};
  swapchain_.get_textures()[frame_idx()] =
      rhi::TextureHandleHolder{texture_pool_.alloc(swap_img_desc, rhi::k_invalid_bindless_idx,
                                                   curr_drawable_->texture(), true),
                               this};

  if (mtl4_enabled_) {
    mtl4_resources_->main_cmd_buf->beginCommandBuffer(mtl4_resources_->cmd_allocators[frame_idx()]);
  } else {
    mtl3_resources_->main_cmd_buf = mtl3_resources_->main_cmd_q->commandBuffer();
    mtl3_resources_->main_cmd_buf->useResidencySet(main_res_set_);
  }
  return true;
}

void MetalDevice::submit_frame() {
  if (mtl4_enabled_) {
    mtl4_resources_->main_cmd_buf->endCommandBuffer();

    // wait until drawable ready
    mtl4_resources_->main_cmd_q->wait(curr_drawable_);

    mtl4_resources_->main_cmd_q->commit(&mtl4_resources_->main_cmd_buf, 1);

    mtl4_resources_->main_cmd_q->signalDrawable(curr_drawable_);
    curr_drawable_->present();
    mtl4_resources_->main_cmd_q->signalEvent(shared_event_, frame_num_);
  } else {
    mtl3_resources_->main_cmd_buf->presentDrawable(curr_drawable_);
    mtl3_resources_->main_cmd_buf->encodeSignalEvent(shared_event_, frame_num_);
    std::function<void(MTL::CommandBuffer*)> f = [](MTL::CommandBuffer* cmd) {
      if (cmd->error()) {
        if (cmd->error()->debugDescription()) {
          LINFO("{}", cmd->error()->debugDescription()->cString(NS::UTF8StringEncoding));
        }
        if (cmd->error()->localizedDescription()) {
          LINFO("{}", cmd->error()->localizedDescription()->cString(NS::UTF8StringEncoding));
        }
        if (cmd->error()->localizedFailureReason()) {
          LINFO("{}", cmd->error()->localizedFailureReason()->cString(NS::UTF8StringEncoding));
        }
        if (cmd->error()->domain()) {
          LINFO("{}", cmd->error()->domain()->cString(NS::UTF8StringEncoding));
        }
        LINFO("err cmd buf");
        ALWAYS_ASSERT(0);
      }
    };
    mtl3_resources_->main_cmd_buf->addCompletedHandler(f);

    mtl3_resources_->main_cmd_buf->commit();
  }

  frame_num_++;

  frame_ar_pool_->release();
}

void MetalDevice::init_bindless() {
  auto create_descriptor_table = [this](rhi::BufferHandleHolder* out_buf, size_t count) {
    *out_buf = create_buf_h(
        rhi::BufferDesc{.size = sizeof(IRDescriptorTableEntry) * count, .bindless = false});
  };

  create_descriptor_table(&resource_descriptor_table_, k_max_buffers + k_max_textures);
  create_descriptor_table(&sampler_descriptor_table_, k_max_samplers);
}

void MetalDevice::copy_to_buffer(const void* src, size_t src_size, rhi::BufferHandle buf,
                                 size_t dst_offset) {
  auto* buffer = get_buf(buf);
  ASSERT(buffer);
  if (!buffer) {
    LERROR("[copy_to_buffer]: Buffer not found");
    return;
  }
  ASSERT(buffer->size() - dst_offset >= src_size);
  // TODO: don't assume it's shared on metal
  memcpy((uint8_t*)buffer->contents() + dst_offset, src, src_size);
}

MTL::ComputePipelineState* MetalDevice::compile_mtl_compute_pipeline(
    const std::filesystem::path& path, const char* entry_point) {
  if (mtl4_enabled_) {
    MTL4::ComputePipelineDescriptor* desc = MTL4::ComputePipelineDescriptor::alloc()->init();
    MTL::Library* lib = create_or_get_lib(path);
    ASSERT(lib);
    desc->setSupportIndirectCommandBuffers(MTL4::IndirectCommandBufferSupportStateEnabled);
    MTL4::LibraryFunctionDescriptor* func_desc = MTL4::LibraryFunctionDescriptor::alloc()->init();
    func_desc->setName(mtl::util::string(entry_point));
    func_desc->setLibrary(lib);
    desc->setComputeFunctionDescriptor(func_desc);
    NS::Error* err{};
    auto* pso = mtl4_resources_->shader_compiler->newComputePipelineState(desc, nullptr, &err);
    if (err) {
      LERROR("Failed to create compute pipeline {}", mtl::util::get_err_string(err));
    }
    lib->release();
    desc->release();
    return pso;
  }
  // MTL3 path
  MTL::ComputePipelineDescriptor* desc = MTL::ComputePipelineDescriptor::alloc()->init();

  MTL::Library* lib = create_or_get_lib(path);
  desc->setSupportIndirectCommandBuffers(true);
  desc->setComputeFunction(lib->newFunction(mtl::util::string(entry_point)));
  desc->setLabel(mtl::util::string(path / entry_point));
  NS::Error* err{};
  auto* pso = device_->newComputePipelineState(desc, MTL::PipelineOptionNone, nullptr, &err);
  if (err) {
    mtl::util::print_err(err);
    exit(1);
  }

  lib->release();
  desc->release();
  return pso;
  return {};
}

void MetalDevice::set_vsync(bool vsync) {
  [(CAMetalLayer*)metal_layer_ setDisplaySyncEnabled:vsync];
}

bool MetalDevice::get_vsync() const { return [(CAMetalLayer*)metal_layer_ displaySyncEnabled]; }

MetalDevice::ICB_Mgr::ICB_Alloc MetalDevice::ICB_Mgr::alloc(rhi::BufferHandle indirect_buf_handle,
                                                            uint32_t draw_cnt) {
  auto it = indirect_buffer_handle_to_icb_.find(indirect_buf_handle.to64());
  if (it == indirect_buffer_handle_to_icb_.end()) {
    it = indirect_buffer_handle_to_icb_.emplace(indirect_buf_handle.to64(), ICB_Data{}).first;
  }
  uint32_t indirect_buf_id = it->second.curr_id;
  it->second.curr_id++;

  std::vector<MTL::IndirectCommandBuffer*> icbs{};
  if (indirect_buf_id < it->second.icbs.size()) {
    icbs = it->second.icbs[indirect_buf_id];
  } else {
    for (size_t curr_draw_cnt_offset = 0, rem_draws = draw_cnt; curr_draw_cnt_offset < draw_cnt;
         curr_draw_cnt_offset += 1000, rem_draws -= 1000) {
      MTL::IndirectCommandBufferDescriptor* desc =
          MTL::IndirectCommandBufferDescriptor::alloc()->init();
      // TODO: try not to do this at least for mesh shaders
      desc->setInheritBuffers(false);
      desc->setInheritPipelineState(true);
      desc->setCommandTypes(cmd_types_);
      if (cmd_types_ & MTL::IndirectCommandTypeDrawIndexed) {
        desc->setMaxVertexBufferBindCount(5);
        desc->setMaxFragmentBufferBindCount(5);
      } else if (cmd_types_ & MTL::IndirectCommandTypeDrawMeshThreadgroups) {
        desc->setMaxObjectBufferBindCount(3);
        desc->setMaxMeshBufferBindCount(3);
        desc->setMaxFragmentBufferBindCount(3);
      }
      auto* icb = device_->get_device()->newIndirectCommandBuffer(
          desc, std::min<uint32_t>(1000, rem_draws), MTL::ResourceStorageModeShared);
      if (device_->mtl4_enabled_) {
        icb->retain();
      }
      device_->get_main_residency_set()->addAllocation(icb);
      device_->get_main_residency_set()->commit();

      icbs.emplace_back(icb);
      desc->release();
    }
    it->second.icbs.emplace_back(icbs);
  }

  return {.id = indirect_buf_id, .icbs = icbs};
}

const ICBs& MetalDevice::ICB_Mgr::get(rhi::BufferHandle indirect_buf, uint32_t id) {
  auto it = indirect_buffer_handle_to_icb_.find(indirect_buf.to64());
  ASSERT(it != indirect_buffer_handle_to_icb_.end());
  ASSERT(id < it->second.icbs.size());
  return it->second.icbs[id];
}

void MetalDevice::ICB_Mgr::reset_for_frame() {
  for (auto& [k, v] : indirect_buffer_handle_to_icb_) {
    v.curr_id = 0;
  }
}

void MetalDevice::ICB_Mgr::remove(rhi::BufferHandle indirect_buf) {
  indirect_buffer_handle_to_icb_.erase(indirect_buf.to64());
}

void MetalDevice::fill_buffer(rhi::BufferHandle handle, size_t size, size_t offset,
                              uint32_t fill_value) {
  // TODO: gpu only buffers!
  memset((uint8_t*)get_buf(handle)->contents() + offset, fill_value, size);
}

void MetalDevice::set_name(rhi::BufferHandle handle, const char* name) {
  get_mtl_buf(handle)->setLabel(mtl::util::string(name));
}

void MetalDevice::on_imgui() {
  ImGui::Text("Active Textures: %zu\nActive Buffers: %zu\nGPU Buffer Space Requested: %zu",
              texture_pool_.size(), buffer_pool_.size(),
              req_alloc_sizes_.total_buffer_space_allocated);
}

std::filesystem::path MetalDevice::get_metallib_path_from_shader_info(
    const rhi::ShaderCreateInfo& shader_info) {
  const char* type_str{};
  switch (shader_info.type) {
    case rhi::ShaderType::Fragment:
      type_str = "frag";
      break;
    case rhi::ShaderType::Vertex:
      type_str = "vert";
      break;
    case rhi::ShaderType::Mesh:
      type_str = "mesh";
      break;
    case rhi::ShaderType::Task:
      type_str = "task";
      break;
    default:
      ASSERT(0);
      break;
  }
  return (shader_lib_dir_ / shader_info.path).concat(".").concat(type_str).concat(".metallib");
}
