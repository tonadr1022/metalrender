#include "RendererMetal.hpp"

#include <Metal/Metal.hpp>
#include <glm/mat4x4.hpp>

#include "ModelLoader.hpp"
#include "QuartzCore/CAMetalDrawable.hpp"
#include "QuartzCore/CAMetalLayer.hpp"
#include "WindowApple.hpp"
#include "core/BitUtil.hpp"
#include "core/Logger.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "metal/MetalDevice.hpp"
#include "metal/MetalUtil.hpp"
#include "shader_global_uniforms.h"

namespace {

enum class RenderMode : uint32_t { Default, Normals, NormalMap };

}  // namespace

void RendererMetal::init(const CreateInfo &cinfo) {
  device_ = cinfo.device;
  window_ = cinfo.window;
  shader_dir_ = cinfo.resource_dir / "shaders";
  resource_dir_ = cinfo.resource_dir;
  assert(!shader_dir_.empty());
  raw_device_ = device_->get_device();
  main_cmd_queue_ = raw_device_->newCommandQueue();

  {
    // TODO: handle resizing/more instances
    const uint32_t instance_capacity = instance_idx_allocator_.get_capacity();
    instance_material_id_buf_ = NS::TransferPtr(raw_device_->newBuffer(
        sizeof(uint32_t) * instance_capacity, MTL::ResourceStorageModeShared));
    instance_model_matrix_buf_ = NS::TransferPtr(raw_device_->newBuffer(
        sizeof(glm::mat4) * instance_capacity, MTL::ResourceStorageModeShared));
  }

  // TODO: rethink
  all_textures_.resize(k_max_textures);

  {
    MTL::TextureDescriptor *texture_descriptor = MTL::TextureDescriptor::alloc()->init();
    texture_descriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
    auto dims = window_->get_window_size();
    texture_descriptor->setWidth(dims.x);
    texture_descriptor->setHeight(dims.y);
    texture_descriptor->setDepth(1);
    texture_descriptor->setStorageMode(MTL::StorageModePrivate);
    texture_descriptor->setMipmapLevelCount(1);
    texture_descriptor->setSampleCount(1);
    texture_descriptor->setUsage(MTL::TextureUsageRenderTarget);
    depth_tex_ = device_->get_device()->newTexture(texture_descriptor);
    texture_descriptor->release();
  }

  {
    // main pipeline
    load_shaders();
    MTL::RenderPipelineDescriptor *pipeline_desc = MTL::RenderPipelineDescriptor::alloc()->init();
    pipeline_desc->setVertexFunction(forward_pass_shader_.vert_func);
    pipeline_desc->setFragmentFunction(forward_pass_shader_.frag_func);
    pipeline_desc->setLabel(util::mtl::string("basic"));
    pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    NS::Error *err{};
    main_pso_ = raw_device_->newRenderPipelineState(pipeline_desc, &err);
    if (err) {
      util::mtl::print_err(err);
      exit(1);
    }

    pipeline_desc->release();
  }
  // {
  //   // mesh shader pipeline
  //   MTL::MeshRenderPipelineDescriptor *pipeline_desc =
  //       MTL::MeshRenderPipelineDescriptor::alloc()->init();
  //   pipeline_desc->setMeshFunction(forward_mesh_shader_.mesh_func);
  //   pipeline_desc->setObjectFunction(forward_mesh_shader_.object_func);
  //   pipeline_desc->setFragmentFunction(forward_mesh_shader_.frag_func);
  //   pipeline_desc->setLabel(util::mtl::string("basic mesh"));
  //   pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  //   pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
  //
  //   NS::Error *err{};
  //   mesh_pso_ =
  //       raw_device_->newRenderPipelineState(pipeline_desc, MTL::PipelineOptionNone, nullptr,
  //       &err);
  //   if (err) {
  //     util::mtl::print_err(err);
  //     exit(1);
  //   }
  //
  //   pipeline_desc->release();
  // }

  MTL::ArgumentEncoder *frag_enc = forward_pass_shader_.frag_func->newArgumentEncoder(0);
  scene_arg_buffer_ = NS::TransferPtr(raw_device_->newBuffer(frag_enc->encodedLength(), 0));
  {
    MTL::ArgumentDescriptor *arg0 = MTL::ArgumentDescriptor::alloc()->init();
    arg0->setIndex(0);
    arg0->setAccess(MTL::ArgumentAccessReadOnly);
    arg0->setArrayLength(k_max_textures);
    arg0->setDataType(MTL::DataTypeTexture);
    arg0->setTextureType(MTL::TextureType2D);

    // MTL::ArgumentDescriptor *arg1 = MTL::ArgumentDescriptor::alloc()->init();
    // arg1->setIndex(k_max_textures);
    // arg1->setAccess(MTL::ArgumentAccessReadOnly);
    // arg1->setDataType(MTL::DataTypePointer);
    std::array<NS::Object *, 1> args_arr{arg0};
    const NS::Array *args = NS::Array::array(args_arr.data(), args_arr.size());
    global_arg_enc_ = raw_device_->newArgumentEncoder(args);
    global_arg_enc_->setArgumentBuffer(scene_arg_buffer_.get(), 0);
    for (auto &i : args_arr) {
      i->release();
    }
  }

  frag_enc->setArgumentBuffer(scene_arg_buffer_.get(), 0);

  materials_buffer_ = NS::TransferPtr(raw_device_->newBuffer(k_max_materials, 0));

  // frag_enc->setBuffer(materials_buffer_.get(), 0, k_max_textures);
  frag_enc->release();

  {
    // per frame uniform buffer
    main_uniform_buffer_ = NS::TransferPtr(raw_device_->newBuffer(
        frames_in_flight_ * util::align_256(sizeof(Uniforms)), MTL::ResourceStorageModeShared));
  }
}

