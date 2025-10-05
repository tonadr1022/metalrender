#include "RendererMetal.hpp"

#include <Metal/MTLArgumentEncoder.hpp>
#include <Metal/MTLCommandBuffer.hpp>
#include <Metal/MTLDevice.hpp>

#include "gfx/GFXTypes.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define IMGUI_IMPL_METAL_CPP
#include <imgui_impl_metal.h>

#include <Metal/Metal.hpp>
#include <glm/mat4x4.hpp>
#include <tracy/Tracy.hpp>

#include "ModelLoader.hpp"
#include "QuartzCore/CAMetalDrawable.hpp"
#include "QuartzCore/CAMetalLayer.hpp"
#include "WindowApple.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "dispatch_mesh_shared.h"
#include "dispatch_vertex_shared.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "imgui_impl_glfw.h"
#include "mesh_shared.h"
#include "metal/BackedGPUAllocator.hpp"
#include "metal/MetalDevice.hpp"
#include "metal/MetalUtil.hpp"
#include "shader_global_uniforms.h"
namespace {

// uint32_t get_mip_levels(uint32_t width, uint32_t height) {
//   return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
// }

enum class RenderMode : uint32_t { Default, Normals, NormalMap };

}  // namespace

InstanceDataMgr::InstanceDataMgr(size_t initial_element_cap, MTL::Device *raw_device,
                                 rhi::Device *device)
    : allocator_(initial_element_cap), device_(device), raw_device_(raw_device) {
  allocate_buffers(initial_element_cap);

  // TODO: parameterize this or something better?
  resize_icb(initial_element_cap);
}

OffsetAllocator::Allocation InstanceDataMgr::allocate(size_t element_count) {
  const OffsetAllocator::Allocation alloc = allocator_.allocate(element_count);
  if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
    auto old_capacity = allocator_.capacity();
    auto new_capacity = old_capacity * 2;
    allocator_.grow(allocator_.capacity());
    ASSERT(new_capacity <= allocator_.capacity());
    // TODO: copy contents
    auto old_instance_data_buf = std::move(instance_data_buf_);
    allocate_buffers(new_capacity);
    memcpy(instance_data_buf()->contents(), device_->get_buf(old_instance_data_buf)->contents(),
           old_capacity * sizeof(InstanceData));
    return allocate(element_count);
  }
  curr_element_count_ += element_count;
  max_seen_size_ = std::max(max_seen_size_, curr_element_count_);
  resize_icb(alloc.offset + element_count);
  return alloc;
}

void InstanceDataMgr::allocate_buffers(size_t element_count) {
  instance_data_buf_ = device_->create_buf_h(rhi::BufferDesc{
      .storage_mode = rhi::StorageMode::Default, .size = sizeof(InstanceData) * element_count});
}

void InstanceDataMgr::resize_icb(size_t element_count) {
  if (element_count > icb_element_count_) {
    icb_element_count_ = std::max<size_t>(icb_element_count_ * 2ull, element_count);
    MTL::IndirectCommandBufferDescriptor *cmd_buf_desc =
        MTL::IndirectCommandBufferDescriptor::alloc()->init();
    cmd_buf_desc->setCommandTypes(MTL::IndirectCommandTypeDrawMeshThreadgroups);
    cmd_buf_desc->setInheritBuffers(false);
    cmd_buf_desc->setInheritPipelineState(true);
    cmd_buf_desc->setMaxFragmentBufferBindCount(3);
    cmd_buf_desc->setMaxMeshBufferBindCount(8);
    cmd_buf_desc->setMaxObjectBufferBindCount(5);
    ASSERT(raw_device_ != nullptr);
    ind_cmd_buf_ = NS::TransferPtr(raw_device_->newIndirectCommandBuffer(
        cmd_buf_desc, icb_element_count_, MTL::ResourceStorageModePrivate));
    cmd_buf_desc->release();
  }
}

GPUFrameAllocator::GPUFrameAllocator(rhi::Device *device, size_t size, size_t frames_in_flight)
    : frames_in_flight_(frames_in_flight), device_(device) {
  ALWAYS_ASSERT(frames_in_flight < k_max_frames_in_flight);
  for (size_t i = 0; i < frames_in_flight; i++) {
    buffers_[i] = device_->create_buf_h(rhi::BufferDesc{
        .storage_mode = rhi::StorageMode::Default, .size = size, .alloc_gpu_slot = false});
  }
}

