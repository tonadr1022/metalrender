#include "RendererMetal.hpp"

#include <Metal/Metal.hpp>
#include <glm/mat4x4.hpp>
#include <tracy/Tracy.hpp>

#include "ModelLoader.hpp"
#include "QuartzCore/CAMetalDrawable.hpp"
#include "QuartzCore/CAMetalLayer.hpp"
#include "WindowApple.hpp"
#include "core/BitUtil.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "dispatch_mesh_shared.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "mesh_shared.h"
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

  MTL::IndirectCommandBufferDescriptor *cmd_buf_desc =
      MTL::IndirectCommandBufferDescriptor::alloc()->init();
  cmd_buf_desc->setCommandTypes(MTL::IndirectCommandTypeDrawMeshThreadgroups);
  cmd_buf_desc->setInheritBuffers(false);
  cmd_buf_desc->setInheritPipelineState(true);
  cmd_buf_desc->setMaxFragmentBufferBindCount(2);
  cmd_buf_desc->setMaxMeshBufferBindCount(7);
  cmd_buf_desc->setMaxObjectBufferBindCount(1);
  ind_cmd_buf_ = NS::TransferPtr(
      raw_device_->newIndirectCommandBuffer(cmd_buf_desc, 1024, MTL::ResourceStorageModePrivate));
  cmd_buf_desc->release();

  {
    // TODO: handle resizing/more instances
    const uint32_t instance_capacity = instance_idx_allocator_.get_capacity();
    instance_data_buf_ = NS::TransferPtr(raw_device_->newBuffer(
        sizeof(InstanceData) * instance_capacity, MTL::ResourceStorageModeShared));
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
  {
    // mesh shader pipeline
    MTL::MeshRenderPipelineDescriptor *pipeline_desc =
        MTL::MeshRenderPipelineDescriptor::alloc()->init();
    pipeline_desc->setMeshFunction(forward_mesh_shader_.mesh_func);
    pipeline_desc->setObjectFunction(forward_mesh_shader_.object_func);
    pipeline_desc->setFragmentFunction(forward_mesh_shader_.frag_func);
    pipeline_desc->setLabel(util::mtl::string("basic mesh shader"));
    pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    pipeline_desc->setSupportIndirectCommandBuffers(true);

    NS::Error *err{};
    mesh_pso_ =
        raw_device_->newRenderPipelineState(pipeline_desc, MTL::PipelineOptionNone, nullptr, &err);
    if (err) {
      util::mtl::print_err(err);
      exit(1);
    }

    pipeline_desc->release();
  }
  {
    MTL::ComputePipelineDescriptor *pipeline_desc = MTL::ComputePipelineDescriptor::alloc()->init();
    pipeline_desc->setComputeFunction(dispatch_mesh_shader_.compute_func);
    pipeline_desc->setLabel(util::mtl::string("dispatch mesh compute"));
    NS::Error *err{};
    dispatch_mesh_pso_ =
        raw_device_->newComputePipelineState(pipeline_desc, MTL::PipelineOptionNone, nullptr, &err);
    if (err) {
      util::mtl::print_err(err);
      exit(1);
    }
    pipeline_desc->release();
  }

  // TODO: better size management this is awful
  scene_arg_buffer_ =
      NS::TransferPtr(raw_device_->newBuffer((8 * k_max_textures) + (8 * k_max_materials), 0));
  {
    MTL::ArgumentDescriptor *arg0 = MTL::ArgumentDescriptor::alloc()->init();
    size_t curr_idx = 0;
    arg0->setIndex(0);
    arg0->setAccess(MTL::ArgumentAccessReadOnly);
    arg0->setArrayLength(k_max_textures);
    curr_idx += k_max_textures;
    arg0->setDataType(MTL::DataTypeTexture);
    arg0->setTextureType(MTL::TextureType2D);

    MTL::ArgumentDescriptor *arg1 = MTL::ArgumentDescriptor::alloc()->init();
    arg1->setIndex(curr_idx);
    arg1->setAccess(MTL::ArgumentAccessReadOnly);
    arg1->setArrayLength(k_max_textures);
    arg1->setDataType(MTL::DataTypePointer);
    std::array<NS::Object *, 2> args_arr{arg0, arg1};
    const NS::Array *args = NS::Array::array(args_arr.data(), args_arr.size());
    global_arg_enc_ = raw_device_->newArgumentEncoder(args);
    global_arg_enc_->setArgumentBuffer(scene_arg_buffer_.get(), 0);
    for (auto &i : args_arr) {
      i->release();
    }
  }

  materials_buffer_ = NS::TransferPtr(raw_device_->newBuffer(k_max_materials, 0));

  // frag_enc->setBuffer(materials_buffer_.get(), 0, k_max_textures);
  MTL::Function *const funcs[] = {forward_pass_shader_.frag_func, forward_mesh_shader_.frag_func};
  for (const auto &func : funcs) {
    MTL::ArgumentEncoder *frag_enc = func->newArgumentEncoder(0);
    frag_enc->setArgumentBuffer(scene_arg_buffer_.get(), 0);
    frag_enc->release();
  }

  {
    // per frame uniform buffer
    main_uniform_buffer_ = NS::TransferPtr(raw_device_->newBuffer(
        frames_in_flight_ * util::align_256(sizeof(Uniforms)), MTL::ResourceStorageModeShared));
  }
}

