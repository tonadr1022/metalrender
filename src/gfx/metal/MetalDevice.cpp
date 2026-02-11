#include "MetalDevice.hpp"

#include <QuartzCore/QuartzCore.h>

#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLResource.hpp>

#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Queue.hpp"

#define NS_PRIVATE_IMPLEMENTATION
#include <Foundation/NSData.hpp>
#include <Foundation/NSObject.hpp>
#include <Metal/MTLRenderPipeline.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>
#include <fstream>
#include <tracy/Tracy.hpp>

#include "Window.hpp"
#include "core/Util.hpp"
#include "imgui.h"
#include "platform/apple/AppleWindow.hpp"
#include "util/Timer.hpp"

#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime_wrapper.h>

#include "MetalUtil.hpp"
#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "gfx/metal/MetalCmdEncoder.hpp"
#include "gfx/metal/MetalPipeline.hpp"
#include "gfx/metal/MetalUtil.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Pipeline.hpp"

namespace TENG_NAMESPACE {

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

bool MetalDevice::init(const InitInfo& init_info, const MetalDeviceInitInfo& metal_init_info) {
  ZoneScoped;
  // TODO: actually check for mtl4 support
  mtl4_enabled_ = metal_init_info.prefer_mtl4;
  shader_lib_dir_ = init_info.shader_lib_dir;
  shader_lib_dir_ /= "metal";

  device_ = MTL::CreateSystemDefaultDevice();

  if (device_->hasUnifiedMemory()) {
    capabilities_ |= rhi::GraphicsCapability::CacheCoherentUMA;
  }

  info_.frames_in_flight = std::clamp<size_t>(init_info.frames_in_flight, k_min_frames_in_flight,
                                              k_max_frames_in_flight);
  info_.timestamp_frequency = device_->queryTimestampFrequency();

  {  // residency set
    auto desc = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());
    desc->setLabel(mtl::util::string("main residency set"));
    NS::Error* err{};
    main_res_set_ = device_->newResidencySet(desc.get(), &err);
    if (err) {
      LERROR("Failed to create residency set");
      return false;
    }
    main_res_set_->requestResidency();
  }

  if (mtl4_enabled_) {
    mtl4_resources_.emplace(MTL4_Resources{});
    NS::Error* err{};
    {
      MTL4::CompilerDescriptor* compiler_desc = MTL4::CompilerDescriptor::alloc()->init();
      m4res().shader_compiler = device_->newCompiler(compiler_desc, &err);
      compiler_desc->release();
    }
    auto init_queue = [this](Queue& queue) {
      queue.queue = NS::TransferPtr(device_->newMTL4CommandQueue());
      queue.queue->addResidencySet(main_res_set_);
    };
    init_queue(get_queue(rhi::QueueType::Graphics));

    m4res().cmd_encoders_.reserve(20);
  } else {
    mtl3_resources_.emplace(MTL3_Resources{});
    auto init_queue = [this](Queue& queue) {
      queue.mtl3_queue = NS::TransferPtr(device_->newCommandQueue());
      queue.mtl3_queue->addResidencySet(main_res_set_);
    };
    init_queue(get_queue(rhi::QueueType::Graphics));

    m3res().cmd_lists_.reserve(20);
  }

  init_bindless();

  push_constant_allocator_.emplace(this);
  test_allocator_.emplace(this);
  arg_buf_allocator_.emplace(this);

  psos_.dispatch_indirect_pso =
      compile_mtl_compute_pipeline(shader_lib_dir_ / "dispatch_indirect.metallib");
  psos_.dispatch_mesh_pso =
      compile_mtl_compute_pipeline(shader_lib_dir_ / "dispatch_mesh.metallib");