void RendererMetal::init(const CreateInfo &cinfo) {
  ZoneScoped;
  device_ = cinfo.device;
  window_ = cinfo.window;
  render_imgui_callback_ = cinfo.render_imgui_callback;
  shader_dir_ = cinfo.resource_dir / "shaders";
  resource_dir_ = cinfo.resource_dir;
  assert(!shader_dir_.empty());
  raw_device_ = device_->get_device();
  main_cmd_queue_ = raw_device_->newCommandQueue();
  gpu_frame_allocator_ = GPUFrameAllocator{device_, 256ull * 6, frames_in_flight_};

  gpu_uniform_buf_.emplace(gpu_frame_allocator_->create_buffer<Uniforms>(1));
  cull_data_buf_.emplace(gpu_frame_allocator_->create_buffer<CullData>(1));

  init_imgui();

  const auto initial_instance_capacity{128};
  instance_data_mgr_.emplace(initial_instance_capacity, raw_device_, device_);
  static_draw_batch_.emplace(DrawBatchType::Static, *device_,
                             DrawBatch::CreateInfo{
                                 .initial_vertex_capacity = 1'000'00,
                                 .initial_index_capacity = 1'000'00,
                                 .initial_meshlet_capacity = 100'000,
                                 .initial_mesh_capacity = 20'000,
                                 .initial_meshlet_triangle_capacity = 1'00'000,
                                 .initial_meshlet_vertex_capacity = 1'000'00,
                             });

  // TODO: rethink
  all_textures_.resize(k_max_textures);

  recreate_render_target_textures();

  load_shaders();
  load_pipelines();

  // TODO: better size management this is awful
  scene_arg_buffer_ = NS::TransferPtr(raw_device_->newBuffer(
      (sizeof(uint64_t) * k_max_textures) + (sizeof(uint64_t) * k_max_buffers), 0));
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

  {
    MTL::ArgumentDescriptor *arg = MTL::ArgumentDescriptor::alloc()->init();
    arg->setIndex(0);
    arg->setAccess(MTL::BindingAccessReadWrite);
    arg->setDataType(MTL::DataTypeIndirectCommandBuffer);
    std::array<NS::Object *, 1> args_arr{arg};
    const NS::Array *args = NS::Array::array(args_arr.data(), args_arr.size());
    main_icb_container_arg_enc_ = raw_device_->newArgumentEncoder(args);
    main_icb_container_buf_ = device_->create_buf_h(
        rhi::BufferDesc{.storage_mode = rhi::StorageMode::Default,
                        .size = main_icb_container_arg_enc_->encodedLength()});
    main_icb_container_arg_enc_->setArgumentBuffer(get_mtl_buf(main_icb_container_buf_), 0);
  }
  {
    dispatch_mesh_encode_arg_enc_ = get_function("dispatch_mesh_main")->newArgumentEncoder(1);
    dispatch_mesh_encode_arg_buf_ = NS::TransferPtr(raw_device_->newBuffer(
        dispatch_mesh_encode_arg_enc_->encodedLength(), MTL::ResourceStorageModeShared));
    dispatch_mesh_encode_arg_enc_->setArgumentBuffer(dispatch_mesh_encode_arg_buf_.get(), 0);
  }
  {
    dispatch_vertex_encode_arg_enc_ = get_function("dispatch_vertex_main")->newArgumentEncoder(1);
    dispatch_vertex_encode_arg_buf_ = NS::TransferPtr(raw_device_->newBuffer(
        dispatch_vertex_encode_arg_enc_->encodedLength(), MTL::ResourceStorageModeShared));
    dispatch_vertex_encode_arg_enc_->setArgumentBuffer(dispatch_vertex_encode_arg_buf_.get(), 0);
  }

  materials_buf_.emplace(*device_,
                         rhi::BufferDesc{rhi::StorageMode::CPUAndGPU, k_max_textures, true},
                         sizeof(Material));
  global_arg_enc_->setBuffer(static_cast<MetalBuffer *>(materials_buf_->get_buffer())->buffer(), 0,
                             k_max_textures);
}

void RendererMetal::shutdown() { shutdown_imgui(); }

