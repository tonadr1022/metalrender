#include "RendererMetal.hpp"

#define IMGUI_IMPL_METAL_CPP
#include <imgui_impl_metal.h>

#include <Metal/Metal.hpp>
#include <glm/mat4x4.hpp>
#include <span>
#include <tracy/Tracy.hpp>

#include "ModelLoader.hpp"
#include "QuartzCore/CAMetalDrawable.hpp"
#include "QuartzCore/CAMetalLayer.hpp"
#include "WindowApple.hpp"
#include "core/BitUtil.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "dispatch_mesh_shared.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "imgui_impl_glfw.h"
#include "mesh_shared.h"
#include "metal/BackedGPUAllocator.hpp"
#include "metal/MetalDevice.hpp"
#include "metal/MetalUtil.hpp"
#include "shader_global_uniforms.h"
namespace {

enum class RenderMode : uint32_t { Default, Normals, NormalMap };

}  // namespace

InstanceDataMgr::InstanceDataMgr(size_t initial_element_cap, MTL::Device *device)
    : allocator_(initial_element_cap), device_(device) {
  allocate_buffers(initial_element_cap);
}

OffsetAllocator::Allocation InstanceDataMgr::allocate(size_t element_count) {
  const OffsetAllocator::Allocation alloc = allocator_.allocate(element_count);
  if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
    auto old_capacity = allocator_.capacity();
    auto new_capacity = old_capacity * 2;
    assert(allocator_.grow(allocator_.capacity()));
    // TODO: copy contents
    auto old_instance_data_buf = instance_data_buf_;
    auto old_model_matrix_buf = model_matrix_buf_;
    allocate_buffers(new_capacity);
    memcpy(instance_data_buf_->contents(), old_instance_data_buf->contents(),
           old_capacity * sizeof(InstanceData));
    memcpy(model_matrix_buf_->contents(), old_model_matrix_buf->contents(),
           old_capacity * sizeof(glm::mat4));
    return allocate(element_count);
  }
  max_seen_size_ = std::max<uint32_t>(max_seen_size_, alloc.offset + element_count);
  return alloc;
}