  {  // frame fences
    for (size_t queue_i = 0; queue_i < (size_t)rhi::QueueType::Count; queue_i++) {
      if (!queues_[queue_i].is_valid()) {
        continue;
      }
      for (size_t frame_i = 0; frame_i < frames_in_flight(); frame_i++) {
        frame_fences_[queue_i][frame_i] = NS::TransferPtr(device_->newSharedEvent());
      }
    }

    for (size_t frame_i = 0; frame_i < frames_in_flight(); frame_i++) {
      frame_fence_values_[frame_i] = 0;
    }
  }
  {
    // static samplers
    constexpr uint32_t k_static_sampler_count = 10;
    static_sampler_entries_.resize(k_static_sampler_count);
    auto* static_table = static_sampler_entries_.data();
    auto add_static_sampler = [&](uint32_t offset, const rhi::SamplerDesc& desc) {
      auto actual_desc = desc;
      actual_desc.flags = rhi::SamplerDescFlags::NoBindless;
      static_samplers_.emplace_back(create_sampler_h(actual_desc));
      auto* sampler = sampler_pool_.get(static_samplers_.back().handle)->sampler();
      IRDescriptorTableSetSampler(&static_table[offset], sampler, 0.0);
    };

    add_static_sampler(0, {
                              .min_filter = rhi::FilterMode::Linear,
                              .mag_filter = rhi::FilterMode::Linear,
                              .mipmap_mode = rhi::FilterMode::Linear,
                              .address_mode = rhi::AddressMode::ClampToEdge,
                          });
    add_static_sampler(1, {
                              .min_filter = rhi::FilterMode::Linear,
                              .mag_filter = rhi::FilterMode::Linear,
                              .mipmap_mode = rhi::FilterMode::Linear,
                              .address_mode = rhi::AddressMode::Repeat,
                          });
    add_static_sampler(2, {
                              .min_filter = rhi::FilterMode::Linear,
                              .mag_filter = rhi::FilterMode::Linear,
                              .mipmap_mode = rhi::FilterMode::Linear,
                              .address_mode = rhi::AddressMode::MirroredRepeat,
                          });
    add_static_sampler(3, {
                              .min_filter = rhi::FilterMode::Nearest,
                              .mag_filter = rhi::FilterMode::Nearest,
                              .mipmap_mode = rhi::FilterMode::Nearest,
                              .address_mode = rhi::AddressMode::ClampToEdge,
                          });
    add_static_sampler(4, {
                              .min_filter = rhi::FilterMode::Nearest,
                              .mag_filter = rhi::FilterMode::Nearest,
                              .mipmap_mode = rhi::FilterMode::Nearest,
                              .address_mode = rhi::AddressMode::Repeat,
                          });
    add_static_sampler(5, {
                              .min_filter = rhi::FilterMode::Nearest,
                              .mag_filter = rhi::FilterMode::Nearest,
                              .mipmap_mode = rhi::FilterMode::Nearest,
                              .address_mode = rhi::AddressMode::MirroredRepeat,
                          });
    add_static_sampler(6, {
                              .min_filter = rhi::FilterMode::Linear,
                              .mag_filter = rhi::FilterMode::Linear,
                              .mipmap_mode = rhi::FilterMode::Linear,
                              .address_mode = rhi::AddressMode::ClampToEdge,
                              .anisotropy_enable = true,
                              .max_anisotropy = 16.0f,
                          });
    add_static_sampler(7, {
                              .min_filter = rhi::FilterMode::Linear,
                              .mag_filter = rhi::FilterMode::Linear,
                              .mipmap_mode = rhi::FilterMode::Linear,
                              .address_mode = rhi::AddressMode::Repeat,
                              .anisotropy_enable = true,
                              .max_anisotropy = 16.0f,
                          });
    add_static_sampler(8, {
                              .min_filter = rhi::FilterMode::Linear,
                              .mag_filter = rhi::FilterMode::Linear,
                              .mipmap_mode = rhi::FilterMode::Linear,
                              .address_mode = rhi::AddressMode::MirroredRepeat,
                              .anisotropy_enable = true,
                              .max_anisotropy = 16.0f,
                          });
    add_static_sampler(9, {
                              .min_filter = rhi::FilterMode::Linear,
                              .mag_filter = rhi::FilterMode::Linear,
                              .mipmap_mode = rhi::FilterMode::Nearest,
                              .address_mode = rhi::AddressMode::ClampToEdge,
                              .compare_enable = true,
                              .compare_op = rhi::CompareOp::GreaterOrEqual,
                          });
  }
  return true;
}

void MetalDevice::init(const InitInfo& init_info) {
  init(init_info, MetalDeviceInitInfo{.prefer_mtl4 = true});
}

void MetalDevice::shutdown() {
  resource_descriptor_table_ = {};
  sampler_descriptor_table_ = {};
  arg_buf_allocator_.reset();
  push_constant_allocator_.reset();
  test_allocator_.reset();

  if (mtl4_enabled_) {
    m4res().shader_compiler->release();
    mtl4_resources_.reset();
  } else {
    m3res().cmd_lists_.clear();
  }

  delete_queues_.flush_deletions(SIZE_T_MAX);

  buffer_pool_.iterate_entries([](const MetalBuffer& entry) {
    LWARN("leaked buffer {}, SIZE {}", entry.desc().name ? entry.desc().name : "unnamed_buffer",
          entry.desc().size);
  });

  texture_pool_.iterate_entries([](const MetalTexture& entry) {
    if (!entry.is_drawable_tex()) {
      LWARN("leaked texture {}, SIZE {}x{}x{}, FORMAT {}",
            entry.desc().name ? entry.desc().name : "unnamed_texture", entry.desc().dims.x,
            entry.desc().dims.y, entry.desc().dims.z, (int)entry.desc().format);
    }
  });

  device_->release();
}

namespace {

MTL::ResourceOptions convert_resource_storage_mode(rhi::BufferDescFlags flags, bool coherent_uma) {
  MTL::ResourceOptions options{};
  if (rhi::has_flag(flags, rhi::BufferDescFlags::CPUAccessible) || coherent_uma) {
    options |= MTL::ResourceStorageModeShared;
  } else {
    options |= MTL::ResourceStorageModePrivate;
  }
  return options;
}

}  // namespace