void RendererMetal::render(const RenderArgs &render_args) {
  ZoneScoped;
  flush_pending_texture_uploads();
  auto *frame_ar_pool = NS::AutoreleasePool::alloc()->init();

  auto dims = window_->get_window_size();
  window_->metal_layer_->setDrawableSize(CGSizeMake(dims.x, dims.y));
  static auto curr_dims = dims;
  if (curr_dims != dims) {
    recreate_render_target_textures();
  }
  const CA::MetalDrawable *drawable = window_->metal_layer_->nextDrawable();
  if (!drawable) {
    frame_ar_pool->release();
    return;
  }

  MTL::CommandBuffer *buf = main_cmd_queue_->commandBuffer();

  {
    ZoneScopedN("encode and commit commands");

    const Uniforms cpu_uniforms = set_cpu_global_uniform_data(render_args);
    gpu_uniform_buf_->fill(cpu_uniforms);
    cull_data_buf_->fill(set_cpu_cull_data(cpu_uniforms, render_args));

    {
      MTL::BlitCommandEncoder *reset_blit_enc = buf->blitCommandEncoder();
      reset_blit_enc->setLabel(util::mtl::string("Reset ICB Blit Encoder"));
      reset_blit_enc->resetCommandsInBuffer(instance_data_mgr_->icb(),
                                            NS::Range::Make(0, all_model_data_.max_objects));
      reset_blit_enc->endEncoding();
    }

    {  // fill ICB
      MTL::ComputePassDescriptor *compute_pass_descriptor =
          MTL::ComputePassDescriptor::computePassDescriptor();
      compute_pass_descriptor->setDispatchType(MTL::DispatchTypeConcurrent);
      MTL::ComputeCommandEncoder *compute_enc = buf->computeCommandEncoder(compute_pass_descriptor);
      if (use_mesh_shader) {
        compute_enc->setComputePipelineState(dispatch_mesh_pso_);
        compute_enc->setBuffer(get_mtl_buf(main_icb_container_buf_), 0, 0);
        compute_enc->setBuffer(dispatch_mesh_encode_arg_buf_.get(), 0, 1);
        compute_enc->setBuffer(
            reinterpret_cast<MetalBuffer *>(gpu_uniform_buf_->get_buf())->buffer(),
            gpu_uniform_buf_->get_offset_bytes(), 3);
        compute_enc->setBuffer(reinterpret_cast<MetalBuffer *>(cull_data_buf_->get_buf())->buffer(),
                               cull_data_buf_->get_offset_bytes(), 4);
        compute_enc->useResource(instance_data_mgr_->icb(), MTL::ResourceUsageWrite);
        compute_enc->useResource(instance_data_mgr_->instance_data_buf(), MTL::ResourceUsageRead);
        compute_enc->useResource(get_mtl_buf(*materials_buf_), MTL::ResourceUsageRead);
        DispatchMeshParams params{.tot_meshes = all_model_data_.max_objects};
        compute_enc->setBytes(&params, sizeof(DispatchMeshParams), 2);
        // TODO: this is awfulllllllllllllll.
        compute_enc->dispatchThreads(MTL::Size::Make(all_model_data_.max_objects, 1, 1),
                                     MTL::Size::Make(32, 1, 1));
        compute_enc->endEncoding();
      } else {
        compute_enc->setComputePipelineState(dispatch_vertex_pso_);
        // compute_enc->setBuffer();
      }
    }

    {
      MTL::BlitCommandEncoder *blit_enc = buf->blitCommandEncoder();
      blit_enc->setLabel(util::mtl::string("Optimize ICB Blit Encoder"));
      blit_enc->optimizeIndirectCommandBuffer(instance_data_mgr_->icb(),
                                              NS::Range::Make(0, all_model_data_.max_objects));
      blit_enc->endEncoding();
    }

    MTL::RenderPassDescriptor *forward_render_pass_desc =
        MTL::RenderPassDescriptor::renderPassDescriptor();
    MTL::RenderPassDepthAttachmentDescriptor *desc =
        MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
    desc->setTexture(reinterpret_cast<MetalTexture *>(device_->get_tex(depth_tex_))->texture());
    desc->setClearDepth(1.0);
    desc->setLoadAction(MTL::LoadActionClear);
    desc->setStoreAction(MTL::StoreActionStore);
    forward_render_pass_desc->setDepthAttachment(desc);
    {
      auto *color0 = forward_render_pass_desc->colorAttachments()->object(0);
      color0->setTexture(drawable->texture());
      color0->setLoadAction(MTL::LoadActionClear);
      color0->setClearColor(MTL::ClearColor::Make(0.5, 0.1, 0.12, 1.0));
      color0->setStoreAction(MTL::StoreActionStore);
    }
    MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(forward_render_pass_desc);

    {  // TODO: don't recreate depth stencil state?
      MTL::DepthStencilDescriptor *depth_stencil_desc =
          MTL::DepthStencilDescriptor::alloc()->init();
      depth_stencil_desc->setDepthCompareFunction(MTL::CompareFunctionLess);
      depth_stencil_desc->setDepthWriteEnabled(true);
      enc->setDepthStencilState(raw_device_->newDepthStencilState(depth_stencil_desc));
    }

    {
      ZoneScopedN("encode draw cmds");
      enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
      enc->setCullMode(MTL::CullModeBack);
      enc->setRenderPipelineState(mesh_pso_);
      const MTL::Resource *const resources[] = {
          instance_data_mgr_->instance_data_buf(),
          get_mtl_buf(static_draw_batch_->vertex_buf),
          get_mtl_buf(static_draw_batch_->meshlet_buf),
          get_mtl_buf(static_draw_batch_->mesh_buf),
          get_mtl_buf(static_draw_batch_->meshlet_vertices_buf),
          get_mtl_buf(static_draw_batch_->meshlet_triangles_buf),
          scene_arg_buffer_.get(),
          get_mtl_buf(*materials_buf_),
      };
      enc->useResources(resources, ARRAY_SIZE(resources), MTL::ResourceUsageRead);
      for (const MTL::Texture *tex : all_textures_) {
        if (tex) {
          enc->useResource(tex, MTL::ResourceUsageSample);
        }
      }
      enc->useResource(get_mtl_buf(*materials_buf_), MTL::ResourceUsageRead);
      enc->executeCommandsInBuffer(instance_data_mgr_->icb(),
                                   NS::Range::Make(0, all_model_data_.max_objects));
    }

    if (render_args.draw_imgui) {
      {
        ZoneScopedN("Imgui frame init");
        // TODO: don't recrete this
        const MTL::RenderPassDescriptor *imgui_pass_desc =
            MTL::RenderPassDescriptor::renderPassDescriptor();
        imgui_pass_desc->colorAttachments()->object(0)->setTexture(drawable->texture());
        imgui_pass_desc->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionLoad);
        imgui_pass_desc->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        ImGui_ImplMetal_NewFrame(forward_render_pass_desc);
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
      }
      render_imgui();
      ImGui::Render();
      ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), buf, enc);
    }

    enc->endEncoding();

    if (render_args.draw_imgui) {
      ImGui::EndFrame();
    }

    buf->presentDrawable(drawable);
    buf->commit();

    curr_frame_++;
    frame_ar_pool->release();
  }
}

