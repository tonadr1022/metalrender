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

void MetalDevice::init(Window* window) {
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
  main_res_set_->requestResidency();
  cmd_lists_.reserve(10);
  main_cmd_q_ = device_->newMTL4CommandQueue();
  main_cmd_q_->addResidencySet(main_res_set_);
  main_cmd_buf_ = device_->newCommandBuffer();

  for (size_t i = 0; i < info_.frames_in_flight; i++) {
    frame_push_constant_bufs_[i] = create_buf_h(rhi::BufferDesc{.size = 1024ul * 1024});
  }
  init_bindless();
}

void MetalDevice::shutdown() {
  ar_pool_->release();
  device_->release();
}

rhi::BufferHandle MetalDevice::create_buf(const rhi::BufferDesc& desc) {
  auto options = mtl::util::convert_storage_mode(desc.storage_mode);
  auto* mtl_buf = device_->newBuffer(desc.size, options);
  mtl_buf->retain();

  uint32_t idx = rhi::k_invalid_bindless_idx;
  if (desc.alloc_gpu_slot) {
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
  texture_desc->setPixelFormat(mtl::util::convert_format(desc.format));
  texture_desc->setStorageMode(mtl::util::convert_storage_mode(desc.storage_mode));
  texture_desc->setMipmapLevelCount(desc.mip_levels);
  texture_desc->setArrayLength(desc.array_length);
  // TODO: parameterize this?
  texture_desc->setAllowGPUOptimizedContents(true);
  auto usage = mtl::util::convert_texture_usage(desc.usage);
  if (desc.flags & rhi::TextureDescFlags_PixelFormatView) {
    usage |= MTL::TextureUsagePixelFormatView;
  }
  texture_desc->setUsage(usage);
  auto* tex = device_->newTexture(texture_desc);
  tex->retain();
  texture_desc->release();
  uint32_t idx = rhi::k_invalid_bindless_idx;
  if (desc.alloc_gpu_slot) {
    idx = texture_index_allocator_.alloc_idx();
  }
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
  for (const auto& shader_info : cinfo.shaders) {
    // TODO: LMAO
    MTL::Library* lib =
        shader_info.type == ShaderType::Fragment
            ? create_or_get_lib(
                  "/Users/tony/personal/metalrender/resources/shader_out/test_frag.metallib")
            : create_or_get_lib(
                  "/Users/tony/personal/metalrender/resources/shader_out/test_vert.metallib");
    MTL4::LibraryFunctionDescriptor* func_desc = MTL4::LibraryFunctionDescriptor::alloc()->init();
    func_desc->setLibrary(lib);
    func_desc->setName(mtl::util::string(shader_info.entry_point));
    switch (shader_info.type) {
      case ShaderType::Fragment: {
        desc->setFragmentFunctionDescriptor(func_desc);
        break;
      }
      case ShaderType::Vertex: {
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
    desc->colorAttachments()->object(i)->setPixelFormat(mtl::util::convert_format(format));
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

rhi::PipelineHandleHolder MetalDevice::create_graphics_pipeline_h(
    const rhi::GraphicsPipelineCreateInfo& cinfo) {
  return rhi::PipelineHandleHolder{create_graphics_pipeline(cinfo), this};
}

MTL::Library* MetalDevice::create_or_get_lib(const std::filesystem::path& path) {
  NS::Error* err{};
  auto it = path_to_lib_.find(path.string());
  return it != path_to_lib_.end()
             ? it->second
             : device_->newLibrary(mtl::util::string(metal_shader_dir_ / path), &err);
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
  frame_ar_pool_ = NS::AutoreleasePool::alloc()->init();
  curr_cmd_list_idx_ = 0;
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
  {
    // buffer desc table
    buffer_descriptor_table_ =
        create_buf_h(rhi::BufferDesc{.size = sizeof(IRDescriptorTableEntry) * k_max_buffers});

    MTL::ArgumentDescriptor* arg0 = MTL::ArgumentDescriptor::alloc()->init();
    arg0->setIndex(0);
    arg0->setAccess(MTL::ArgumentAccessReadWrite);
    arg0->setArrayLength(k_max_buffers);
    arg0->setDataType(MTL::DataTypePointer);

    NS::Object* args_arr[] = {arg0};
    const NS::Array* args = NS::Array::array(args_arr, ARRAY_SIZE(args_arr));
    buffer_arg_enc_ = device_->newArgumentEncoder(args);
    buffer_arg_enc_->setArgumentBuffer(get_mtl_buf(buffer_descriptor_table_), 0);
  }
  {  // top level arg enc
    top_level_arg_buf_ = create_buf_h(rhi::BufferDesc{.size = sizeof(IRDescriptorTableEntry) * 4});

    MTL::ArgumentDescriptor* arg0 = MTL::ArgumentDescriptor::alloc()->init();
    arg0->setIndex(0);
    arg0->setAccess(MTL::ArgumentAccessReadOnly);
    arg0->setDataType(MTL::DataTypePointer);

    MTL::ArgumentDescriptor* arg1 = MTL::ArgumentDescriptor::alloc()->init();
    arg1->setIndex(1);
    arg1->setAccess(MTL::ArgumentAccessReadWrite);
    arg1->setDataType(MTL::DataTypePointer);

    NS::Object* args_arr[] = {arg0, arg1};
    const NS::Array* args = NS::Array::array(args_arr, ARRAY_SIZE(args_arr));
    top_level_arg_enc_ = device_->newArgumentEncoder(args);
    top_level_arg_enc_->setArgumentBuffer(get_mtl_buf(top_level_arg_buf_), 0);
    for (auto& i : args_arr) {
      i->release();
    }
    // {
    //   IRBufferView bview{};
    //   bview.buffer = get_mtl_buf(buffer_descriptor_table_);
    //   bview.bufferOffset = 0;
    //   bview.bufferSize = get_buf(top_level_arg_buf_)->size();
    //   bview.typedBuffer = false;
    //   auto metadata = IRDescriptorTableGetBufferMetadata(&bview);
    //   auto* pResourceTable =
    //   (IRDescriptorTableEntry*)(get_mtl_buf(top_level_arg_buf_))->contents();
    //   IRDescriptorTableSetBuffer(&pResourceTable[1],
    //                              get_mtl_buf(buffer_descriptor_table_)->gpuAddress(), metadata);
    // }
    top_level_arg_enc_->setBuffer(get_mtl_buf(buffer_descriptor_table_), 0, 1);
  }
  // TODO: this is sooooooooooooo cursed LMAOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
  main_res_set_->commit();
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

size_t MetalDevice::copy_to_frame_push_constant_buf(void* data, size_t size) {
  auto* buf = get_frame_push_constant_buf();
  memcpy((uint8_t*)buf->contents() + curr_frame_push_constant_buf_offset_bytes_, data, size);
  size_t offset = curr_frame_push_constant_buf_offset_bytes_;
  curr_frame_push_constant_buf_offset_bytes_ += size;
  return offset;
}