rhi::BufferHandle MetalDevice::create_buf(const rhi::BufferDesc& desc) {
  auto resource_opts = convert_resource_storage_mode(
      desc.flags, rhi::has_flag(capabilities_, rhi::GraphicsCapability::CacheCoherentUMA) &&
                      !has_flag(desc.flags, rhi::BufferDescFlags::DisableCPUAccessOnUMA));
  resource_opts |= MTL::ResourceHazardTrackingModeUntracked;
  auto* mtl_buf = device_->newBuffer(desc.size, resource_opts);
  ALWAYS_ASSERT(mtl_buf);
  if (desc.name) {
    mtl_buf->setLabel(mtl::util::string(desc.name));
  }
  if (mtl4_enabled_) {
    mtl_buf->retain();
  }

  uint32_t idx = rhi::k_invalid_bindless_idx;
  if (!has_flag(desc.flags, rhi::BufferDescFlags::NoBindless)) {
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

  return buffer_pool_.alloc(desc, mtl_buf, resource_opts, idx);
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
  MTL::StorageMode storage_mode{};
  MTL::ResourceOptions resource_opts{};
  if (has_flag(desc.flags, rhi::TextureDescFlags::CPUAccessible) ||
      (has_flag(capabilities_, rhi::GraphicsCapability::CacheCoherentUMA) &&
       !has_flag(desc.flags, rhi::TextureDescFlags::DisableCPUAccessOnUMA))) {
    storage_mode = MTL::StorageModeShared;
    resource_opts |= MTL::ResourceStorageModeShared;
  } else {
    storage_mode = MTL::StorageModePrivate;
    resource_opts |= MTL::ResourceStorageModePrivate;
  }
  texture_desc->setStorageMode(storage_mode);
  texture_desc->setMipmapLevelCount(desc.mip_levels);
  texture_desc->setArrayLength(desc.array_length);
  texture_desc->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);
  texture_desc->setAllowGPUOptimizedContents(true);
  auto usage = mtl::util::convert(desc.usage);
  if (has_flag(desc.flags, rhi::TextureDescFlags::PixelFormatView)) {
    usage |= MTL::TextureUsagePixelFormatView;
  }
  texture_desc->setUsage(usage);
  texture_desc->setResourceOptions(resource_opts);
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
  if (!has_flag(desc.flags, rhi::TextureDescFlags::NoBindless)) {
    idx = resource_desc_heap_allocator_.alloc_idx();
    write_bindless_resource_descriptor(idx, tex);
  }
  main_res_set_->addAllocation(tex);
  main_res_set_->commit();

  return texture_pool_.alloc(desc, idx, tex);
}

void MetalDevice::destroy(rhi::BufferHandle handle) {
  delete_queues_.enqueue_deletion(handle, frame_num_);
}

void MetalDevice::destroy(rhi::TextureHandle handle) {
  auto* tex = texture_pool_.get(handle);
  ASSERT(tex);
  if (!tex) {
    return;
  }
  ASSERT(tex->texture());

  if (!has_flag(tex->desc().flags, rhi::TextureDescFlags::NoBindless)) {
    resource_desc_heap_allocator_.free_idx(tex->bindless_idx());
  }
  for (auto& view : tex->tex_views) {
    ASSERT(view.tex == nullptr);
  }

  if (tex->texture() && !tex->is_drawable_tex()) {
    main_res_set_->removeAllocation(tex->texture());
    main_res_set_->commit();
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

void MetalDevice::destroy(rhi::QueryPoolHandle) {}

void MetalDevice::destroy(rhi::SwapchainHandle) {}

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
  auto* pso = create_graphics_pipeline_internal(cinfo);
  if (!pso) {
    return {};
  }
  auto handle = pipeline_pool_.alloc(pso, cinfo);
  auto* pipeline = pipeline_pool_.get(handle);
  pipeline->render_target_info_hash = rhi::compute_render_target_info_hash(cinfo.rendering);
  return handle;
}

rhi::PipelineHandle MetalDevice::create_compute_pipeline(const rhi::ShaderCreateInfo& cinfo) {
  auto* pso = create_compute_pipeline_internal(cinfo);
  if (!pso) {
    return {};
  }
  return pipeline_pool_.alloc(pso, cinfo);
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
  if (!has_flag(desc.flags, rhi::SamplerDescFlags::NoBindless)) {
    bindless_idx = sampler_desc_heap_allocator_.alloc_idx();
    auto* resource_table =
        (IRDescriptorTableEntry*)(get_mtl_buf(sampler_descriptor_table_))->contents();
    IRDescriptorTableSetSampler(&resource_table[bindless_idx], sampler, 0.0);
  }
  return sampler_pool_.alloc(desc, sampler, bindless_idx);
}

MTL::Library* MetalDevice::create_or_get_lib(const std::filesystem::path& path, bool) {
  NS::Error* err{};
  auto it = path_to_lib_.find(path.string());
  if (it != path_to_lib_.end()) {
    return it->second;
  }
  constexpr bool allow_cache = false;
  if (!allow_cache) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (file.read(buffer.data(), size)) {
      dispatch_data_t data = dispatch_data_create(buffer.data(), buffer.size(), nullptr,
                                                  DISPATCH_DATA_DESTRUCTOR_DEFAULT);

      auto* lib = device_->newLibrary(data, &err);
      dispatch_release(data);
      if (err) {
        mtl::util::print_err(err);
        return nullptr;
      }
      return lib;
    }
    LINFO("Failed to read file {}", path.string());
    return nullptr;
  }

  auto* lib = device_->newLibrary(mtl::util::string(path), &err);
  if (err) {
    mtl::util::print_err(err);
    return nullptr;
  }
  return lib;
}