bool RendererMetal::load_model(const std::filesystem::path &path, const glm::mat4 &root_transform,
                               ModelInstance &model, ModelGPUHandle &out_handle) {
  ModelLoadResult result;
  if (!model::load_model(path, *this, root_transform, model, result)) {
    return false;
  }

  pending_texture_uploads_.reserve(result.texture_uploads.size());
  for (auto &u : result.texture_uploads) {
    pending_texture_uploads_.push_back(std::move(u));
  }

  auto draw_batch_alloc = upload_geometry(DrawBatchType::Static, result.vertices, result.indices,
                                          result.meshlet_process_result, result.meshes);

  std::vector<InstanceData> base_instance_datas;
  std::vector<uint32_t> instance_id_to_node;
  base_instance_datas.reserve(model.tot_mesh_nodes);
  instance_id_to_node.reserve(model.tot_mesh_nodes);

  {
    uint32_t instance_copy_idx = 0;
    for (size_t node = 0; node < model.nodes.size(); node++) {
      auto mesh_id = model.mesh_ids[node];
      if (model.mesh_ids[node] == Mesh::k_invalid_mesh_id) {
        continue;
      }
      base_instance_datas.emplace_back(
          InstanceData{.instance_id = instance_copy_idx,
                       .mat_id = result.meshes[mesh_id].material_id,
                       .mesh_id = draw_batch_alloc.mesh_alloc.offset + mesh_id});
      instance_id_to_node.push_back(node);
      instance_copy_idx++;
    }
  }

  bool resized{};
  assert(!result.materials.empty());
  auto material_alloc = materials_buf_->allocate(result.materials.size(), resized);
  {
    memcpy(reinterpret_cast<Material *>(materials_buf_->get_buffer()->contents()) +
               material_alloc.offset,
           result.materials.data(), result.materials.size() * sizeof(Material));
    if (resized) {
      LINFO("materials resized");
      ASSERT(0);
    }
  }

  out_handle = model_gpu_resource_pool_.alloc(
      ModelGPUResources{.material_alloc = material_alloc,
                        .static_draw_batch_alloc = draw_batch_alloc,
                        .base_instance_datas = std::move(base_instance_datas),
                        .instance_id_to_node = instance_id_to_node});
  return true;
}

