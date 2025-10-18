#include "RendererMetal.hpp"

#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLArgumentEncoder.hpp>
#include <Metal/MTLCommandBuffer.hpp>
#include <Metal/MTLCommandEncoder.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLRenderCommandEncoder.hpp>
#include <Metal/MTLRenderPass.hpp>
#include <Metal/MTLSampler.hpp>
#include <Metal/MTLTexture.hpp>

#include "gfx/Config.hpp"
#include "gfx/GFXTypes.hpp"
#include "gfx/metal/MetalBuffer.hpp"
#include "gfx/metal/MetalTexture.hpp"
#include "imgui.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
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
#include "dispatch_shader_shared.h"
#include "dispatch_vertex_shared.h"
#include "imgui_impl_glfw.h"
#include "mesh_shared.h"
#include "metal/BackedGPUAllocator.hpp"
#include "metal/MetalDevice.hpp"
#include "metal/MetalUtil.hpp"
#include "shader_global_uniforms.h"
namespace {

uint32_t get_mip_levels(uint32_t width, uint32_t height) {
  return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

glm::mat4 perspective_proj(float fov_y, float aspect, float z_near) {
  // clang-format off
	float f = 1.0f / tanf(fov_y / 2.0f);
	return {
	    f / aspect, 0.0f, 0.0f, 0.0f,
	    0.0f, f, 0.0f, 0.0f,
	    0.0f, 0.0f, 0.0f, -1.0f,
	    0.0f, 0.0f, z_near, 0.0f};
  // clang-format on
}

uint32_t prev_pow2(uint32_t val) {
  uint32_t v = 1;
  while (v * 2 < val) {
    v *= 2;
  }
  return v;
}

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
    MTL::IndirectCommandBufferDescriptor *desc =
        MTL::IndirectCommandBufferDescriptor::alloc()->init();
    desc->setInheritBuffers(false);
    desc->setInheritPipelineState(true);
    if (k_use_mesh_shader) {
      desc->setCommandTypes(MTL::IndirectCommandTypeDrawMeshThreadgroups);
      desc->setMaxFragmentBufferBindCount(3);
      desc->setMaxMeshBufferBindCount(8);
      desc->setMaxObjectBufferBindCount(6);
    } else {
      desc->setCommandTypes(MTL::IndirectCommandTypeDrawIndexed);
      desc->setMaxFragmentBufferBindCount(2);
      desc->setMaxVertexBufferBindCount(3);
    }
    ASSERT(raw_device_ != nullptr);
    main_icb_ = NS::TransferPtr(raw_device_->newIndirectCommandBuffer(
        desc, icb_element_count_, MTL::ResourceStorageModePrivate));
    desc->release();
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

  {
    default_white_tex_ =
        device_->create_tex_h(rhi::TextureDesc{.format = rhi::TextureFormat::R8G8B8A8Unorm,
                                               .storage_mode = rhi::StorageMode::GPUOnly,
                                               .dims = glm::uvec3{1, 1, 1},
                                               .mip_levels = 1,
                                               .array_length = 1,
                                               .alloc_gpu_slot = true});
    ALWAYS_ASSERT(device_->get_tex(default_white_tex_)->gpu_slot() == 0);
    auto *data = reinterpret_cast<uint64_t *>(malloc(sizeof(uint64_t)));
    *data = 0xFFFFFFFF;
    std::unique_ptr<void, void (*)(void *)> data_ptr{data, free};
    pending_texture_uploads_.push_back(TextureUpload{.data = std::move(data_ptr),
                                                     .tex = std::move(default_white_tex_),
                                                     .dims = glm::uvec3{1, 1, 1},
                                                     .bytes_per_row = 4});
  }

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
  meshlet_vis_buf_.emplace(*device_, rhi::BufferDesc{.size = 100'0000}, sizeof(uint32_t));

  // TODO: rethink
  all_textures_.resize(k_max_textures);

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
    dispatch_mesh_encode_arg_buf_ =
        NS::TransferPtr(raw_device_->newBuffer(dispatch_mesh_encode_arg_enc_->encodedLength(), 0));
    dispatch_mesh_encode_arg_enc_->setArgumentBuffer(dispatch_mesh_encode_arg_buf_.get(), 0);
  }
  {
    dispatch_vertex_encode_arg_enc_ = get_function("dispatch_vertex_main")->newArgumentEncoder(1);
    dispatch_vertex_encode_arg_buf_ = NS::TransferPtr(
        raw_device_->newBuffer(dispatch_vertex_encode_arg_enc_->encodedLength(), 0));
    dispatch_vertex_encode_arg_enc_->setArgumentBuffer(dispatch_vertex_encode_arg_buf_.get(), 0);
  }

  {
    main_object_arg_enc_ = get_function("basic1_object_main_late_pass")->newArgumentEncoder(2);
    main_object_arg_buf_ =
        NS::TransferPtr(raw_device_->newBuffer(main_object_arg_enc_->encodedLength(), 0));
    main_object_arg_enc_->setArgumentBuffer(main_object_arg_buf_.get(), 0);
  }

  recreate_render_target_textures();

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
  gpu_frame_allocator_->switch_to_next_buffer();
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
  encode_regular_frame(render_args, buf, drawable);

  if (debug_render_view_ == DebugRenderView::DepthPyramidTex) {
    encode_debug_depth_pyramid_view(buf, drawable);
  }
  if (render_args.draw_imgui) {
    MTL::RenderPassDescriptor *forward_render_pass_desc =
        MTL::RenderPassDescriptor::renderPassDescriptor();
    auto *color0 = forward_render_pass_desc->colorAttachments()->object(0);
    color0->setTexture(drawable->texture());
    color0->setLoadAction(MTL::LoadActionLoad);
    color0->setStoreAction(MTL::StoreActionStore);
    MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(forward_render_pass_desc);

    ImGui_ImplMetal_NewFrame(forward_render_pass_desc);
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    render_imgui();

    ImGui::Render();

    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), buf, enc);
    enc->endEncoding();
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
    uint32_t curr_meshlet_vis_buf_offset = 0;
    for (size_t node = 0; node < model.nodes.size(); node++) {
      auto mesh_id = model.mesh_ids[node];
      if (model.mesh_ids[node] == Mesh::k_invalid_mesh_id) {
        continue;
      }
      base_instance_datas.emplace_back(
          InstanceData{.instance_id = instance_copy_idx,
                       .mat_id = result.meshes[mesh_id].material_id,
                       .mesh_id = draw_batch_alloc.mesh_alloc.offset + mesh_id,
                       .meshlet_vis_base = curr_meshlet_vis_buf_offset});
      instance_id_to_node.push_back(node);
      instance_copy_idx++;
      curr_meshlet_vis_buf_offset +=
          result.meshlet_process_result.meshlet_datas[mesh_id].meshlets.size();
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
                        .instance_id_to_node = instance_id_to_node,
                        .tot_meshlet_count = result.meshlet_process_result.tot_meshlet_count});
  return true;
}

