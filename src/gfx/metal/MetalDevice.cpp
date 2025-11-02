#include "MetalDevice.hpp"

#include <Foundation/NSObject.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>

#include "shader_constants.h"

#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>

#include "MetalUtil.hpp"
#include "WindowApple.hpp"
#include "core/EAssert.hpp"
#include "core/Util.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/Pipeline.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/metal/MetalPipeline.hpp"
#include "gfx/metal/MetalUtil.hpp"

namespace {

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

void MetalDevice::init(Window* window, std::filesystem::path shader_lib_dir) {
  shader_lib_dir_ = std::move(shader_lib_dir);
  shader_lib_dir_ /= "metal";

  auto* win = dynamic_cast<WindowApple*>(window);
  if (!win) {
    LERROR("invalid window pointer");
    return;
  }
  device_ = MTL::CreateSystemDefaultDevice();
  ar_pool_ = NS::AutoreleasePool::alloc()->init();
  NS::Error* err{};
  {
    MTL4::CompilerDescriptor* compiler_desc = MTL4::CompilerDescriptor::alloc()->init();
    shader_compiler_ = device_->newCompiler(compiler_desc, &err);
    compiler_desc->release();
  }

  // TODO: parameterize
  info_.frames_in_flight = 2;
  for (size_t i = 0; i < info_.frames_in_flight; i++) {
    cmd_allocators_[i] = device_->newCommandAllocator();
  }

  main_res_set_ = make_residency_set();
  cmd_lists_.reserve(10);
  main_cmd_q_ = device_->newMTL4CommandQueue();
  main_cmd_q_->addResidencySet(main_res_set_);
  main_cmd_buf_ = device_->newCommandBuffer();

  push_constant_allocator_.emplace(1024ul * 1024, this, info_.frames_in_flight);
  arg_buf_allocator_.emplace(1024ul * 1024, this, info_.frames_in_flight);

  init_bindless();

  main_res_set_->requestResidency();
}

void MetalDevice::shutdown() {
  ar_pool_->release();
  device_->release();
}

rhi::BufferHandle MetalDevice::create_buf(const rhi::BufferDesc& desc) {
  auto options = mtl::util::convert(desc.storage_mode);
  auto* mtl_buf = device_->newBuffer(desc.size, options);
  if (desc.name) {
    mtl_buf->setLabel(mtl::util::string(desc.name));
  }
  mtl_buf->retain();

  uint32_t idx = rhi::k_invalid_bindless_idx;
  if (desc.bindless) {
    idx = buffer_index_allocator_.alloc_idx();
    IRBufferView bview{};
    bview.buffer = mtl_buf;
    bview.bufferOffset = 0;
    bview.bufferSize = desc.size;
    bview.typedBuffer = false;
    auto metadata = IRDescriptorTableGetBufferMetadata(&bview);
    auto* pResourceTable =
        (IRDescriptorTableEntry*)(get_mtl_buf(buffer_descriptor_table_))->contents();
    IRDescriptorTableSetBuffer(&pResourceTable[idx], mtl_buf->gpuAddress(), metadata);
  }
  main_res_set_->addAllocation(mtl_buf);

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
  texture_desc->setStorageMode(mtl::util::convert(desc.storage_mode));
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
  tex->retain();
  texture_desc->release();
  uint32_t idx = rhi::k_invalid_bindless_idx;
  if (desc.bindless) {
    idx = texture_index_allocator_.alloc_idx();
    auto* resource_table =
        (IRDescriptorTableEntry*)(get_mtl_buf(texture_descriptor_table_))->contents();
    IRDescriptorTableSetTexture(&resource_table[idx], tex, 0.0f, 0);
  }
  main_res_set_->addAllocation(tex);

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
    buf->buffer()->release();
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

rhi::PipelineHandle MetalDevice::create_graphics_pipeline(
    const rhi::GraphicsPipelineCreateInfo& cinfo) {
  using ShaderType = rhi::ShaderType;
  MTL4::RenderPipelineDescriptor* desc = MTL4::RenderPipelineDescriptor::alloc()->init();
  // TODO: LMAO this is cursed
  for (const auto& shader_info : cinfo.shaders) {
    const char* type_str{};
    switch (shader_info.type) {
      case rhi::ShaderType::Fragment:
        type_str = "frag";
        break;
      case rhi::ShaderType::Vertex:
        type_str = "vert";
        break;
      default:
        type_str = "";
        break;
    }
    auto path = (shader_lib_dir_ / shader_info.path)
                    .concat("_")
                    .concat(type_str)
                    .replace_extension(".metallib");
    MTL::Library* lib = create_or_get_lib(path);
    MTL4::LibraryFunctionDescriptor* func_desc = MTL4::LibraryFunctionDescriptor::alloc()->init();
    func_desc->setLibrary(lib);
    switch (shader_info.type) {
      case ShaderType::Fragment: {
        func_desc->setName(mtl::util::string("frag_main"));
        desc->setFragmentFunctionDescriptor(func_desc);
        break;
      }
      case ShaderType::Vertex: {
        func_desc->setName(mtl::util::string("vert_main"));
        desc->setVertexFunctionDescriptor(func_desc);
        break;
      }
      default: {
        LERROR("Invalid shader type for GraphicsPipeline creation: {}",
               to_string(shader_info.type));
      }
    }
    lib->release();
  }

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

  desc->setSupportIndirectCommandBuffers(MTL4::IndirectCommandBufferSupportStateEnabled);
  NS::Error* err{};

  auto* result = shader_compiler_->newRenderPipelineState(desc, nullptr, &err);
  auto* reflection = result->reflection();
  for (size_t i = 0; i < reflection->vertexBindings()->count(); i++) {
    auto* binding = (MTL::Binding*)reflection->vertexBindings()->object(i);
    LINFO("vertex binding name: {}", binding->name()->cString(NS::ASCIIStringEncoding));
  }
  if (!result) {
    LERROR("Failed to create render pipeline {}", mtl::util::get_err_string(err));
  }

  auto handle = pipeline_pool_.alloc(MetalPipeline{.render_pso = result});

  return handle;
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
    bindless_idx = sampler_index_allocator_.alloc_idx();
    auto* resource_table =
        (IRDescriptorTableEntry*)(get_mtl_buf(sampler_descriptor_table_))->contents();
    // TODO: lod bias
    IRDescriptorTableSetSampler(&resource_table[bindless_idx], sampler, 0.0);
  }
  return sampler_pool_.alloc(desc, sampler, bindless_idx);
}

rhi::PipelineHandleHolder MetalDevice::create_graphics_pipeline_h(
    const rhi::GraphicsPipelineCreateInfo& cinfo) {
  return rhi::PipelineHandleHolder{create_graphics_pipeline(cinfo), this};
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
  if (curr_cmd_list_idx_ < cmd_lists_.size()) {
    auto* ret = cmd_lists_[curr_cmd_list_idx_].get();
    curr_cmd_list_idx_++;
    return ret;
  }
  cmd_lists_.emplace_back(
      std::make_unique<MetalCmdEncoder>(this, main_cmd_buf_, top_level_arg_enc_));
  curr_cmd_list_idx_++;
  return cmd_lists_.back().get();
}

bool MetalDevice::begin_frame(glm::uvec2 window_dims) {
  main_res_set_->commit();
  frame_ar_pool_ = NS::AutoreleasePool::alloc()->init();
  curr_cmd_list_idx_ = 0;
  push_constant_allocator_->reset(frame_idx());
  arg_buf_allocator_->reset(frame_idx());
  rhi::TextureDesc swap_img_desc{};
  ASSERT(metal_layer_);
  curr_drawable_ = metal_layer_->nextDrawable();
  curr_frame_push_constant_buf_offset_bytes_ = 0;

  if (!curr_drawable_) {
    return false;
  }

  metal_layer_->setDrawableSize(CGSizeMake(window_dims.x, window_dims.y));

  swapchain_.get_textures()[frame_idx()] =
      rhi::TextureHandleHolder{texture_pool_.alloc(swap_img_desc, rhi::k_invalid_bindless_idx,
                                                   curr_drawable_->texture(), true),
                               this};

  main_cmd_buf_->beginCommandBuffer(cmd_allocators_[frame_idx()]);
  return true;
}

void MetalDevice::submit_frame() {
  main_cmd_buf_->endCommandBuffer();

  // wait until drawable ready
  main_cmd_q_->wait(curr_drawable_);

  main_cmd_q_->commit(&main_cmd_buf_, 1);

  main_cmd_q_->signalDrawable(curr_drawable_);

  curr_drawable_->present();

  frame_num_++;
}

void MetalDevice::init_bindless() {
  auto create_descriptor_table = [this](rhi::BufferHandleHolder* out_buf, size_t count,
                                        bool read_only = false) -> MTL::ArgumentEncoder* {
    *out_buf = create_buf_h(
        rhi::BufferDesc{.size = sizeof(IRDescriptorTableEntry) * count, .bindless = false});
    MTL::ArgumentDescriptor* arg0 = MTL::ArgumentDescriptor::alloc()->init();
    arg0->setIndex(0);
    arg0->setAccess(read_only ? MTL::ArgumentAccessReadOnly : MTL::ArgumentAccessReadWrite);
    arg0->setArrayLength(count);
    arg0->setDataType(MTL::DataTypePointer);

    NS::Object* args_arr[] = {arg0};
    const NS::Array* args = NS::Array::array(args_arr, ARRAY_SIZE(args_arr));
    auto* arg_enc = device_->newArgumentEncoder(args);
    arg_enc->setArgumentBuffer(get_mtl_buf(*out_buf), 0);
    return arg_enc;
  };

  buffer_arg_enc_ = create_descriptor_table(&buffer_descriptor_table_, k_max_buffers);
  texture_arg_enc_ = create_descriptor_table(&texture_descriptor_table_, k_max_textures);
  sampler_arg_enc_ = create_descriptor_table(&sampler_descriptor_table_, k_max_samplers, true);

  {
    // push constant buffer
    MTL::ArgumentDescriptor* arg0 = MTL::ArgumentDescriptor::alloc()->init();
    arg0->setIndex(0);
    arg0->setAccess(MTL::ArgumentAccessReadOnly);
    arg0->setDataType(MTL::DataTypePointer);

    // top level arg buffer
    MTL::ArgumentDescriptor* arg1 = MTL::ArgumentDescriptor::alloc()->init();
    arg1->setIndex(1);
    arg1->setAccess(MTL::ArgumentAccessReadWrite);
    arg1->setDataType(MTL::DataTypePointer);

    NS::Object* args_arr[] = {arg0, arg1};
    const NS::Array* args = NS::Array::array(args_arr, ARRAY_SIZE(args_arr));
    top_level_arg_enc_ = device_->newArgumentEncoder(args);
    for (auto& i : args_arr) {
      i->release();
    }
  }
}

void MetalDevice::copy_to_buffer(void* src, size_t src_size, rhi::BufferHandle buf,
                                 size_t dst_offset) {
  auto* buffer = get_buf(buf);
  ASSERT(buffer);
  if (!buffer) {
    LERROR("[copy_to_buffer]: Buffer not found");
    return;
  }
  // TODO: don't assume it's shared on metal
  memcpy((uint8_t*)buffer->contents() + dst_offset, src, src_size);
  buffer->contents();
}

MetalDevice::GPUFrameAllocator::Alloc MetalDevice::alloc_arg_buf() {
  return arg_buf_allocator_->alloc(sizeof(uint64_t) * 12);
}