ModelInstanceGPUHandle RendererMetal::add_model_instance(const ModelInstance &model,
                                                         ModelGPUHandle model_gpu_handle) {
  ZoneScoped;
  auto &model_instance_datas = model_gpu_resource_pool_.get(model_gpu_handle)->base_instance_datas;
  auto &instance_id_to_node = model_gpu_resource_pool_.get(model_gpu_handle)->instance_id_to_node;
  std::vector<TRS> instance_transforms;
  instance_transforms.reserve(model_instance_datas.size());
  std::vector<InstanceData> instance_datas = {model_instance_datas.begin(),
                                              model_instance_datas.end()};
  ASSERT(instance_datas.size() == instance_id_to_node.size());

  const OffsetAllocator::Allocation instance_data_gpu_alloc =
      instance_data_mgr_->allocate(model_instance_datas.size());
  all_model_data_.max_objects = instance_data_mgr_->max_seen_size();

  for (size_t i = 0; i < instance_datas.size(); i++) {
    ASSERT(instance_datas[i].instance_id == i);
    instance_transforms.push_back(model.global_transforms[instance_id_to_node[i]]);
    const auto &transform = model.global_transforms[instance_id_to_node[i]];
    const auto rot = transform.rotation;
    // TODO: do this initially?
    instance_datas[i].translation = transform.translation;
    instance_datas[i].rotation = glm::vec4{rot[0], rot[1], rot[2], rot[3]};
    instance_datas[i].scale = transform.scale;
    instance_datas[i].instance_id += instance_data_gpu_alloc.offset;
  }

  memcpy(reinterpret_cast<InstanceData *>(instance_data_mgr_->instance_data_buf()->contents()) +
             instance_data_gpu_alloc.offset,
         instance_datas.data(), instance_datas.size() * sizeof(InstanceData));

  main_icb_container_arg_enc_->setIndirectCommandBuffer(instance_data_mgr_->icb(), 0);

  if (use_mesh_shader) {  // move this bs
    // on main vertex buffer resize at beginning of frame
    dispatch_mesh_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->vertex_buf), 0,
                                             EncodeMeshDrawArgs_MainVertexBuf);
    dispatch_mesh_encode_arg_enc_->setBuffer(
        reinterpret_cast<MetalBuffer *>(static_draw_batch_->meshlet_buf.get_buffer())->buffer(), 0,
        EncodeMeshDrawArgs_MeshletBuf);
    dispatch_mesh_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->mesh_buf), 0,
                                             EncodeMeshDrawArgs_MeshDataBuf);
    dispatch_mesh_encode_arg_enc_->setBuffer(instance_data_mgr_->instance_data_buf(), 0,
                                             EncodeMeshDrawArgs_InstanceDataBuf);
    dispatch_mesh_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->meshlet_vertices_buf),
                                             0, EncodeMeshDrawArgs_MeshletVerticesBuf);
    dispatch_mesh_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->meshlet_triangles_buf),
                                             0, EncodeMeshDrawArgs_MeshletTrianglesBuf);
    dispatch_mesh_encode_arg_enc_->setBuffer(scene_arg_buffer_.get(), 0,
                                             EncodeMeshDrawArgs_SceneArgBuf);
  } else {
    dispatch_vertex_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->vertex_buf), 0,
                                               DispatchVertexShaderArgs_MainVertexBuf);
    dispatch_vertex_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->index_buf), 0,
                                               DispatchVertexShaderArgs_MainIndexBuf);
    dispatch_vertex_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->mesh_buf), 0,
                                               DispatchVertexShaderArgs_MeshDataBuf);
    dispatch_vertex_encode_arg_enc_->setBuffer(scene_arg_buffer_.get(), 0,
                                               DispatchVertexShaderArgs_SceneArgBuf);
    dispatch_vertex_encode_arg_enc_->setBuffer(instance_data_mgr_->instance_data_buf(), 0,
                                               DispatchVertexShaderArgs_InstanceDataBuf);
  }

  return model_instance_gpu_resource_pool_.alloc(
      ModelInstanceGPUResources{.instance_data_gpu_alloc = instance_data_gpu_alloc});
}

void RendererMetal::free_model(ModelGPUHandle handle) {
  auto *gpu_resources = model_gpu_resource_pool_.get(handle);
  ASSERT(gpu_resources);
  if (!gpu_resources) {
    return;
  }
  // TODO: free textures

  materials_buf_->free(gpu_resources->material_alloc);
  static_draw_batch_->free(gpu_resources->static_draw_batch_alloc);
  model_gpu_resource_pool_.destroy(handle);
}

void RendererMetal::free_instance(ModelInstanceGPUHandle handle) {
  auto *gpu_resources = model_instance_gpu_resource_pool_.get(handle);
  ASSERT(gpu_resources);
  if (!gpu_resources) {
    return;
  }
  instance_data_mgr_->free(gpu_resources->instance_data_gpu_alloc);
}

void RendererMetal::on_imgui() {
  const DrawBatch *const draw_batches[] = {&static_draw_batch_.value()};
  for (const auto &batch : draw_batches) {
    ImGui::Text("Draw Batch: %s", draw_batch_type_to_string(batch->type));
    auto stats = batch->get_stats();
    ImGui::Text("\tVertex Count: %d", stats.vertex_count);
    ImGui::Text("\tIndex Count: %d", stats.index_count);
    ImGui::Text("\tMeshlet Count: %d", stats.meshlet_count);
    ImGui::Text("\tMeshlet Triangle Count: %d", stats.meshlet_triangle_count);
    ImGui::Text("\tMeshlet Vertex Count: %d", stats.meshlet_vertex_count);
  }
  ImGui::Checkbox("meshlet frustum cull", &meshlet_frustum_cull_);
}

// TextureWithIdx RendererMetal::load_material_image(const rhi::TextureDesc &desc) {
//   return {.tex = device_->create_texture(desc), .idx = texture_index_allocator_.alloc_idx()};
// }

void RendererMetal::load_shaders() {
  NS::Error *err{};
  MTL::Library *shader_lib = raw_device_->newLibrary(
      util::mtl::string(resource_dir_ / "shader_out" / "default.metallib"), &err);

  if (err != nullptr) {
    util::mtl::print_err(err);
    return;
  }

  {
    auto add_func = [this, &shader_lib](const char *name) {
      shader_funcs_.emplace(name, shader_lib->newFunction(util::mtl::string(name)));
    };
    add_func("vertexMain");
    add_func("fragmentMain");
    add_func("basic1_mesh_main");
    add_func("basic1_object_main");
    add_func("basic1_fragment_main");
    add_func("dispatch_mesh_main");
    add_func("dispatch_vertex_main");
  }
  shader_lib->release();
}