ModelInstanceGPUHandle RendererMetal::add_model_instance(const ModelInstance &model,
                                                         ModelGPUHandle model_gpu_handle) {
  ZoneScoped;
  auto *model_resources = model_gpu_resource_pool_.get(model_gpu_handle);
  ALWAYS_ASSERT(model_resources);
  auto &model_instance_datas = model_resources->base_instance_datas;
  auto &instance_id_to_node = model_resources->instance_id_to_node;
  std::vector<TRS> instance_transforms;
  instance_transforms.reserve(model_instance_datas.size());
  std::vector<InstanceData> instance_datas = {model_instance_datas.begin(),
                                              model_instance_datas.end()};
  ASSERT(instance_datas.size() == instance_id_to_node.size());

  const OffsetAllocator::Allocation instance_data_gpu_alloc =
      instance_data_mgr_->allocate(model_instance_datas.size());
  all_model_data_.max_objects = instance_data_mgr_->max_seen_size();

  OffsetAllocator::Allocation meshlet_vis_buf_alloc{};
  if (k_use_mesh_shader) {
    bool resized{};
    meshlet_vis_buf_alloc = meshlet_vis_buf_->allocate(model_resources->tot_meshlet_count, resized);
    meshlet_vis_buf_dirty_ = true;
  }

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
    instance_datas[i].meshlet_vis_base += meshlet_vis_buf_alloc.offset;
  }

  memcpy(reinterpret_cast<InstanceData *>(instance_data_mgr_->instance_data_buf()->contents()) +
             instance_data_gpu_alloc.offset,
         instance_datas.data(), instance_datas.size() * sizeof(InstanceData));

  main_icb_container_arg_enc_->setIndirectCommandBuffer(instance_data_mgr_->icb(), 0);

  if (k_use_mesh_shader) {  // move this bs
    // on main vertex buffer resize at beginning of frame
    dispatch_mesh_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->vertex_buf), 0,
                                             EncodeMeshDrawArgs_MainVertexBuf);
    dispatch_mesh_encode_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->meshlet_buf), 0,
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

    main_object_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->mesh_buf), 0,
                                    MainObjectArgs_MeshDataBuf);
    main_object_arg_enc_->setBuffer(get_mtl_buf(static_draw_batch_->meshlet_buf), 0,
                                    MainObjectArgs_MeshletBuf);
    ASSERT(meshlet_vis_buf_->get_buffer());
    main_object_arg_enc_->setBuffer(get_mtl_buf(*meshlet_vis_buf_), 0,
                                    MainObjectArgs_MeshletVisBuf);

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
      ModelInstanceGPUResources{.instance_data_gpu_alloc = instance_data_gpu_alloc,
                                .meshlet_vis_buf_alloc = meshlet_vis_buf_alloc});
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
  const char *items[] = {debug_render_view_to_str(DebugRenderView::None),
                         debug_render_view_to_str(DebugRenderView::DepthPyramidTex)};
  int curr = (int)debug_render_view_;
  if (ImGui::Combo("Debug View Mode", &curr, items, ARRAY_SIZE(items))) {
    debug_render_view_ = (DebugRenderView)curr;
  }
  if (debug_render_view_ == DebugRenderView::DepthPyramidTex) {
    ImGui::SliderInt("Depth Pyramid Mip Level", &debug_depth_pyramid_mip_level_, 0,
                     device_->get_tex(depth_pyramid_tex_)->desc().mip_levels - 1);
  }
  ImGui::Checkbox("Occlusion culling", &meshlet_occlusion_culling_enabled_);
  ImGui::Checkbox("Pause Culling", &culling_paused_);
  ImGui::Checkbox("Object Frustum Culling", &object_frust_cull_enabled_);
}