rhi::CmdEncoder* MetalDevice::begin_cmd_encoder() {
  if (mtl4_enabled_) {
    if (curr_cmd_list_idx_ == m4res().cmd_encoders_.size()) {
      m4res().cmd_encoders_.emplace_back(std::make_unique<Metal4CmdEncoder>());
      m4res().cmd_list_resources_.emplace_back();
    }
    ASSERT(curr_cmd_list_idx_ < m4res().cmd_encoders_.size());
    auto* cmd_list = m4res().cmd_encoders_[curr_cmd_list_idx_].get();
    ASSERT(curr_cmd_list_idx_ < m4res().cmd_list_resources_.size());
    auto& res = m4res().cmd_list_resources_[curr_cmd_list_idx_];
    if (!res.cmd_allocators[frame_idx()]) {
      res.cmd_allocators[frame_idx()] = NS::TransferPtr(device_->newCommandAllocator());
    }
    if (!res.cmd_buf) {
      res.cmd_buf = NS::TransferPtr(device_->newCommandBuffer());
    }
    res.cmd_buf->beginCommandBuffer(res.cmd_allocators[frame_idx()].get());
    curr_cmd_list_idx_++;
    cmd_list->reset(this);
    cmd_list->m4_state().cmd_buf = res.cmd_buf.get();
    cmd_list->done_ = false;
    cmd_list->presents_.clear();
    cmd_list->queue_ = rhi::QueueType::Graphics;
    return m4res().cmd_encoders_[curr_cmd_list_idx_ - 1].get();
  }

  // MTL3 path
  if (curr_cmd_list_idx_ >= m3res().cmd_lists_.size()) {
    m3res().cmd_lists_.emplace_back(std::make_unique<Metal3CmdEncoder>());
  }
  ASSERT(curr_cmd_list_idx_ < m3res().cmd_lists_.size());
  auto* ret = (Metal3CmdEncoder*)m3res().cmd_lists_[curr_cmd_list_idx_].get();
  ret->reset(this);
  ret->m3_state().cmd_buf = queues_[(int)rhi::QueueType::Graphics].mtl3_queue->commandBuffer();
  ret->done_ = false;
  ret->presents_.clear();
  ret->queue_ = rhi::QueueType::Graphics;
  // ret->m3_state().cmd_buf = m3res().main_cmd_buf;
  curr_cmd_list_idx_++;

  return ret;
}

void MetalDevice::end_command_list(rhi::CmdEncoder* cmd_enc) {
  if (mtl4_enabled_) {
    auto* cmd = (Metal4CmdEncoder*)cmd_enc;
    get_queue(rhi::QueueType::Graphics).submit_cmd_bufs.push_back(cmd->m4_state().cmd_buf);
  } else {
    auto* cmd = (Metal3CmdEncoder*)cmd_enc;
    // cmd->m3_state().cmd_buf->commit();
    get_queue(rhi::QueueType::Graphics).mtl3_submit_cmd_bufs.push_back(cmd->m3_state().cmd_buf);
  }
}