void RendererMetal::init_imgui() {
  ZoneScoped;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  [[maybe_unused]] ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window_->get_handle(), true);
  ImGui_ImplMetal_Init(raw_device_);
}

void RendererMetal::shutdown_imgui() {
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void RendererMetal::render_imgui() {
  if (render_imgui_callback_) {
    render_imgui_callback_();
  }
}

Uniforms RendererMetal::set_cpu_global_uniform_data(const RenderArgs &render_args) const {
  Uniforms uniform_data{};
  const auto window_dims = window_->get_window_size();
  const float aspect = (window_dims.x != 0) ? float(window_dims.x) / float(window_dims.y) : 1.0f;
  uniform_data.view = render_args.view_mat;
  uniform_data.proj = glm::perspective(glm::radians(70.f), aspect, k_z_near, k_z_far);
  uniform_data.vp =
      glm::perspective(glm::radians(70.f), aspect, k_z_near, k_z_far) * uniform_data.view;
  uniform_data.render_mode = (uint32_t)RenderMode::Normals;
  return uniform_data;
}

CullData RendererMetal::set_cpu_cull_data(const Uniforms &uniforms,
                                          const RenderArgs &render_args) const {
  // https:github.com/zeux/niagara/blob/7fa51801abc258c3cb05e9a615091224f02e11cf/src/niagara.cpp#L128
  CullData cull_data{};
  const glm::mat4 projection_transpose = glm::transpose(uniforms.proj);
  const auto normalize_plane = [](const glm::vec4 &p) {
    const auto n = glm::vec3(p);
    const float inv_len = 1.0f / glm::length(n);
    return glm::vec4(n * inv_len, p.w * inv_len);
  };
  const glm::vec4 frustum_x =
      normalize_plane(projection_transpose[0] + projection_transpose[3]);  // x + w < 0
  const glm::vec4 frustum_y =
      normalize_plane(projection_transpose[1] + projection_transpose[3]);  // y + w < 0
  cull_data.frustum[0] = frustum_x.x;
  cull_data.frustum[1] = frustum_x.z;
  cull_data.frustum[2] = frustum_y.y;
  cull_data.frustum[3] = frustum_y.z;
  cull_data.view = uniforms.view;
  cull_data.meshlet_frustum_cull = meshlet_frustum_cull_;
  cull_data.camera_pos = render_args.camera_pos;
  cull_data.z_near = k_z_near;
  cull_data.z_far = k_z_far;
  return cull_data;
}

void RendererMetal::flush_pending_texture_uploads() {
  if (!pending_texture_uploads_.empty()) {
    MTL::CommandBuffer *buf = main_cmd_queue_->commandBuffer();
    MTL::BlitCommandEncoder *blit_enc = buf->blitCommandEncoder();
    for (auto &upload : pending_texture_uploads_) {
      const auto src_img_size = static_cast<size_t>(upload.bytes_per_row) * upload.dims.y;
      MTL::Buffer *upload_buf =
          raw_device_->newBuffer(src_img_size, MTL::ResourceStorageModeShared);
      memcpy(upload_buf->contents(), upload.data, src_img_size);
      const MTL::Origin origin = MTL::Origin::Make(0, 0, 0);
      const MTL::Size img_size = MTL::Size::Make(upload.dims.x, upload.dims.y, upload.dims.z);
      auto *tex = reinterpret_cast<MetalTexture *>(device_->get_tex(upload.tex));
      auto *mtl_tex = tex->texture();
      blit_enc->copyFromBuffer(upload_buf, 0, upload.bytes_per_row, 0, img_size, mtl_tex, 0, 0,
                               origin);
      blit_enc->generateMipmaps(mtl_tex);
      global_arg_enc_->setTexture(mtl_tex, tex->gpu_slot());
      // mtl_tex->retain();
      all_textures_[tex->gpu_slot()] = mtl_tex;
    }
    blit_enc->endEncoding();
    pending_texture_uploads_.clear();
    buf->commit();
    buf->waitUntilCompleted();
  }
}

void RendererMetal::recreate_render_target_textures() {
  auto dims = window_->get_window_size();
  depth_tex_ = device_->create_tex_h(rhi::TextureDesc{.format = rhi::TextureFormat::D32float,
                                                      .storage_mode = rhi::StorageMode::GPUOnly,
                                                      .usage = rhi::TextureUsageRenderTarget,
                                                      .dims = glm::uvec3{dims, 1}});
  // hzb_tex_ = device_->create_tex_h(rhi::TextureDesc{
  //     .format = rhi::TextureFormat::D32float,
  //     .storage_mode = rhi::StorageMode::GPUOnly,
  //     .usage = static_cast<rhi::TextureUsage>(rhi::TextureUsageShaderWrite |
  //                                             rhi::TextureUsageShaderRead),
  //     .dims = glm::uvec3{dims, 1},
  //     .mip_levels = get_mip_levels(dims.x, dims.y),
  // });
}

NS::SharedPtr<MTL::Buffer> RendererMetal::create_buffer(size_t size, void *data,
                                                        MTL::ResourceOptions options) {
  NS::SharedPtr<MTL::Buffer> buf = NS::TransferPtr(raw_device_->newBuffer(size, options));
  memcpy(buf->contents(), data, size);
  return buf;
}

const char *draw_batch_type_to_string(DrawBatchType type) {
  switch (type) {
    case DrawBatchType::Static:
      return "Static";
  }
}

DrawBatch::DrawBatch(DrawBatchType type, rhi::Device &device, const CreateInfo &cinfo)
    : vertex_buf(device,
                 rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                                 .size = cinfo.initial_vertex_capacity * sizeof(DefaultVertex)},
                 sizeof(DefaultVertex)),
      index_buf(device,
                rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                                .size = cinfo.initial_index_capacity * sizeof(rhi::DefaultIndexT)},
                sizeof(rhi::DefaultIndexT)),
      meshlet_buf(device,
                  rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                                  .size = cinfo.initial_meshlet_capacity * sizeof(Meshlet)},
                  sizeof(Meshlet)),
      mesh_buf(device,
               rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                               .size = cinfo.initial_mesh_capacity * sizeof(MeshData)},
               sizeof(MeshData)),
      meshlet_triangles_buf(
          device,
          rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                          .size = cinfo.initial_meshlet_triangle_capacity * sizeof(uint8_t)},
          sizeof(uint8_t)),
      meshlet_vertices_buf(
          device,
          rhi::BufferDesc{.storage_mode = rhi::StorageMode::CPUAndGPU,
                          .size = cinfo.initial_meshlet_vertex_capacity * sizeof(uint32_t)},
          sizeof(uint32_t)),
      type(type) {}