void InstanceDataMgr::allocate_buffers(size_t element_count) {
  instance_data_buf_ = NS::TransferPtr(
      device_->newBuffer(sizeof(InstanceData) * element_count, MTL::ResourceStorageModeShared));
  model_matrix_buf_ = NS::TransferPtr(
      device_->newBuffer(sizeof(glm::mat4) * element_count, MTL::ResourceStorageModeShared));
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

  init_imgui();

  MTL::IndirectCommandBufferDescriptor *cmd_buf_desc =
      MTL::IndirectCommandBufferDescriptor::alloc()->init();
  cmd_buf_desc->setCommandTypes(MTL::IndirectCommandTypeDrawMeshThreadgroups);
  cmd_buf_desc->setInheritBuffers(false);
  cmd_buf_desc->setInheritPipelineState(true);
  cmd_buf_desc->setMaxFragmentBufferBindCount(2);
  cmd_buf_desc->setMaxMeshBufferBindCount(8);
  cmd_buf_desc->setMaxObjectBufferBindCount(2);
  ind_cmd_buf_ = NS::TransferPtr(
      raw_device_->newIndirectCommandBuffer(cmd_buf_desc, 1024, MTL::ResourceStorageModePrivate));
  cmd_buf_desc->release();

  instance_data_mgr_.emplace(128, raw_device_);
  static_draw_batch_.emplace(DrawBatchType::Static, *device_,
                             DrawBatch::CreateInfo{
                                 .initial_vertex_capacity = 100'000,
                                 .initial_index_capacity = 100'000,
                                 .initial_meshlet_capacity = 100'000,
                                 .initial_mesh_capacity = 10'000,
                                 .initial_meshlet_triangle_capacity = 100'000,
                                 .initial_meshlet_vertex_capacity = 100'000,
                             });

  // TODO: rethink
  all_textures_.resize(k_max_textures);

  recreate_render_target_textures();

  load_shaders();
  {
    // main pipeline
    // MTL::RenderPipelineDescriptor *pipeline_desc =
    // MTL::RenderPipelineDescriptor::alloc()->init();
    // pipeline_desc->setVertexFunction(forward_pass_shader_.vert_func);
    // pipeline_desc->setFragmentFunction(forward_pass_shader_.frag_func);
    // pipeline_desc->setLabel(util::mtl::string("basic"));
    // pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    // pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    // NS::Error *err{};
    // main_pso_ = raw_device_->newRenderPipelineState(pipeline_desc, &err);
    // if (err) {
    //   util::mtl::print_err(err);
    //   exit(1);
    // }
    //
    // pipeline_desc->release();
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

  materials_buf_.emplace(*device_, rhi::BufferDesc{rhi::StorageMode::CPUAndGPU, k_max_textures},
                         sizeof(Material));
  all_buffers_buf_ =
      raw_device_->newBuffer(sizeof(uint64_t) * k_max_buffers, MTL::ResourceStorageModeShared);

  global_arg_enc_->setBuffer(all_buffers_buf_, 0, k_max_textures);
  auto material_buf_i = 1;
  *(reinterpret_cast<uint64_t *>(all_buffers_buf_->contents()) + material_buf_i) =
      reinterpret_cast<MetalBuffer *>(materials_buf_->get_buffer())->buffer()->gpuAddress();

  MTL::Function *const funcs[] = {forward_pass_shader_.frag_func, forward_mesh_shader_.frag_func};
  for (const auto &func : funcs) {
    MTL::ArgumentEncoder *frag_enc = func->newArgumentEncoder(0);
    frag_enc->setArgumentBuffer(scene_arg_buffer_.get(), 0);
    frag_enc->release();
  }

  {
    // per frame uniform buffer
    for (size_t i = 0; i < frames_in_flight_; i++) {
      per_frame_datas_.emplace_back(
          PerFrameData{.uniform_buf = NS::TransferPtr(raw_device_->newBuffer(
                           util::align_256(sizeof(Uniforms)), MTL::ResourceStorageModeShared))});
    }
  }
}

void RendererMetal::shutdown() { shutdown_imgui(); }

void RendererMetal::render(const RenderArgs &render_args) {
  ZoneScoped;
  auto *frame_ar_pool = NS::AutoreleasePool::alloc()->init();

  flush_pending_texture_uploads();

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

  // TODO: this is awful
  auto use_scene_arg_buffer_resources = [this](auto enc) {
    for (const MTL::Texture *tex : all_textures_) {
      if (tex) {
        enc->useResource(tex, MTL::ResourceUsageSample);
      }
    }
    enc->useResource(all_buffers_buf_, MTL::ResourceUsageRead);
    enc->useResource(get_mtl_buf(*materials_buf_), MTL::ResourceUsageRead);
  };

  set_global_uniform_data(render_args);

  {
    MTL::BlitCommandEncoder *reset_blit_enc = buf->blitCommandEncoder();
    reset_blit_enc->setLabel(util::mtl::string("Reset ICB Blit Encoder"));
    reset_blit_enc->resetCommandsInBuffer(ind_cmd_buf_.get(),
                                          NS::Range::Make(0, all_model_data_.max_objects));
    reset_blit_enc->endEncoding();
  }
  {
    MTL::ComputePassDescriptor *compute_pass_descriptor =
        MTL::ComputePassDescriptor::computePassDescriptor();
    compute_pass_descriptor->setDispatchType(MTL::DispatchTypeSerial);
    MTL::ComputeCommandEncoder *compute_enc = buf->computeCommandEncoder(compute_pass_descriptor);
    compute_enc->setComputePipelineState(dispatch_mesh_pso_);
    compute_enc->setBuffer(dispatch_mesh_icb_container_buf_.get(), 0, 0);
    compute_enc->setBuffer(dispatch_mesh_encode_arg_buf_.get(), 0, 1);
    compute_enc->useResource(ind_cmd_buf_.get(), MTL::ResourceUsageWrite);
    compute_enc->useResource(instance_data_mgr_->instance_data_buf(), MTL::ResourceUsageRead);
    // TODO: remove
    compute_enc->useResource(get_mtl_buf(*materials_buf_), MTL::ResourceUsageRead);
    DispatchMeshParams params{.tot_meshes = all_model_data_.max_objects};
    compute_enc->setBytes(&params, sizeof(DispatchMeshParams), 2);
    // TODO: this is awfulllllllllllllll.
    compute_enc->dispatchThreads(MTL::Size::Make(all_model_data_.max_objects, 1, 1),
                                 MTL::Size::Make(32, 1, 1));
    compute_enc->endEncoding();
  }
  {
    MTL::BlitCommandEncoder *blit_enc = buf->blitCommandEncoder();
    blit_enc->setLabel(util::mtl::string("Optimize ICB Blit Encoder"));
    blit_enc->optimizeIndirectCommandBuffer(ind_cmd_buf_.get(),
                                            NS::Range::Make(0, all_model_data_.max_objects));
    blit_enc->endEncoding();
  }

  MTL::RenderPassDescriptor *forward_render_pass_desc =
      MTL::RenderPassDescriptor::renderPassDescriptor();
  MTL::RenderPassDepthAttachmentDescriptor *desc =
      MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
  desc->setTexture(depth_tex_);
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
    MTL::DepthStencilDescriptor *depth_stencil_desc = MTL::DepthStencilDescriptor::alloc()->init();
    depth_stencil_desc->setDepthCompareFunction(MTL::CompareFunctionLess);
    depth_stencil_desc->setDepthWriteEnabled(true);
    enc->setDepthStencilState(raw_device_->newDepthStencilState(depth_stencil_desc));
  }

  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeBack);

  {
    ZoneScopedN("encode draw cmds");
    enc->setRenderPipelineState(mesh_pso_);
    const MTL::Resource *const resources[] = {
        instance_data_mgr_->instance_data_buf(),
        instance_data_mgr_->model_matrix_buf(),
        get_mtl_buf(static_draw_batch_->vertex_buf),
        get_mtl_buf(static_draw_batch_->meshlet_buf),
        get_mtl_buf(static_draw_batch_->mesh_buf),
        get_mtl_buf(static_draw_batch_->meshlet_vertices_buf),
        get_mtl_buf(static_draw_batch_->meshlet_triangles_buf),
        get_curr_frame_data().uniform_buf.get(),
        scene_arg_buffer_.get(),
        all_buffers_buf_,
        get_mtl_buf(*materials_buf_),
    };
    enc->useResources(resources, ARRAY_SIZE(resources), MTL::ResourceUsageRead);
    use_scene_arg_buffer_resources(enc);
    enc->executeCommandsInBuffer(ind_cmd_buf_.get(),
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

bool RendererMetal::load_model(const std::filesystem::path &path, const glm::mat4 &root_transform,
                               ModelInstance &model, ModelGPUHandle &out_handle) {
  ModelLoadResult result;
  if (!model::load_model(path, *this, root_transform, model, result)) {
    return false;
  }

  pending_texture_uploads_.append_range(result.texture_uploads);

  auto draw_batch_alloc = upload_geometry(DrawBatchType::Static, result.vertices, result.indices,
                                          result.meshlet_process_result);

  std::vector<InstanceData> base_instance_datas;
  base_instance_datas.reserve(model.tot_mesh_nodes);

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

  {  // move this bs
    {
      MTL::ArgumentEncoder *arg_enc = dispatch_mesh_shader_.compute_func->newArgumentEncoder(0);
      dispatch_mesh_icb_container_buf_ = NS::TransferPtr(
          raw_device_->newBuffer(arg_enc->encodedLength(), MTL::ResourceStorageModeShared));
      arg_enc->setArgumentBuffer(dispatch_mesh_icb_container_buf_.get(), 0);
      arg_enc->setIndirectCommandBuffer(ind_cmd_buf_.get(), 0);
      arg_enc->setBuffer(instance_data_mgr_->instance_data_buf(), 0, 1);
      arg_enc->release();
    }
    {
      MTL::ArgumentEncoder *arg_enc = dispatch_mesh_shader_.compute_func->newArgumentEncoder(1);
      // TODO: frames in flight
      dispatch_mesh_encode_arg_buf_ = NS::TransferPtr(
          raw_device_->newBuffer(arg_enc->encodedLength() * 2, MTL::ResourceStorageModeShared));
      arg_enc->setArgumentBuffer(dispatch_mesh_encode_arg_buf_.get(), 0);

      // TODO: ONLY WHEN THE BUFFER RESIZES PLEASE
      arg_enc->setBuffer(((MetalBuffer *)static_draw_batch_->vertex_buf.get_buffer())->buffer(), 0,
                         EncodeMeshDrawArgs_MainVertexBuf);
      arg_enc->setBuffer(
          reinterpret_cast<MetalBuffer *>(static_draw_batch_->meshlet_buf.get_buffer())->buffer(),
          0, EncodeMeshDrawArgs_MeshletBuf);
      arg_enc->setBuffer(get_mtl_buf(static_draw_batch_->mesh_buf), 0,
                         EncodeMeshDrawArgs_MeshDataBuf);
      arg_enc->setBuffer(instance_data_mgr_->model_matrix_buf(), 0,
                         EncodeMeshDrawArgs_InstanceModelMatrixBuf);
      arg_enc->setBuffer(instance_data_mgr_->instance_data_buf(), 0,
                         EncodeMeshDrawArgs_InstanceDataBuf);
      arg_enc->setBuffer(
          reinterpret_cast<MetalBuffer *>(static_draw_batch_->meshlet_vertices_buf.get_buffer())
              ->buffer(),
          0, EncodeMeshDrawArgs_MeshletVerticesBuf);
      arg_enc->setBuffer(
          reinterpret_cast<MetalBuffer *>(static_draw_batch_->meshlet_triangles_buf.get_buffer())
              ->buffer(),
          0, EncodeMeshDrawArgs_MeshletTrianglesBuf);
      arg_enc->setBuffer(get_curr_frame_data().uniform_buf.get(), 0,
                         EncodeMeshDrawArgs_MainUniformBuf);
      arg_enc->setBuffer(scene_arg_buffer_.get(), 0, EncodeMeshDrawArgs_SceneArgBuf);
      arg_enc->release();
    }
  }

  out_handle = model_gpu_resource_pool_.alloc(
      ModelGPUResources{.material_alloc = material_alloc,
                        .static_draw_batch_alloc = draw_batch_alloc,
                        .base_instance_datas = std::move(base_instance_datas)});
  return true;
}

ModelInstanceGPUHandle RendererMetal::add_model_instance(const ModelInstance &model,
                                                         ModelGPUHandle model_gpu_handle) {
  auto &model_instance_datas = model_gpu_resource_pool_.get(model_gpu_handle)->base_instance_datas;
  std::vector<glm::mat4> instance_model_matrices;
  instance_model_matrices.reserve(model_instance_datas.size());
  std::vector<InstanceData> instance_datas = {model_instance_datas.begin(),
                                              model_instance_datas.end()};
  for (const auto &instance_data : instance_datas) {
    instance_model_matrices.push_back(model.global_transforms[instance_data.instance_id]);
  }
  const OffsetAllocator::Allocation instance_data_gpu_alloc =
      instance_data_mgr_->allocate(model_instance_datas.size());
  all_model_data_.max_objects = instance_data_mgr_->max_seen_size();
  for (auto &data : instance_datas) {
    data.instance_id += instance_data_gpu_alloc.offset;
  }
  memcpy(reinterpret_cast<InstanceData *>(instance_data_mgr_->instance_data_buf()->contents()) +
             instance_data_gpu_alloc.offset,
         instance_datas.data(), instance_datas.size() * sizeof(InstanceData));

  memcpy(reinterpret_cast<glm::mat4 *>(instance_data_mgr_->model_matrix_buf()->contents()) +
             instance_data_gpu_alloc.offset,
         instance_model_matrices.data(), instance_model_matrices.size() * sizeof(glm::mat4));
  return model_instance_gpu_resource_pool_.alloc(
      ModelInstanceGPUResources{.instance_data_gpu_alloc = instance_data_gpu_alloc});
}

void RendererMetal::free_model(ModelGPUHandle model) {
  auto *gpu_resources = model_gpu_resource_pool_.get(model);
  if (!gpu_resources) {
    return;
  }
  // TODO: free things!

  // instance_data_mgr_->free(gpu_resources->instance_data_gpu_slot);
  model_gpu_resource_pool_.destroy(model);
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
}

TextureWithIdx RendererMetal::load_material_image(const rhi::TextureDesc &desc) {
  return {.tex = device_->create_texture(desc), .idx = texture_index_allocator_.alloc_idx()};
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

void RendererMetal::set_global_uniform_data(const RenderArgs &render_args) {
  ZoneScoped;
  // Uniform data
  auto &uniform_buf = get_curr_frame_data().uniform_buf;
  auto *uniform_data = static_cast<Uniforms *>(uniform_buf->contents());
  auto window_dims = window_->get_window_size();
  const float aspect = (window_dims.x != 0) ? float(window_dims.x) / float(window_dims.y) : 1.0f;
  uniform_data->vp =
      glm::perspective(glm::radians(70.f), aspect, 0.001f, 10000.f) * render_args.view_mat;
  uniform_data->render_mode = (uint32_t)RenderMode::Default;
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

void RendererMetal::recreate_render_target_textures() {
  auto dims = window_->get_window_size();
  if (depth_tex_) depth_tex_->release();
  depth_tex_ = device_->create_texture(rhi::TextureDesc{.format = rhi::TextureFormat::D32float,
                                                        .storage_mode = rhi::StorageMode::GPUOnly,
                                                        .usage = rhi::TextureUsageRenderTarget,
                                                        .dims = glm::uvec3{dims, 1}});
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
                                  .size = cinfo.initial_meshlet_capacity * sizeof(meshopt_Meshlet)},
                  sizeof(meshopt_Meshlet)),
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

DrawBatchAlloc RendererMetal::upload_geometry([[maybe_unused]] DrawBatchType type,
                                              const std::vector<DefaultVertex> &vertices,
                                              const std::vector<rhi::DefaultIndexT> &indices,
                                              const MeshletProcessResult &meshlets) {
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
    memcpy((reinterpret_cast<meshopt_Meshlet *>(draw_batch->meshlet_buf.get_buffer()->contents()) +
            meshlet_alloc.offset + meshlet_offset),
           meshlet_data.meshlets.data(), meshlet_data.meshlets.size() * sizeof(meshopt_Meshlet));
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
    };
    memcpy((reinterpret_cast<MeshData *>(draw_batch->mesh_buf.get_buffer()->contents()) + mesh_i +
            mesh_alloc.offset),
           &d, sizeof(MeshData));
    mesh_i++;
  }

  return {.vertex_alloc = vertex_alloc,
          .index_alloc = index_alloc,
          .meshlet_alloc = meshlet_alloc,
          .mesh_alloc = mesh_alloc,
          .meshlet_triangles_alloc = meshlet_triangles_alloc,
          .meshlet_vertices_alloc = meshlet_vertices_alloc};
}