void MetalDevice::submit_frame() {
  ZoneScoped;
  if (mtl4_enabled_) {
    // wait for presents to be ready
    for (size_t cmd_list_i = 0; cmd_list_i < curr_cmd_list_idx_; cmd_list_i++) {
      auto* list = m4res().cmd_encoders_[cmd_list_i].get();
      for (auto& present : list->presents_) {
        queues_[(int)list->queue_].queue->wait(present.get());
      }
    }

    for (auto& queue : queues_) {
      if (queue.is_valid()) {
        queue.submit();
      }
    }

    // signal completion of queues post-commit
    frame_fence_values_[frame_idx()]++;
    for (size_t queue_type = 0; queue_type < (size_t)rhi::QueueType::Count; queue_type++) {
      auto& queue = queues_[queue_type];
      if (!queue.is_valid()) {
        continue;
      }
      queue.submit();
      queue.queue->signalEvent(frame_fences_[queue_type][frame_idx()].get(),
                               frame_fence_values_[frame_idx()]);
    }

    // presents
    for (size_t cmd_list_i = 0; cmd_list_i < curr_cmd_list_idx_; cmd_list_i++) {
      auto* list = m4res().cmd_encoders_[cmd_list_i].get();
      for (auto& present : list->presents_) {
        auto& queue = get_queue(list->queue_);
        queue.queue->signalDrawable(present.get());
        present->present();
      }
    }
  } else {
    for (size_t cmd_list_i = 0; cmd_list_i < curr_cmd_list_idx_; cmd_list_i++) {
      auto* list = m3res().cmd_lists_[cmd_list_i].get();
      for (auto& present : list->presents_) {
        list->m3_state().cmd_buf->presentDrawable(present.get());
      }
    }

    for (auto& queue : queues_) {
      if (queue.is_valid()) {
        queue.m3_submit();
      }
    }

    // dummy encoders that signal frame fences
    // for (size_t queue_type = 0; queue_type < (size_t)rhi::QueueType::Count; queue_type++) {
    //   auto& queue = queues_[queue_type];
    //   if (!queue.is_valid()) {
    //     continue;
    //   }
    //   auto* cmd_buf = queue.mtl3_queue->commandBuffer();
    //   cmd_buf->encodeSignalEvent(frame_fences_[queue_type][frame_idx()].get(),
    //                              frame_fence_values_[frame_idx()]);
    //   cmd_buf->commit();
    // }

    // presents
    for (size_t cmd_list_i = 0; cmd_list_i < curr_cmd_list_idx_; cmd_list_i++) {
      auto* list = m3res().cmd_lists_[cmd_list_i].get();
      for (auto& present : list->presents_) {
        present->present();
      }
    }
  }

  frame_num_++;

  if (mtl4_enabled_) {
    // wait for N-frames--in-flight-ago frame to complete
    for (size_t queue_type = 0; queue_type < (size_t)rhi::QueueType::Count; queue_type++) {
      if (queues_[queue_type].is_valid()) {
        auto& fence = frame_fences_[queue_type][frame_idx()];
        if (!fence->waitUntilSignaledValue(frame_fence_values_[frame_idx()], ~0ULL)) {
          LERROR("No signaled value from shared event for frame: {}, queue: {}", frame_idx(),
                 to_string((rhi::QueueType)queue_type));
        }
      }
    }
  }

  frame_ar_pool_->release();
  frame_ar_pool_ = NS::AutoreleasePool::alloc()->init();
  curr_cmd_list_idx_ = 0;

  delete_queues_.flush_deletions(frame_num_);

  icb_mgr_draw_indexed_.reset_for_frame();
  icb_mgr_draw_mesh_threadgroups_.reset_for_frame();

  push_constant_allocator_->advance_frame();
  test_allocator_->advance_frame();
  arg_buf_allocator_->advance_frame();
}

void MetalDevice::init_bindless() {
  auto create_descriptor_table = [this](rhi::BufferHandleHolder* out_buf, size_t count,
                                        const char* name) {
    *out_buf = create_buf_h(rhi::BufferDesc{.size = sizeof(IRDescriptorTableEntry) * count,
                                            .flags = rhi::BufferDescFlags::NoBindless,
                                            .name = name});
  };

  create_descriptor_table(&resource_descriptor_table_, k_max_buffers + k_max_textures,
                          "bindless_resource_descriptor_table");
  create_descriptor_table(&sampler_descriptor_table_, k_max_samplers,
                          "bindless_sampler_descriptor_table");
}