void RendererMetal::load_shaders() {
  NS::Error *err{};
  default_shader_lib_ = raw_device_->newLibrary(
      util::mtl::string(resource_dir_ / "shader_out" / "default.metallib"), &err);

  if (err) {
    util::mtl::print_err(err);
    return;
  }

  {
    auto add_func = [this](const char *name) {
      shader_funcs_.emplace(name, default_shader_lib_->newFunction(util::mtl::string(name)));
    };
    add_func("vertexMain");
    add_func("fragmentMain");
    add_func("basic1_mesh_main");
    add_func("basic1_fragment_main");
    add_func("dispatch_mesh_main");
    add_func("dispatch_vertex_main");
    add_func("depth_reduce_main");
  }
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
  uniform_data.view = render_args.view_mat;

  float far = k_z_far;
  float near = k_z_near;
  if (k_reverse_z) {
    std::swap(far, near);
  }
  // uniform_data.proj = glm::perspectiveFovZO(glm::radians(70.0f), (float)window_dims.x,
  //                                           (float)window_dims.y, near, far);
  float aspect = (float)window_dims.x / (float)window_dims.y;
  uniform_data.proj = perspective_proj(glm::radians(70.f), aspect, k_z_near);
  uniform_data.vp = uniform_data.proj * uniform_data.view;
  uniform_data.render_mode = (uint32_t)RenderMode::Normals;
  return uniform_data;
}