DrawBatch::Stats DrawBatch::get_stats() const {
  return {
      .vertex_count = vertex_buf.allocated_element_count(),
      .index_count = index_buf.allocated_element_count(),
      .meshlet_count = meshlet_buf.allocated_element_count(),
      .meshlet_triangle_count = meshlet_triangles_buf.allocated_element_count(),
      .meshlet_vertex_count = meshlet_vertices_buf.allocated_element_count(),
  };
}

DrawBatch::Alloc RendererMetal::upload_geometry([[maybe_unused]] DrawBatchType type,
                                                const std::vector<DefaultVertex> &vertices,
                                                const std::vector<rhi::DefaultIndexT> &indices,
                                                const MeshletProcessResult &meshlets,
                                                std::span<Mesh> meshes) {
  auto &draw_batch = static_draw_batch_;
  ALWAYS_ASSERT(!vertices.empty());
  ALWAYS_ASSERT(!meshlets.meshlet_datas.empty());

  bool resized{};
  const auto vertex_alloc = draw_batch->vertex_buf.allocate(vertices.size(), resized);
  memcpy((reinterpret_cast<DefaultVertex *>(draw_batch->vertex_buf.get_buffer()->contents()) +
          vertex_alloc.offset),
         vertices.data(), vertices.size() * sizeof(DefaultVertex));

  OffsetAllocator::Allocation index_alloc{};
  if (!indices.empty()) {
    index_alloc = draw_batch->index_buf.allocate(indices.size(), resized);
    memcpy((reinterpret_cast<rhi::DefaultIndexT *>(draw_batch->index_buf.get_buffer()->contents()) +
            index_alloc.offset),
           indices.data(), indices.size() * sizeof(rhi::DefaultIndexT));
  }

  const auto meshlet_alloc = draw_batch->meshlet_buf.allocate(meshlets.tot_meshlet_count, resized);
  const auto meshlet_vertices_alloc =
      draw_batch->meshlet_vertices_buf.allocate(meshlets.tot_meshlet_verts_count, resized);
  const auto meshlet_triangles_alloc =
      draw_batch->meshlet_triangles_buf.allocate(meshlets.tot_meshlet_tri_count, resized);
  const auto mesh_alloc = draw_batch->mesh_buf.allocate(meshlets.meshlet_datas.size(), resized);

  size_t meshlet_offset{};
  size_t meshlet_triangles_offset{};
  size_t meshlet_vertices_offset{};
  size_t mesh_i{};
  for (const auto &meshlet_data : meshlets.meshlet_datas) {
    memcpy((reinterpret_cast<Meshlet *>(draw_batch->meshlet_buf.get_buffer()->contents()) +
            meshlet_alloc.offset + meshlet_offset),
           meshlet_data.meshlets.data(), meshlet_data.meshlets.size() * sizeof(Meshlet));
    meshlet_offset += meshlet_data.meshlets.size();

    memcpy(
        (reinterpret_cast<uint32_t *>(draw_batch->meshlet_vertices_buf.get_buffer()->contents()) +
         meshlet_vertices_alloc.offset + meshlet_vertices_offset),
        meshlet_data.meshlet_vertices.data(),
        meshlet_data.meshlet_vertices.size() * sizeof(uint32_t));
    meshlet_vertices_offset += meshlet_data.meshlet_vertices.size();

    memcpy(
        (reinterpret_cast<uint8_t *>(draw_batch->meshlet_triangles_buf.get_buffer()->contents()) +
         meshlet_triangles_alloc.offset + meshlet_triangles_offset),
        meshlet_data.meshlet_triangles.data(),
        meshlet_data.meshlet_triangles.size() * sizeof(uint8_t));
    meshlet_triangles_offset += meshlet_data.meshlet_triangles.size();

    MeshData d{
        .meshlet_base = meshlet_data.meshlet_base,
        .meshlet_count = static_cast<uint32_t>(meshlet_data.meshlets.size()),
        .meshlet_vertices_offset = meshlet_data.meshlet_vertices_offset,
        .meshlet_triangles_offset = meshlet_data.meshlet_triangles_offset,
        .index_count = meshes[mesh_i].index_count,
        .index_offset = meshes[mesh_i].index_offset,
        .vertex_base = meshes[mesh_i].vertex_offset_bytes,
        .vertex_count = meshes[mesh_i].vertex_count,
        .center = meshes[mesh_i].center,
        .radius = meshes[mesh_i].radius,
    };
    memcpy((reinterpret_cast<MeshData *>(draw_batch->mesh_buf.get_buffer()->contents()) + mesh_i +
            mesh_alloc.offset),
           &d, sizeof(MeshData));
    mesh_i++;
  }

  return DrawBatch::Alloc{.vertex_alloc = vertex_alloc,
                          .index_alloc = index_alloc,
                          .meshlet_alloc = meshlet_alloc,
                          .mesh_alloc = mesh_alloc,
                          .meshlet_triangles_alloc = meshlet_triangles_alloc,
                          .meshlet_vertices_alloc = meshlet_vertices_alloc};
}