void RendererMetal::shutdown() {}

void RendererMetal::render(const RenderArgs &render_args) {
  auto *frame_ar_pool = NS::AutoreleasePool::alloc()->init();

  flush_pending_texture_uploads();

  const CA::MetalDrawable *drawable = window_->metal_layer_->nextDrawable();
  if (!drawable) {
    frame_ar_pool->release();
    return;
  }

  MTL::RenderPassDescriptor *render_pass_desc = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto *color0 = render_pass_desc->colorAttachments()->object(0);
  MTL::RenderPassDepthAttachmentDescriptor *desc =
      MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
  desc->setTexture(depth_tex_);
  desc->setClearDepth(1.0);
  desc->setLoadAction(MTL::LoadActionClear);
  desc->setStoreAction(MTL::StoreActionStore);
  render_pass_desc->setDepthAttachment(desc);
  color0->setTexture(drawable->texture());
  color0->setLoadAction(MTL::LoadActionClear);
  color0->setClearColor(MTL::ClearColor::Make(0.5, 0.1, 0.12, 1.0));
  color0->setStoreAction(MTL::StoreActionStore);

  MTL::CommandBuffer *buf = main_cmd_queue_->commandBuffer();
  MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(render_pass_desc);

  {
    MTL::DepthStencilDescriptor *depth_stencil_desc = MTL::DepthStencilDescriptor::alloc()->init();
    depth_stencil_desc->setDepthCompareFunction(MTL::CompareFunctionLess);
    depth_stencil_desc->setDepthWriteEnabled(true);
    enc->setDepthStencilState(raw_device_->newDepthStencilState(depth_stencil_desc));
  }
  // TODO: this is awful
  for (const MTL::Texture *tex : all_textures_) {
    if (tex) {
      enc->useResource(tex, MTL::ResourceUsageSample);
    }
  }
  // TODO: class for this
  const size_t uniforms_offset =
      (curr_frame_ % frames_in_flight_) * util::align_256(sizeof(Uniforms));
  auto *uniform_data = reinterpret_cast<Uniforms *>(
      reinterpret_cast<uint8_t *>(main_uniform_buffer_->contents()) + uniforms_offset);
  auto window_dims = window_->get_window_size();
  const float aspect = (window_dims.x != 0) ? float(window_dims.x) / float(window_dims.y) : 1.0f;
  uniform_data->vp =
      glm::perspective(glm::radians(70.f), aspect, 0.1f, 10000.f) * render_args.view_mat;
  uniform_data->render_mode = (uint32_t)RenderMode::Default;

  if (render_mesh_shader_) {
    enc->setVertexBuffer(main_vert_buffer_.get(), 0, 0);
    enc->setMeshBuffer(main_uniform_buffer_.get(), uniforms_offset, 1);
  } else {
    enc->setRenderPipelineState(main_pso_);
    enc->setVertexBuffer(main_vert_buffer_.get(), 0, 0);
    enc->setVertexBuffer(main_uniform_buffer_.get(), uniforms_offset, 1);
    enc->setVertexBuffer(instance_model_matrix_buf_.get(), 0, 2);
    enc->setVertexBuffer(instance_material_id_buf_.get(), 0, 3);
    enc->setFragmentBuffer(scene_arg_buffer_.get(), 0, 0);
    enc->setFragmentBuffer(materials_buffer_.get(), 0, 1);
    enc->setFragmentBuffer(main_uniform_buffer_.get(), uniforms_offset, 2);

    enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
    enc->setCullMode(MTL::CullModeBack);

    uint32_t i = 0;
    for (auto &model : models_) {
      for (auto &node : model.nodes) {
        if (node.mesh_id == Model::invalid_id) {
          continue;
        }
        auto &mesh = model.meshes[node.mesh_id];
        // bind mesh stuff
        enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, mesh.index_count,
                                   MTL::IndexTypeUInt16, main_index_buffer_.get(),
                                   mesh.index_offset, 1,
                                   (uint32_t)(mesh.vertex_offset / sizeof(DefaultVertex)), i);
        i++;
      }
    }
  }

  enc->endEncoding();
  buf->presentDrawable(drawable);
  buf->commit();

  curr_frame_++;
  frame_ar_pool->release();
}