CullData RendererMetal::set_cpu_cull_data(const Uniforms &uniforms, const RenderArgs &render_args) {
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
  cull_data.proj = uniforms.proj;
  cull_data.meshlet_frustum_cull = meshlet_frustum_cull_;
  cull_data.camera_pos = render_args.camera_pos;
  cull_data.z_near = k_z_near;
  cull_data.z_far = k_z_far;
  cull_data.p00 = uniforms.proj[0][0];
  cull_data.p11 = uniforms.proj[1][1];

  auto pyramid_dims = device_->get_tex(depth_pyramid_tex_)->desc().dims;
  cull_data.pyramid_width = pyramid_dims.x;
  cull_data.pyramid_height = pyramid_dims.y;
  cull_data.pyramid_mip_count = device_->get_tex(depth_pyramid_tex_)->desc().mip_levels;
  cull_data.meshlet_occlusion_culling_enabled = meshlet_occlusion_culling_enabled_;
  cull_data.paused = culling_paused_;

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
      memcpy(upload_buf->contents(), upload.data.get(), src_img_size);
      const MTL::Origin origin = MTL::Origin::Make(0, 0, 0);
      const MTL::Size img_size = MTL::Size::Make(upload.dims.x, upload.dims.y, upload.dims.z);
      auto *tex = reinterpret_cast<MetalTexture *>(device_->get_tex(upload.tex));
      auto *mtl_tex = tex->texture();
      blit_enc->copyFromBuffer(upload_buf, 0, upload.bytes_per_row, 0, img_size, mtl_tex, 0, 0,
                               origin);
      if (tex->desc().mip_levels > 1) {
        blit_enc->generateMipmaps(mtl_tex);
      }
      global_arg_enc_->setTexture(mtl_tex, tex->gpu_slot());
      // mtl_tex->retain();
      all_textures_[tex->gpu_slot()] = std::move(upload.tex);
    }
    blit_enc->endEncoding();
    pending_texture_uploads_.clear();
    buf->commit();
    buf->waitUntilCompleted();
  }
}

void RendererMetal::recreate_render_target_textures() {
  auto dims = window_->get_window_size();
  depth_tex_ = device_->create_tex_h(rhi::TextureDesc{
      .format = rhi::TextureFormat::D32float,
      .storage_mode = rhi::StorageMode::GPUOnly,
      .usage = static_cast<rhi::TextureUsage>(rhi::TextureUsageRenderTarget |
                                              rhi::TextureUsageShaderRead),
      .dims = glm::uvec3{dims, 1},
  });
  recreate_depth_pyramid_tex();
}