void RendererMetal::load_pipelines() {
  auto load_pipeline = [this](auto *desc) {
    NS::Error *err{};
    auto *result =
        raw_device_->newRenderPipelineState(desc, MTL::PipelineOptionNone, nullptr, &err);
    if (err) {
      util::mtl::print_err(err);
      exit(1);
    }

    desc->release();
    return result;
  };

  {
    MTL::MeshRenderPipelineDescriptor *pipeline_desc =
        MTL::MeshRenderPipelineDescriptor::alloc()->init();
    pipeline_desc->setMeshFunction(get_function("basic1_mesh_main"));
    pipeline_desc->setObjectFunction(get_function("basic1_object_main"));
    // TODO: consolidate fragment shader PLEASEEEEEEEE!
    pipeline_desc->setFragmentFunction(get_function("basic1_fragment_main"));
    pipeline_desc->setLabel(util::mtl::string("basic mesh pipeline"));
    pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    pipeline_desc->setSupportIndirectCommandBuffers(true);
    mesh_pso_ = load_pipeline(pipeline_desc);
  }
  {
    MTL::RenderPipelineDescriptor *pipeline_desc = MTL::RenderPipelineDescriptor::alloc()->init();
    pipeline_desc->setVertexFunction(get_function("vertexMain"));
    pipeline_desc->setFragmentFunction(get_function("fragmentMain"));
    pipeline_desc->setLabel(util::mtl::string("basic vert pipeline"));
    pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    pipeline_desc->setSupportIndirectCommandBuffers(true);
    vertex_pso_ = load_pipeline(pipeline_desc);
  }

  {
    auto create_compute_pipeline = [this](const char *name) {
      MTL::ComputePipelineDescriptor *pipeline_desc =
          MTL::ComputePipelineDescriptor::alloc()->init();
      pipeline_desc->setComputeFunction(get_function(name));
      pipeline_desc->setLabel(util::mtl::string(name));
      NS::Error *err{};
      auto *pso = raw_device_->newComputePipelineState(pipeline_desc, MTL::PipelineOptionNone,
                                                       nullptr, &err);
      if (err) {
        util::mtl::print_err(err);
        exit(1);
      }
      pipeline_desc->release();
      return pso;
    };

    dispatch_mesh_pso_ = create_compute_pipeline("dispatch_mesh_main");
    dispatch_vertex_pso_ = create_compute_pipeline("dispatch_vertex_main");
  }
}
MTL::Function *RendererMetal::get_function(const char *name) {
  auto it = shader_funcs_.find(name);
  if (it == shader_funcs_.end()) {
    LERROR("shader function not found: {}", name);
    return nullptr;
  }
  return it->second;
}