MTL::ComputePipelineState* MetalDevice::compile_mtl_compute_pipeline(
    const std::filesystem::path& path, const char* entry_point, bool) {
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
    auto* pso = m4res().shader_compiler->newComputePipelineState(desc, nullptr, &err);
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

MetalDevice::ICB_Mgr::ICB_Alloc MetalDevice::ICB_Mgr::alloc(rhi::BufferHandle indirect_buf_handle,
                                                            uint32_t draw_cnt) {
  auto it = indirect_buffer_handle_to_icb_.find(indirect_buf_handle.to64());
  if (it == indirect_buffer_handle_to_icb_.end()) {
    it = indirect_buffer_handle_to_icb_.emplace(indirect_buf_handle.to64(), ICB_Data{}).first;
  }
  uint32_t indirect_buf_id = it->second.curr_id;
  it->second.curr_id++;

  std::array<MTL::IndirectCommandBuffer*, k_max_frames_in_flight> icbs{};
  if (indirect_buf_id < it->second.icbs.size()) {
    icbs = it->second.icbs[indirect_buf_id];
  } else {
    for (size_t i = 0; i < k_max_frames_in_flight; i++) {
      MTL::IndirectCommandBufferDescriptor* desc =
          MTL::IndirectCommandBufferDescriptor::alloc()->init();
      // TODO: try not to do this at least for mesh shaders
      desc->setInheritBuffers(false);
      desc->setInheritPipelineState(true);
      desc->setCommandTypes(cmd_types_);
      if (cmd_types_ & MTL::IndirectCommandTypeDrawIndexed) {
        desc->setMaxVertexBufferBindCount(3);
        desc->setMaxFragmentBufferBindCount(3);
      } else if (cmd_types_ & MTL::IndirectCommandTypeDrawMeshThreadgroups) {
        desc->setMaxObjectBufferBindCount(3);
        desc->setMaxMeshBufferBindCount(3);
        desc->setMaxFragmentBufferBindCount(3);
      }
      auto* icb = device_->get_device()->newIndirectCommandBuffer(desc, draw_cnt,
                                                                  MTL::ResourceStorageModeShared);
      if (device_->mtl4_enabled_) {
        icb->retain();
      }
      device_->get_main_residency_set()->addAllocation(icb);
      device_->get_main_residency_set()->commit();

      icbs[i] = icb;
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

void MetalDevice::on_imgui() {
  ImGui::Text(
      "Active Textures: %zu\nActive Buffers: %zu\nActive Samplers: %zu\nGPU Buffer Space "
      "Requested: %zu (mb)",
      texture_pool_.size(), buffer_pool_.size(), sampler_pool_.size(),
      req_alloc_sizes_.total_buffer_space_allocated / 1024 / 1024);
  ImGui::Text("API Version: %s", mtl4_enabled_ ? "Metal 4" : "Metal 3");
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

rhi::TextureViewHandle MetalDevice::create_tex_view(rhi::TextureHandle handle,
                                                    uint32_t base_mip_level, uint32_t level_count,
                                                    uint32_t base_array_layer,
                                                    uint32_t layer_count) {
  auto* tex = reinterpret_cast<MetalTexture*>(get_tex(handle));
  ALWAYS_ASSERT(tex);
  auto* mtl_tex = tex->texture();
  auto* view = mtl_tex->newTextureView(mtl::util::convert(tex->desc().format),
                                       get_texture_type(tex->desc().dims, tex->desc().array_length),
                                       NS::Range::Make(base_mip_level, level_count),
                                       NS::Range::Make(base_array_layer, layer_count));
  auto bindless_idx = resource_desc_heap_allocator_.alloc_idx();
  auto subresource_id = static_cast<rhi::TextureViewHandle>(tex->tex_views.size());
  tex->tex_views.emplace_back(view, bindless_idx);
  auto* resource_table =
      (IRDescriptorTableEntry*)(get_mtl_buf(resource_descriptor_table_))->contents();
  IRDescriptorTableSetTexture(&resource_table[bindless_idx], view, 0.0f, 0);
  return subresource_id;
}

void MetalDevice::destroy(rhi::TextureHandle handle, int subresource_id) {
  auto* tex = reinterpret_cast<MetalTexture*>(get_tex(handle));
  ASSERT((subresource_id >= 0 && subresource_id < (int)tex->tex_views.size()));
  auto& tv = tex->tex_views[subresource_id];
  tv.tex->release();
  tex->tex_views[subresource_id] = {};
}

uint32_t MetalDevice::get_tex_view_bindless_idx(rhi::TextureHandle handle, int subresource_id) {
  auto* tex = reinterpret_cast<MetalTexture*>(get_tex(handle));
  ALWAYS_ASSERT(tex);
  ALWAYS_ASSERT((subresource_id >= 0 && (size_t)subresource_id < tex->tex_views.size()));
  return tex->tex_views[subresource_id].bindless_idx;
}

void MetalDevice::write_bindless_resource_descriptor(uint32_t bindless_idx, MTL::Texture* tex) {
  auto* resource_table =
      (IRDescriptorTableEntry*)(get_mtl_buf(resource_descriptor_table_))->contents();
  IRDescriptorTableSetTexture(&resource_table[bindless_idx], tex, 0.0f, 0);
}

void MetalDevice::get_all_buffers(std::vector<rhi::Buffer*>& out_buffers) {
  buffer_pool_.iterate_entries(
      [&out_buffers](const MetalBuffer& entry) { out_buffers.emplace_back((rhi::Buffer*)&entry); });
}

bool MetalDevice::replace_pipeline(rhi::PipelineHandle handle,
                                   const rhi::GraphicsPipelineCreateInfo& cinfo) {
  auto* pipeline = pipeline_pool_.get(handle);
  if (!pipeline) {
    return false;
  }
  auto* new_render_pso = create_graphics_pipeline_internal(cinfo);
  if (!new_render_pso) {
    return false;
  }
  if (pipeline->render_pso) {
    pipeline->render_pso->release();
  }
  pipeline->render_pso = new_render_pso;
  return true;
}

bool MetalDevice::replace_compute_pipeline(rhi::PipelineHandle handle,
                                           const rhi::ShaderCreateInfo& cinfo) {
  auto* pipeline = pipeline_pool_.get(handle);
  if (!pipeline) {
    return false;
  }
  auto* new_compute_pso = create_compute_pipeline_internal(cinfo);
  if (!new_compute_pso) {
    return false;
  }
  if (pipeline->compute_pso) {
    pipeline->compute_pso->release();
  }
  pipeline->compute_pso = new_compute_pso;
  return true;
}

MTL::RenderPipelineState* MetalDevice::create_graphics_pipeline_internal(
    const rhi::GraphicsPipelineCreateInfo& cinfo) {
  using ShaderType = rhi::ShaderType;

  bool is_vertex_pipeline{};
  for (const auto& shader_info : cinfo.shaders) {
    if (shader_info.type == ShaderType::Vertex) {
      is_vertex_pipeline = true;
      break;
    }
  }

  auto set_color_blend_atts = [&cinfo](auto desc) {
    size_t color_att_count = 0;
    for (const auto& format : cinfo.rendering.color_formats) {
      desc->colorAttachments()
          ->object(color_att_count++)
          ->setPixelFormat(mtl::util::convert(format));
    }

    for (size_t i = 0; i < color_att_count; i++) {
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
                          auto desc, NS::Error** err) -> MTL::RenderPipelineState* {
      set_shader_stage_functions_for_desc(desc);
      set_color_blend_atts(desc);
      for (size_t i = 0; i < cinfo.blend.attachments.size(); i++) {
        desc->colorAttachments()->object(i)->setBlendingState(
            cinfo.blend.attachments[i].enable ? MTL4::BlendStateEnabled : MTL4::BlendStateDisabled);
      }
      desc->setSupportIndirectCommandBuffers(MTL4::IndirectCommandBufferSupportStateEnabled);
      return m4res().shader_compiler->newRenderPipelineState(desc, nullptr, err);
    };

    if (!is_vertex_pipeline) {
      MTL4::MeshRenderPipelineDescriptor* desc =
          MTL4::MeshRenderPipelineDescriptor::alloc()->init();
      result = create_pso(desc, &err);
    } else {
      MTL4::RenderPipelineDescriptor* desc = MTL4::RenderPipelineDescriptor::alloc()->init();
      result = create_pso(desc, &err);
    }

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
      if (cinfo.rendering.depth_format != rhi::TextureFormat::Undefined) {
        desc->setDepthAttachmentPixelFormat(mtl::util::convert(cinfo.rendering.depth_format));
      }
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
      result = create_pso(desc, &err);
    } else {
      MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
      result = create_pso(desc, &err);
    }
  }

  if (!result) {
    LERROR("Failed to create MTL3 render pipeline {}", mtl::util::get_err_string(err));
  }

  return result;
}

MTL::ComputePipelineState* MetalDevice::create_compute_pipeline_internal(
    const rhi::ShaderCreateInfo& cinfo) {
  auto path = (shader_lib_dir_ / cinfo.path).concat(".comp.metallib");
  if (!std::filesystem::exists(path)) {
    LERROR("Compute metallib not found at path: {}", path.string());
    return nullptr;
  }
  return compile_mtl_compute_pipeline(path, "main");
}

void MetalDevice::DeleteQueues::flush_deletions(size_t curr_frame_num) {
  while (!to_delete_buffers.empty() &&
         to_delete_buffers.front().valid_to_delete_frame_num <= curr_frame_num) {
    device_->destroy_actual(to_delete_buffers.front().handle);
    to_delete_buffers.pop();
  }
}

void MetalDevice::DeleteQueues::enqueue_deletion(rhi::BufferHandle handle, size_t curr_frame_num) {
  to_delete_buffers.push(
      Entry<rhi::BufferHandle>{handle, curr_frame_num + device_->frames_in_flight()});
}

void MetalDevice::destroy_actual(rhi::BufferHandle handle) {
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

  if (!has_flag(buf->desc().flags, rhi::BufferDescFlags::NoBindless)) {
    ASSERT(buf->bindless_idx() != rhi::k_invalid_bindless_idx);
    resource_desc_heap_allocator_.free_idx(buf->bindless_idx());
  }

  if (has_flag(buf->desc().usage, rhi::BufferUsage::Indirect)) {
    // TODO: improve this cringe
    icb_mgr_draw_indexed_.remove(handle);
    icb_mgr_draw_mesh_threadgroups_.remove(handle);
  }

  buffer_pool_.destroy(handle);
}

// Ensures cmd_enc2 executes after cmd_enc1.
// No-op when both encoders are recorded into the same command buffer.
void MetalDevice::cmd_encoder_wait_for(rhi::CmdEncoder*, rhi::CmdEncoder*) {
  if (mtl4_enabled_) {
    // } else {
    // auto* mtl_cmd_enc1 = static_cast<Metal3CmdEncoder*>(cmd_enc1);
    // auto* mtl_cmd_enc2 = static_cast<Metal3CmdEncoder*>(cmd_enc2);
  }
}
MetalTexture::TexView* MetalDevice::get_tex_view(rhi::TextureHandle handle, int subresource_id) {
  auto* tex = reinterpret_cast<MetalTexture*>(get_tex(handle));
  ASSERT(tex);
  ASSERT((subresource_id >= 0 && (size_t)subresource_id < tex->tex_views.size()));
  return &tex->tex_views[subresource_id];
}

rhi::QueryPoolHandle MetalDevice::create_query_pool(const rhi::QueryPoolDesc& desc) {
  auto* heap_desc = MTL4::CounterHeapDescriptor::alloc()->init();
  heap_desc->setCount(desc.count);
  heap_desc->setType(MTL4::CounterHeapTypeTimestamp);
  NS::Error* err{};
  auto pool = NS::TransferPtr(device_->newCounterHeap(heap_desc, &err));
  if (!desc.name.empty()) {
    pool->setLabel(mtl::util::string(desc.name));
  }
  rhi::QueryPoolHandle handle{};
  if (!err) {
    handle = querypool_pool_.alloc(pool);
  } else {
    mtl::util::print_err(err);
  }
  heap_desc->release();
  return handle;
}

rhi::QueryPool* MetalDevice::get_query_pool(const rhi::QueryPoolHandle& handle) {
  return querypool_pool_.get(handle);
}

void MetalDevice::resolve_query_data(rhi::QueryPoolHandle query_pool, uint32_t start_query,
                                     uint32_t query_count, std::span<uint64_t> out_timestamps) {
  if (!mtl4_enabled_) {
    return;
  }
  auto* pool = (MetalQueryPool*)get_query_pool(query_pool);
  ASSERT(pool);
  NS::Data* resolved_query_data =
      pool->heap_->resolveCounterRange(NS::Range::Make(start_query, query_count));
  ASSERT(resolved_query_data);
  // objc runtime directly to call -bytes(), since NS::Data binding only has mutableBytes().
  const void* data = [(__bridge NSData*)resolved_query_data bytes];
  ASSERT(data);
  ASSERT(out_timestamps.size_bytes() >= resolved_query_data->length());
  memcpy(out_timestamps.data(), data, resolved_query_data->length());
  pool->heap_->invalidateCounterRange(NS::Range::Make(start_query, query_count));
}

void MetalDevice::acquire_next_swapchain_image(rhi::Swapchain* swapchain) {
  auto* swap = (MetalSwapchain*)swapchain;
  CA::MetalDrawable* drawable{};
  while (!drawable) {
    ZoneScopedN("Waiting for drawable");
    drawable = swap->metal_layer_->nextDrawable();
  }

  swap->drawable_ = NS::TransferPtr(drawable->retain());

  rhi::TextureDesc swap_img_desc{};
  auto* tex = drawable->texture();
  swap_img_desc.dims = {tex->width(), tex->height(), 1};
  swap_img_desc.format = mtl::util::convert(tex->pixelFormat());
  swap_img_desc.mip_levels = 1;
  swap_img_desc.array_length = 1;
  swap_img_desc.flags |= rhi::TextureDescFlags::NoBindless;
  // auto tex_handle_h = rhi::TextureHandleHolder{
  //     texture_pool_.alloc(swap_img_desc, rhi::k_invalid_bindless_idx, tex, true), this};
  // TODO: fix
  for (auto& t : swap->textures_) {
    t = rhi::TextureHandleHolder{
        texture_pool_.alloc(swap_img_desc, rhi::k_invalid_bindless_idx, tex, true), this};
  }
}

void MetalDevice::begin_swapchain_rendering(rhi::Swapchain* swapchain, rhi::CmdEncoder* cmd_enc,
                                            glm::vec4* clear_color) {
  ZoneScoped;
  auto* swap = (MetalSwapchain*)swapchain;
  if (mtl4_enabled_) {
    auto* enc = (Metal4CmdEncoder*)cmd_enc;
    enc->presents_.emplace_back(swap->drawable_);

    // auto tex_handle = texture_pool_.alloc(swap_img_desc, rhi::k_invalid_bindless_idx, tex, true);
    if (clear_color) {
      enc->begin_rendering({rhi::RenderAttInfo::color_att(
          swap->textures_[0].handle, rhi::LoadOp::Clear, rhi::ClearValue{.color = *clear_color})});
    } else {
      enc->begin_rendering(
          {rhi::RenderAttInfo::color_att(swap->textures_[0].handle, rhi::LoadOp::DontCare)});
    }
  } else {
    auto* enc = (Metal3CmdEncoder*)cmd_enc;
    enc->presents_.emplace_back(swap->drawable_);

    // TODO: remove dup
    // auto tex_handle = texture_pool_.alloc(swap_img_desc, rhi::k_invalid_bindless_idx, tex, true);
    if (clear_color) {
      enc->begin_rendering({rhi::RenderAttInfo::color_att(
          swap->textures_[0].handle, rhi::LoadOp::Clear, rhi::ClearValue{.color = *clear_color})});
    } else {
      enc->begin_rendering(
          {rhi::RenderAttInfo::color_att(swap->textures_[0].handle, rhi::LoadOp::DontCare)});
    }
  }
}

rhi::SwapchainHandle MetalDevice::create_swapchain(const rhi::SwapchainDesc& desc) {
  MetalSwapchain swapchain;
  if (!recreate_swapchain(desc, &swapchain)) {
    return {};
  }
  return swapchain_pool_.alloc(std::move(swapchain));
}

bool MetalDevice::recreate_swapchain(const rhi::SwapchainDesc& desc, rhi::Swapchain* swapchain) {
  auto& swap = *(MetalSwapchain*)swapchain;
  if (!swap.metal_layer_) {
    swap.metal_layer_ = CA::MetalLayer::layer();

    {  // transparency
      auto* objcLayer = (__bridge CAMetalLayer*)swap.metal_layer_;
      objcLayer.opaque = NO;
      objcLayer.opacity = 1.0;
    }

    swap.metal_layer_->setDevice(device_);
    swap.metal_layer_->setDisplaySyncEnabled(desc.vsync);
    swap.metal_layer_->setMaximumDrawableCount(3);
    swap.metal_layer_->setDrawableSize(CGSizeMake(desc.width, desc.height));
    set_layer_for_window(desc.window->get_handle(), swap.metal_layer_);
  } else {
    if (swap.desc_.vsync != desc.vsync) {
      swap.metal_layer_->setDisplaySyncEnabled(desc.vsync);
    }
    swap.metal_layer_->setDrawableSize(CGSizeMake(desc.width, desc.height));
  }
  swap.desc_ = desc;
  return true;
}

}  // namespace TENG_NAMESPACE