void RendererMetal::load_model(const std::filesystem::path &path) {
  auto result = ResourceManager::get().load_model(path, *this);
  assert(result.has_value());
  auto &model = result.value();

  pending_texture_uploads_.append_range(result->texture_uploads);
  result->texture_uploads.clear();

  for (const auto &node : model.model.nodes) {
    if (node.mesh_id == UINT32_MAX) {
      continue;
    }
    auto &mesh = model.model.meshes[node.mesh_id];
    const uint32_t instance_id = instance_idx_allocator_.alloc_idx();
    *((uint32_t *)instance_material_id_buf_->contents() + instance_id) = mesh.material_id;
    *((glm::mat4 *)instance_model_matrix_buf_->contents() + instance_id) = node.global_transform;
  }

  const size_t vertices_size = model.vertices.size() * sizeof(DefaultVertex);
  const size_t indices_size = model.indices.size() * sizeof(uint16_t);
  main_vert_buffer_ =
      NS::TransferPtr(raw_device_->newBuffer(vertices_size, MTL::ResourceStorageModeShared));
  memcpy(main_vert_buffer_->contents(), model.vertices.data(), vertices_size);
  main_index_buffer_ =
      NS::TransferPtr(raw_device_->newBuffer(indices_size, MTL::ResourceStorageModeShared));
  memcpy(main_index_buffer_->contents(), model.indices.data(), indices_size);
  models_.emplace_back(std::move(result->model));
  // TODO: material allocator
  memcpy(materials_buffer_->contents(), model.materials.data(),
         model.materials.size() * sizeof(Material));
  all_materials_.append_range(std::move(model.materials));
}

TextureWithIdx RendererMetal::load_material_image(const TextureDesc &desc) {
  MTL::TextureDescriptor *texture_desc = MTL::TextureDescriptor::alloc()->init();
  texture_desc->setWidth(desc.dims.x);
  texture_desc->setHeight(desc.dims.y);
  texture_desc->setDepth(desc.dims.z);
  texture_desc->setPixelFormat(util::mtl::convert_format(desc.format));
  texture_desc->setStorageMode(util::mtl::convert_storage_mode(desc.storage_mode));
  texture_desc->setMipmapLevelCount(desc.mip_levels);
  texture_desc->setArrayLength(desc.array_length);
  texture_desc->setAllowGPUOptimizedContents(true);
  texture_desc->setUsage(MTL::TextureUsageShaderRead);
  MTL::Texture *tex = raw_device_->newTexture(texture_desc);
  texture_desc->release();
  return {.tex = tex, .idx = texture_index_allocator_.alloc_idx()};
}

void RendererMetal::load_shaders() {
  NS::Error *err{};
  MTL::Library *shader_lib = raw_device_->newLibrary(
      util::mtl::string(resource_dir_ / "shader_out" / "default.metallib"), &err);

  if (err != nullptr) {
    util::mtl::print_err(err);
    return;
  }

  {
    forward_pass_shader_.vert_func = shader_lib->newFunction(util::mtl::string("vertexMain"));
    forward_pass_shader_.frag_func = shader_lib->newFunction(util::mtl::string("fragmentMain"));

    forward_mesh_shader_.mesh_func = shader_lib->newFunction(util::mtl::string("basic1_mesh_main"));
    forward_mesh_shader_.object_func =
        shader_lib->newFunction(util::mtl::string("basic1_object_main"));
    forward_mesh_shader_.frag_func =
        shader_lib->newFunction(util::mtl::string("basic1_fragment_main"));
  }
  shader_lib->release();
}

void RendererMetal::flush_pending_texture_uploads() {
  if (!pending_texture_uploads_.empty()) {
    MTL::CommandBuffer *buf = main_cmd_queue_->commandBuffer();
    MTL::BlitCommandEncoder *blit_enc = buf->blitCommandEncoder();
    for (auto &upload : pending_texture_uploads_) {
      MTL::Texture *tex = upload.tex;
      const auto src_img_size = static_cast<size_t>(upload.bytes_per_row) * upload.dims.y;
      MTL::Buffer *upload_buf =
          raw_device_->newBuffer(src_img_size, MTL::ResourceStorageModeShared);
      memcpy(upload_buf->contents(), upload.data, src_img_size);
      const MTL::Origin origin = MTL::Origin::Make(0, 0, 0);
      const MTL::Size img_size = MTL::Size::Make(upload.dims.x, upload.dims.y, upload.dims.z);
      blit_enc->copyFromBuffer(upload_buf, 0, upload.bytes_per_row, 0, img_size, tex, 0, 0, origin);
      blit_enc->generateMipmaps(tex);
      global_arg_enc_->setTexture(tex, upload.idx);
      tex->retain();
      all_textures_[upload.idx] = tex;
    }
    blit_enc->endEncoding();
    pending_texture_uploads_.clear();
    buf->commit();
    buf->waitUntilCompleted();
  }
}