void RendererMetal::recreate_depth_pyramid_tex() {
  auto dims = window_->get_window_size();
  auto depth_pyramid_dims = glm::uvec3{prev_pow2(dims.x), prev_pow2(dims.y), 1};
  depth_pyramid_tex_ = device_->create_tex_h(rhi::TextureDesc{
      .format = rhi::TextureFormat::R32float,
      .storage_mode = rhi::StorageMode::GPUOnly,
      .usage = static_cast<rhi::TextureUsage>(rhi::TextureUsageShaderWrite |
                                              rhi::TextureUsageRenderTarget |
                                              rhi::TextureUsageShaderRead),
      .dims = depth_pyramid_dims,
      .mip_levels = get_mip_levels(depth_pyramid_dims.x, depth_pyramid_dims.y),
  });

  auto *tex = reinterpret_cast<MetalTexture *>(device_->get_tex(depth_pyramid_tex_));
  for (size_t i = 0; i < tex->desc().mip_levels; i++) {
    depth_pyramid_tex_views_[i] = NS::TransferPtr(
        tex->texture()->newTextureView(MTL::PixelFormatR32Float, MTL::TextureType2D,
                                       NS::Range::Make(i, 1), NS::Range::Make(0, 1)));
  }

  ALWAYS_ASSERT(main_object_arg_enc_);
  main_object_arg_enc_->setTexture(tex->texture(), MainObjectArgs_DepthPyramidTex);
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
  auto load_pipeline = [this](const auto &desc) {
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

  for (int is_late_pass = 0; is_late_pass < 2; is_late_pass++) {
    FuncConst object_func_consts[] = {{"is_late_pass", &is_late_pass, FuncConst::Type::Bool}};
    MTL::MeshRenderPipelineDescriptor *pipeline_desc =
        MTL::MeshRenderPipelineDescriptor::alloc()->init();
    pipeline_desc->setMeshFunction(get_function("basic1_mesh_main"));
    pipeline_desc->setObjectFunction(create_function(
        "basic1_object_main",
        is_late_pass ? "basic1_object_main_late_pass" : "basic1_object_main_early_pass",
        object_func_consts));
    pipeline_desc->setFragmentFunction(get_function("basic1_fragment_main"));
    pipeline_desc->setLabel(util::mtl::string("basic mesh pipeline"));
    pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    pipeline_desc->setSupportIndirectCommandBuffers(true);
    if (is_late_pass) {
      mesh_late_pso_ = load_pipeline(pipeline_desc);
    } else {
      mesh_pso_ = load_pipeline(pipeline_desc);
    }
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
    MTL::RenderPipelineDescriptor *pipeline_desc = MTL::RenderPipelineDescriptor::alloc()->init();
    pipeline_desc->setVertexFunction(get_function("full_screen_tex_vertex_main"));
    pipeline_desc->setFragmentFunction(get_function("full_screen_tex_frag_main"));
    pipeline_desc->setLabel(util::mtl::string("full screen texture pipeline"));
    pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    full_screen_tex_pso_ = load_pipeline(pipeline_desc);
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
    depth_reduce_pso_ = create_compute_pipeline("depth_reduce_main");
  }
}
MTL::Function *RendererMetal::get_function(const char *name, bool load) {
  auto it = shader_funcs_.find(name);
  if (it == shader_funcs_.end()) {
    if (load) {
      MTL::Function *func = default_shader_lib_->newFunction(util::mtl::string(name));
      ALWAYS_ASSERT(func != nullptr);
      shader_funcs_.emplace(name, func);
      return func;
    }

    LERROR("shader function not found: {}", name);
    return nullptr;
  }
  return it->second;
}

MTL::Function *RendererMetal::create_function(const std::string &name,
                                              const std::string &specialized_name,
                                              std::span<FuncConst> consts) {
  MTL::FunctionDescriptor *desc = MTL::FunctionDescriptor::alloc()->init();
  desc->setName(util::mtl::string(name));

  if (!consts.empty()) {
    MTL::FunctionConstantValues *vals = MTL::FunctionConstantValues::alloc()->init();
    for (auto &c : consts) {
      MTL::DataType t{};
      switch (c.type) {
        case FuncConst::Type::Bool:
          t = MTL::DataTypeBool;
          break;
        default:
          break;
      }

      vals->setConstantValue(c.val, t, util::mtl::string(c.name));
    }
    desc->setConstantValues(vals);
  }

  if (!specialized_name.empty()) {
    desc->setSpecializedName(util::mtl::string(specialized_name));
  }
  NS::Error *err{};
  MTL::Function *func = default_shader_lib_->newFunction(desc, &err);
  if (err) {
    util::mtl::print_err(err);
    ALWAYS_ASSERT(0 && "failed to create function");
    return nullptr;
  }
  shader_funcs_.emplace(!specialized_name.empty() ? specialized_name : name, func);
  return func;
}

void RendererMetal::encode_regular_frame(const RenderArgs &render_args, MTL::CommandBuffer *buf,
                                         const CA::MetalDrawable *drawable) {
  ZoneScoped;

  const Uniforms cpu_uniforms = set_cpu_global_uniform_data(render_args);
  gpu_uniform_buf_->fill(cpu_uniforms);
  cull_data_buf_->fill(set_cpu_cull_data(cpu_uniforms, render_args));

  {
    MTL::BlitCommandEncoder *reset_blit_enc = buf->blitCommandEncoder();
    reset_blit_enc->setLabel(util::mtl::string("Reset ICB Blit Encoder"));
    reset_blit_enc->resetCommandsInBuffer(instance_data_mgr_->icb(),
                                          NS::Range::Make(0, all_model_data_.max_objects));

    if (k_use_mesh_shader && meshlet_vis_buf_dirty_) {
      meshlet_vis_buf_dirty_ = false;
      reset_blit_enc->fillBuffer(get_mtl_buf(*meshlet_vis_buf_),
                                 NS::Range::Make(0, meshlet_vis_buf_->get_buffer()->size()), 0);
    }

    reset_blit_enc->endEncoding();
  }
  auto begin_compute_enc = [&buf](bool concurrent) {
    MTL::ComputePassDescriptor *compute_pass_descriptor =
        MTL::ComputePassDescriptor::computePassDescriptor();
    compute_pass_descriptor->setDispatchType(concurrent ? MTL::DispatchTypeConcurrent
                                                        : MTL::DispatchTypeSerial);
    return buf->computeCommandEncoder(compute_pass_descriptor);
  };

  {  // fill ICB

    auto *compute_enc = begin_compute_enc(false);
    if (k_use_mesh_shader) {
      compute_enc->setComputePipelineState(dispatch_mesh_pso_);
      compute_enc->setBuffer(dispatch_mesh_encode_arg_buf_.get(), 0, 1);
    } else {
      compute_enc->setComputePipelineState(dispatch_vertex_pso_);
      compute_enc->setBuffer(dispatch_vertex_encode_arg_buf_.get(), 0, 1);
    }

    compute_enc->setBuffer(get_mtl_buf(main_icb_container_buf_), 0, 0);
    compute_enc->setBuffer(reinterpret_cast<MetalBuffer *>(gpu_uniform_buf_->get_buf())->buffer(),
                           gpu_uniform_buf_->get_offset_bytes(), 3);
    compute_enc->setBuffer(reinterpret_cast<MetalBuffer *>(cull_data_buf_->get_buf())->buffer(),
                           cull_data_buf_->get_offset_bytes(), 4);
    ASSERT(main_object_arg_buf_);
    compute_enc->setBuffer(main_object_arg_buf_.get(), 0, 5);
    compute_enc->useResource(get_mtl_buf(*meshlet_vis_buf_),
                             MTL::ResourceUsageRead | MTL::ResourceUsageWrite);

    compute_enc->useResource(instance_data_mgr_->icb(), MTL::ResourceUsageWrite);
    compute_enc->useResource(instance_data_mgr_->instance_data_buf(), MTL::ResourceUsageRead);
    DispatchMeshParams params{.tot_meshes = all_model_data_.max_objects,
                              .frustum_cull = !culling_paused_ && object_frust_cull_enabled_};
    compute_enc->setBytes(&params, sizeof(DispatchMeshParams), 2);
    compute_enc->dispatchThreads(MTL::Size::Make(all_model_data_.max_objects, 1, 1),
                                 MTL::Size::Make(32, 1, 1));
    compute_enc->endEncoding();
  }

  {
    MTL::BlitCommandEncoder *blit_enc = buf->blitCommandEncoder();
    blit_enc->setLabel(util::mtl::string("Optimize ICB Blit Encoder"));
    blit_enc->optimizeIndirectCommandBuffer(instance_data_mgr_->icb(),
                                            NS::Range::Make(0, all_model_data_.max_objects));
    blit_enc->endEncoding();
  }

  auto set_depth_stencil_state = [this](MTL::RenderCommandEncoder *enc) {
    // TODO: don't recreate this is cringe.
    MTL::DepthStencilDescriptor *depth_stencil_desc = MTL::DepthStencilDescriptor::alloc()->init();
    depth_stencil_desc->setDepthCompareFunction(k_reverse_z ? MTL::CompareFunctionGreater
                                                            : MTL::CompareFunctionLess);
    depth_stencil_desc->setDepthWriteEnabled(true);
    enc->setDepthStencilState(raw_device_->newDepthStencilState(depth_stencil_desc));
  };

  auto set_cull_mode_wind_order = [](MTL::RenderCommandEncoder *enc) {
    enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
    enc->setCullMode(MTL::CullModeBack);
  };

  auto use_mesh_shader_resources = [this](MTL::RenderCommandEncoder *enc) {
    const MTL::Resource *const resources[] = {
        instance_data_mgr_->instance_data_buf(),
        get_mtl_buf(static_draw_batch_->vertex_buf),
        get_mtl_buf(static_draw_batch_->meshlet_buf),
        get_mtl_buf(static_draw_batch_->mesh_buf),
        get_mtl_buf(static_draw_batch_->meshlet_vertices_buf),
        get_mtl_buf(static_draw_batch_->meshlet_triangles_buf),
        scene_arg_buffer_.get(),
    };
    enc->useResources(resources, ARRAY_SIZE(resources), MTL::ResourceUsageRead);
  };

  auto use_fragment_resources = [this](MTL::RenderCommandEncoder *enc) {
    for (const auto &handle : all_textures_) {
      if (auto *tex = device_->get_tex(handle)) {
        enc->useResource(reinterpret_cast<MetalTexture *>(tex)->texture(),
                         MTL::ResourceUsageSample);
      }
    }
    enc->useResource(get_mtl_buf(*materials_buf_), MTL::ResourceUsageRead);
  };

  {
    ZoneScopedN("encode forward pass cmds");
    MTL::RenderPassDescriptor *forward_render_pass_desc =
        MTL::RenderPassDescriptor::renderPassDescriptor();
    MTL::RenderPassDepthAttachmentDescriptor *desc =
        MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
    desc->setTexture(get_mtl_tex(depth_tex_));
    desc->setClearDepth(k_reverse_z ? 0.0 : 1.0);
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

    set_depth_stencil_state(enc);
    set_cull_mode_wind_order(enc);

    use_fragment_resources(enc);
    if (k_use_mesh_shader) {
      enc->setRenderPipelineState(mesh_pso_);
      use_mesh_shader_resources(enc);
      enc->useResource(get_mtl_buf(*meshlet_vis_buf_),
                       MTL::ResourceUsageRead | MTL::ResourceUsageWrite);
    } else {
      enc->setRenderPipelineState(vertex_pso_);
      const MTL::Resource *const resources[] = {
          instance_data_mgr_->instance_data_buf(),
          get_mtl_buf(static_draw_batch_->vertex_buf),
          get_mtl_buf(static_draw_batch_->index_buf),
          get_mtl_buf(static_draw_batch_->mesh_buf),
          scene_arg_buffer_.get(),
          get_mtl_buf(*materials_buf_),
      };
      enc->useResources(resources, ARRAY_SIZE(resources), MTL::ResourceUsageRead);
    }

    enc->executeCommandsInBuffer(instance_data_mgr_->icb(),
                                 NS::Range::Make(0, all_model_data_.max_objects));

    enc->endEncoding();
  }

  {
    auto *enc = begin_compute_enc(false);
    enc->setComputePipelineState(depth_reduce_pso_);
    auto *main_tex = reinterpret_cast<MetalTexture *>(device_->get_tex(depth_pyramid_tex_));
    auto dp_dims = main_tex->desc().dims;
    auto *depth_tex = reinterpret_cast<MetalTexture *>(device_->get_tex(depth_tex_));
    auto depth_dims = depth_tex->desc().dims;
    for (size_t i = 0; i < main_tex->desc().mip_levels; i++) {
      depth_pyramid_tex_views_[i] = NS::TransferPtr(
          main_tex->texture()->newTextureView(MTL::PixelFormatR32Float, MTL::TextureType2D,
                                              NS::Range::Make(i, 1), NS::Range::Make(0, 1)));
      MTL::Texture *input_view =
          i == 0 ? reinterpret_cast<MetalTexture *>(device_->get_tex(depth_tex_))->texture()
                 : depth_pyramid_tex_views_[i - 1].get();
      MTL::Texture *output_view = depth_pyramid_tex_views_[i].get();
      enc->useResource(input_view, MTL::ResourceUsageSample);
      enc->useResource(output_view, MTL::ResourceUsageWrite);

      enc->setTexture(input_view, 0);
      enc->setTexture(output_view, 1);

      glm::uvec2 in_dims = (i == 0) ? depth_dims
                                    : glm::uvec2{std::max(1u, dp_dims.x >> (i - 1)),
                                                 std::max(1u, dp_dims.y >> (i - 1))};
      struct {
        glm::uvec2 in_dims;
        glm::uvec2 out_dims;
      } args{
          .in_dims = in_dims,
          .out_dims = {dp_dims.x >> i, dp_dims.y >> i},
      };

      constexpr int n = 16;
      enc->setBytes(&args, sizeof(args), 0);
      enc->dispatchThreadgroups(
          MTL::Size::Make((args.out_dims.x + n - 1) / n, (args.out_dims.y + n - 1) / n, 1),
          MTL::Size::Make(n, n, 1));
    }
    enc->endEncoding();
  }

  if (!culling_paused_ && k_use_mesh_shader) {  // late pass
    MTL::RenderPassDescriptor *pass_desc = MTL::RenderPassDescriptor::renderPassDescriptor();
    MTL::RenderPassDepthAttachmentDescriptor *desc =
        MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
    desc->setTexture(get_mtl_tex(depth_tex_));
    desc->setLoadAction(MTL::LoadActionLoad);
    desc->setStoreAction(MTL::StoreActionStore);
    pass_desc->setDepthAttachment(desc);
    {
      auto *color0 = pass_desc->colorAttachments()->object(0);
      color0->setTexture(drawable->texture());
      color0->setLoadAction(MTL::LoadActionLoad);
      color0->setStoreAction(MTL::StoreActionStore);
    }
    MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(pass_desc);
    enc->setRenderPipelineState(mesh_late_pso_);
    enc->useResource(get_mtl_tex(depth_pyramid_tex_), MTL::ResourceUsageSample);

    set_depth_stencil_state(enc);
    set_cull_mode_wind_order(enc);
    use_fragment_resources(enc);
    use_mesh_shader_resources(enc);
    enc->useResource(get_mtl_buf(*meshlet_vis_buf_),
                     MTL::ResourceUsageRead | MTL::ResourceUsageWrite);

    enc->executeCommandsInBuffer(instance_data_mgr_->icb(),
                                 NS::Range::Make(0, all_model_data_.max_objects));
    enc->endEncoding();
  }
}

void RendererMetal::encode_debug_depth_pyramid_view(MTL::CommandBuffer *buf,
                                                    const CA::MetalDrawable *drawable) {
  MTL::RenderPassDescriptor *desc = MTL::RenderPassDescriptor::alloc()->init();
  auto *color0 = desc->colorAttachments()->object(0);
  color0->setTexture(drawable->texture());
  color0->setLoadAction(MTL::LoadActionDontCare);
  color0->setStoreAction(MTL::StoreActionStore);
  MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(desc);
  enc->setRenderPipelineState(full_screen_tex_pso_);

  struct {
    int mip_level;
    glm::vec4 mult;
  } args{0, glm::vec4{glm::vec3{1000}, 1.0}};
  enc->setFragmentBytes(&args, sizeof(args), 0);

  // enc->setFragmentTexture(get_mtl_tex(depth_tex_), 0);
  enc->setFragmentTexture(depth_pyramid_tex_views_[debug_depth_pyramid_mip_level_].get(), 0);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, 0, 3, 1);

  enc->endEncoding();
}