void RendererMetal::shutdown() {}

void RendererMetal::render(const RenderArgs &render_args) {
  ZoneScoped;
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

  auto set_depth_stencil_state = [this](auto enc) {
    MTL::DepthStencilDescriptor *depth_stencil_desc = MTL::DepthStencilDescriptor::alloc()->init();
    depth_stencil_desc->setDepthCompareFunction(MTL::CompareFunctionLess);
    depth_stencil_desc->setDepthWriteEnabled(true);
    enc->setDepthStencilState(raw_device_->newDepthStencilState(depth_stencil_desc));
  };

  // TODO: this is awful
  auto use_scene_arg_buffer_resources = [this](auto enc) {
    for (const MTL::Texture *tex : all_textures_) {
      if (tex) {
        enc->useResource(tex, MTL::ResourceUsageSample);
      }
    }
    enc->useResource(materials_buffer_.get(), MTL::ResourceUsageRead);
  };

  // TODO: class for this
  const size_t uniforms_offset =
      (curr_frame_ % frames_in_flight_) * util::align_256(sizeof(Uniforms));
  auto *uniform_data = reinterpret_cast<Uniforms *>(
      reinterpret_cast<uint8_t *>(main_uniform_buffer_->contents()) + uniforms_offset);
  auto window_dims = window_->get_window_size();
  const float aspect = (window_dims.x != 0) ? float(window_dims.x) / float(window_dims.y) : 1.0f;
  uniform_data->vp =
      glm::perspective(glm::radians(70.f), aspect, 0.001f, 10000.f) * render_args.view_mat;
  uniform_data->render_mode = (uint32_t)RenderMode::Default;

  auto bind_fragment_resources = [this, &uniforms_offset](MTL::RenderCommandEncoder *enc) {
    enc->setFragmentBuffer(scene_arg_buffer_.get(), 0, 0);
    enc->setFragmentBuffer(main_uniform_buffer_.get(), uniforms_offset, 1);
  };

  {
    ZoneScopedN("encode draw cmds");
    if (draw_mode_ == DrawMode::IndirectMeshShader) {  // also implies mesh shaders

      {
        MTL::BlitCommandEncoder *reset_blit_enc = buf->blitCommandEncoder();
        reset_blit_enc->setLabel(util::mtl::string("Reset ICB Blit Encoder"));
        reset_blit_enc->resetCommandsInBuffer(ind_cmd_buf_.get(), NS::Range::Make(0, tot_meshes_));
        reset_blit_enc->endEncoding();
      }
      MTL::ComputePassDescriptor *compute_pass_descriptor =
          MTL::ComputePassDescriptor::computePassDescriptor();
      compute_pass_descriptor->setDispatchType(MTL::DispatchTypeSerial);
      MTL::ComputeCommandEncoder *compute_enc = buf->computeCommandEncoder(compute_pass_descriptor);
      compute_enc->setComputePipelineState(dispatch_mesh_pso_);
      compute_enc->setBuffer(dispatch_mesh_icb_container_buf_.get(), 0, 0);
      compute_enc->setBuffer(dispatch_mesh_encode_arg_buf_.get(), 0, 1);
      compute_enc->useResource(ind_cmd_buf_.get(), MTL::ResourceUsageWrite);
      compute_enc->useResource(obj_info_buf_.get(), MTL::ResourceUsageRead);
      DispatchMeshParams params{.tot_meshes = tot_meshes_,
                                .uniforms_offset = static_cast<uint32_t>(uniforms_offset)};
      compute_enc->setBytes(&params, sizeof(DispatchMeshParams), 2);
      // TODO: this is awfulllllllllllllll.
      tot_meshes_ = models_[0].tot_mesh_nodes;
      compute_enc->dispatchThreads(MTL::Size::Make(tot_meshes_, 1, 1), MTL::Size::Make(32, 1, 1));
      compute_enc->endEncoding();
      {
        MTL::BlitCommandEncoder *blit_enc = buf->blitCommandEncoder();
        blit_enc->setLabel(util::mtl::string("Optimize ICB Blit Encoder"));
        blit_enc->optimizeIndirectCommandBuffer(ind_cmd_buf_.get(),
                                                NS::Range::Make(0, tot_meshes_));
        blit_enc->endEncoding();
      }
      {
        MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(render_pass_desc);
        set_depth_stencil_state(enc);
        enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
        enc->setCullMode(MTL::CullModeBack);
        enc->setRenderPipelineState(mesh_pso_);
        const MTL::Resource *const resources[] = {
            main_vert_buffer_.get(),          meshlet_buf_.get(),
            instance_model_matrix_buf_.get(), instance_data_buf_.get(),
            meshlet_vertices_buf_.get(),      meshlet_triangles_buf_.get(),
            main_uniform_buffer_.get(),       scene_arg_buffer_.get(),
        };
        enc->useResources(resources, ARRAY_SIZE(resources), MTL::ResourceUsageRead);
        use_scene_arg_buffer_resources(enc);
        enc->executeCommandsInBuffer(ind_cmd_buf_.get(), NS::Range::Make(0, tot_meshes_));
        enc->endEncoding();
      }

    } else if (draw_mode_ == DrawMode::MeshShader) {
      MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(render_pass_desc);
      set_depth_stencil_state(enc);
      use_scene_arg_buffer_resources(enc);
      enc->setRenderPipelineState(mesh_pso_);
      enc->setMeshBuffer(main_uniform_buffer_.get(), uniforms_offset, 1);

      uint32_t i = 0;
      enc->setMeshBuffer(main_vert_buffer_.get(), 0, 0);
      enc->setMeshBuffer(meshlet_buf_.get(), 0, 2);
      enc->setMeshBuffer(meshlet_vertices_buf_.get(), 0, 3);
      enc->setMeshBuffer(meshlet_triangles_buf_.get(), 0, 4);
      bind_fragment_resources(enc);

      for (auto &model : models_) {
        for (auto &node : model.nodes) {
          if (node.mesh_id == Model::invalid_id) {
            continue;
          }
          const auto &meshlet_data = model.meshlet_datas[node.mesh_id];
          enc->setMeshBuffer(instance_data_buf_.get(), i * sizeof(InstanceData), 6);
          enc->setObjectBuffer(instance_data_buf_.get(), i * sizeof(InstanceData), 0);
          enc->setMeshBuffer(instance_model_matrix_buf_.get(), i * sizeof(glm::mat4), 5);
          struct TaskCmd {
            uint32_t instance_idx;
          } task_cmd{.instance_idx = i};
          enc->setObjectBytes(&task_cmd, sizeof(TaskCmd), 1);
          const uint32_t num_meshlets = meshlet_data.meshlets.size();
          const uint32_t threads_per_object_thread_group = 256;
          const uint32_t thread_groups_per_object =
              (num_meshlets + threads_per_object_thread_group - 1) /
              threads_per_object_thread_group;
          const uint32_t max_mesh_threads =
              std::max(k_max_triangles_per_meshlet, k_max_vertices_per_meshlet);
          const uint32_t threads_per_mesh_thread_group = max_mesh_threads;
          enc->drawMeshThreadgroups(MTL::Size::Make(thread_groups_per_object, 1, 1),
                                    MTL::Size::Make(threads_per_object_thread_group, 1, 1),
                                    MTL::Size::Make(threads_per_mesh_thread_group, 1, 1));

          i++;
        }
      }
      enc->endEncoding();
    } else {  // draw_mode_ == DrawMode::VertexShader
      MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(render_pass_desc);
      set_depth_stencil_state(enc);
      use_scene_arg_buffer_resources(enc);
      enc->setRenderPipelineState(main_pso_);
      enc->setVertexBuffer(main_vert_buffer_.get(), 0, 0);
      enc->setVertexBuffer(main_uniform_buffer_.get(), uniforms_offset, 1);
      enc->setVertexBuffer(instance_model_matrix_buf_.get(), 0, 2);
      enc->setVertexBuffer(instance_material_id_buf_.get(), 0, 3);
      bind_fragment_resources(enc);

      enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
      enc->setCullMode(MTL::CullModeBack);

      uint32_t i = 0;
      for (auto &model : models_) {
        for (auto &node : model.nodes) {
          if (node.mesh_id == Model::invalid_id) {
            continue;
          }
          const auto &mesh = model.meshes[node.mesh_id];
          // bind mesh stuff
          enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, mesh.index_count,
                                     MTL::IndexTypeUInt32, main_index_buffer_.get(),
                                     mesh.index_offset, 1,
                                     (uint32_t)(mesh.vertex_offset / sizeof(DefaultVertex)), i);
          i++;
        }
      }
      enc->endEncoding();
    }
  }
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

  // TODO: MOVEEEEEEEEEEEE PLEASE!!!!!!!!!!!!!
  auto &meshlet_datas = model.model.meshlet_datas;
  size_t tot_meshlet_count{};
  size_t tot_meshlet_verts_count{};
  size_t tot_meshlet_tri_count{};
  for (auto &meshlet_data : meshlet_datas) {
    meshlet_data.meshlet_triangles_offset = tot_meshlet_tri_count;
    meshlet_data.meshlet_vertices_offset = tot_meshlet_verts_count;
    meshlet_data.meshlet_base = tot_meshlet_count;
    tot_meshlet_count += meshlet_data.meshlets.size();
    tot_meshlet_verts_count += meshlet_data.meshlet_vertices.size();
    tot_meshlet_tri_count += meshlet_data.meshlet_triangles.size();
  }

  uint32_t instance_id = 0;
  for (const auto &node : model.model.nodes) {
    if (node.mesh_id == UINT32_MAX) {
      continue;
    }
    auto &mesh = model.model.meshes[node.mesh_id];
    const auto &meshlet_data = model.model.meshlet_datas[node.mesh_id];
    *((InstanceData *)instance_data_buf_->contents() + instance_id) =
        InstanceData{.instance_id = instance_id,
                     .mat_id = mesh.material_id,
                     .meshlet_base = meshlet_data.meshlet_base,
                     .meshlet_count = static_cast<uint32_t>(meshlet_data.meshlets.size()),
                     .meshlet_vertices_offset = meshlet_data.meshlet_vertices_offset,
                     .meshlet_triangles_offset = meshlet_data.meshlet_triangles_offset};
    *((uint32_t *)instance_material_id_buf_->contents() + instance_id) = mesh.material_id;
    *((glm::mat4 *)instance_model_matrix_buf_->contents() + instance_id) = node.global_transform;
    instance_id++;
  }

  const size_t vertices_size = model.model.vertices.size() * sizeof(DefaultVertex);
  const size_t indices_size = model.model.indices.size() * sizeof(IndexT);
  main_vert_buffer_ =
      NS::TransferPtr(raw_device_->newBuffer(vertices_size, MTL::ResourceStorageModeShared));
  memcpy(main_vert_buffer_->contents(), model.model.vertices.data(), vertices_size);
  main_index_buffer_ =
      NS::TransferPtr(raw_device_->newBuffer(indices_size, MTL::ResourceStorageModeShared));
  memcpy(main_index_buffer_->contents(), model.model.indices.data(), indices_size);
  // TODO: material allocator
  memcpy(materials_buffer_->contents(), model.materials.data(),
         model.materials.size() * sizeof(Material));
  all_materials_.append_range(std::move(model.materials));
  global_arg_enc_->setBuffer(materials_buffer_.get(), 0, 1024);

  {
    std::vector<ObjectInfo> obj_info_buf;
    const auto &nodes = model.model.nodes;
    obj_info_buf.reserve(nodes.size());
    for (const auto &node : nodes) {
      if (node.mesh_id == Model::invalid_id) {
        continue;
      }
      const auto &meshlet_data = model.model.meshlet_datas[node.mesh_id];
      obj_info_buf.emplace_back(
          ObjectInfo{.num_meshlets = static_cast<uint32_t>(meshlet_data.meshlets.size())});
    }
    obj_info_buf_ = create_buffer(obj_info_buf.size() * sizeof(ObjectInfo), obj_info_buf.data());

    // std::vector<GPUMeshData> gpu_mesh_datas;
    // const auto &meshes = model.model.meshes;
    // gpu_mesh_datas.reserve(meshes.size());
    // for (const auto &mesh : meshes) {
    //   gpu_mesh_datas.emplace_back(GPUMeshData{
    //       .vertex_offset = static_cast<uint32_t>(mesh.vertex_offset / sizeof(DefaultVertex)),
    //       .vertex_count = mesh.vertex_count});
    // }
    // gpu_mesh_data_buf_ =
    //     create_buffer(gpu_mesh_datas.size() * sizeof(GPUMeshData), gpu_mesh_datas.data());
  }

  {
    const size_t tot_meshlet_size = tot_meshlet_count * sizeof(meshopt_Meshlet);
    const size_t tot_meshlet_verts_size = tot_meshlet_verts_count * sizeof(uint32_t);
    const size_t tot_meshlet_tri_size = tot_meshlet_tri_count * sizeof(uint8_t);
    meshlet_buf_ =
        NS::TransferPtr(raw_device_->newBuffer(tot_meshlet_size, MTL::ResourceStorageModeShared));
    meshlet_vertices_buf_ = NS::TransferPtr(
        raw_device_->newBuffer(tot_meshlet_verts_size, MTL::ResourceStorageModeShared));
    meshlet_triangles_buf_ = NS::TransferPtr(
        raw_device_->newBuffer(tot_meshlet_tri_size, MTL::ResourceStorageModeShared));
    size_t meshlet_offset{};
    size_t meshlet_tri_offset{};
    size_t meshlet_verts_offset{};
    for (const auto &meshlet_data : meshlet_datas) {
      const size_t meshlet_copy_size = meshlet_data.meshlets.size() * sizeof(meshopt_Meshlet);
      const size_t meshlet_vert_copy_size = meshlet_data.meshlet_vertices.size() * sizeof(uint32_t);
      const size_t meshlet_tri_copy_size = meshlet_data.meshlet_triangles.size() * sizeof(uint8_t);
      memcpy((uint8_t *)meshlet_buf_->contents() + meshlet_offset, meshlet_data.meshlets.data(),
             meshlet_copy_size);
      memcpy((uint8_t *)meshlet_vertices_buf_->contents() + meshlet_verts_offset,
             meshlet_data.meshlet_vertices.data(), meshlet_vert_copy_size);
      memcpy((uint8_t *)meshlet_triangles_buf_->contents() + meshlet_tri_offset,
             meshlet_data.meshlet_triangles.data(), meshlet_tri_copy_size);
      meshlet_offset += meshlet_copy_size;
      meshlet_tri_offset += meshlet_tri_copy_size;
      meshlet_verts_offset += meshlet_vert_copy_size;
    }
  }
  {
    {
      MTL::ArgumentEncoder *enc = dispatch_mesh_shader_.compute_func->newArgumentEncoder(0);
      dispatch_mesh_icb_container_buf_ = NS::TransferPtr(
          raw_device_->newBuffer(enc->encodedLength(), MTL::ResourceStorageModeShared));
      enc->setArgumentBuffer(dispatch_mesh_icb_container_buf_.get(), 0);
      enc->setIndirectCommandBuffer(ind_cmd_buf_.get(), 0);
      enc->setBuffer(obj_info_buf_.get(), 0, 1);
      enc->release();
    }
    {
      MTL::ArgumentEncoder *enc = dispatch_mesh_shader_.compute_func->newArgumentEncoder(1);
      // TODO: frames in flight
      dispatch_mesh_encode_arg_buf_ = NS::TransferPtr(
          raw_device_->newBuffer(enc->encodedLength() * 2, MTL::ResourceStorageModeShared));
      enc->setArgumentBuffer(dispatch_mesh_encode_arg_buf_.get(), 0);
      enc->setBuffer(main_vert_buffer_.get(), 0, EncodeMeshDrawArgs_MainVertexBuf);
      enc->setBuffer(meshlet_buf_.get(), 0, EncodeMeshDrawArgs_MeshletBuf);
      enc->setBuffer(instance_model_matrix_buf_.get(), 0,
                     EncodeMeshDrawArgs_InstanceModelMatrixBuf);
      enc->setBuffer(instance_data_buf_.get(), 0, EncodeMeshDrawArgs_InstanceDataBuf);
      enc->setBuffer(meshlet_vertices_buf_.get(), 0, EncodeMeshDrawArgs_MeshletVerticesBuf);
      enc->setBuffer(meshlet_triangles_buf_.get(), 0, EncodeMeshDrawArgs_MeshletTrianglesBuf);
      enc->setBuffer(main_uniform_buffer_.get(), 0, EncodeMeshDrawArgs_MainUniformBuf);
      enc->setBuffer(scene_arg_buffer_.get(), 0, EncodeMeshDrawArgs_SceneArgBuf);
      enc->release();
    }
  }
  models_.emplace_back(std::move(result->model));
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
    dispatch_mesh_shader_.compute_func =
        shader_lib->newFunction(util::mtl::string("dispatch_mesh_main"));
  }
  shader_lib->release();
}

NS::SharedPtr<MTL::Buffer> RendererMetal::create_buffer(size_t size, void *data,
                                                        MTL::ResourceOptions options) {
  NS::SharedPtr<MTL::Buffer> buf = NS::TransferPtr(raw_device_->newBuffer(size, options));
  memcpy(buf->contents(), data, size);
  return buf;
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
