#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <Metal/Metal.hpp>
#include <expected>
#include <filesystem>
#include <fstream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <memory>
#include <print>
#include <sstream>

#include "QuartzCore/CAMetalLayer.hpp"
#include "WindowApple.hpp"
#include "core/Logger.hpp"
#include "gfx/Device.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

constexpr size_t align_256(size_t n) { return (n + 255) & ~size_t(255); }
struct Vertex {
  glm::vec3 pos;
  glm::vec4 color;
};

namespace mtl_util {

void print_err(NS::Error *err) {
  assert(err);
  std::println("{}", err->localizedDescription()->cString(NS::ASCIIStringEncoding));
}

}  // namespace mtl_util
namespace {

struct ModelLoadResult {
  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
};

std::expected<ModelLoadResult, std::string> load_model(const std::filesystem::path &path) {
  cgltf_options gltf_load_opts{};
  cgltf_data *raw_gltf{};
  cgltf_result gltf_res = cgltf_parse_file(&gltf_load_opts, path.c_str(), &raw_gltf);
  std::unique_ptr<cgltf_data, void (*)(cgltf_data *)> gltf(raw_gltf, cgltf_free);

  if (gltf_res != cgltf_result_success) {
    if (gltf_res == cgltf_result_file_not_found) {
      return std::unexpected(std::format("Failed to load GLTF. File not found {}", path.c_str()));
    } else {
      return std::unexpected(std::format("Failed to laod GLTF with error {} for file {}",
                                         static_cast<int>(gltf_res), path.c_str()));
    }
  }

  gltf_res = cgltf_load_buffers(&gltf_load_opts, gltf.get(), path.c_str());

  if (gltf_res != cgltf_result_success) {
    return std::unexpected(
        std::format("Failed to load GLTF buffers for gltf path {}", path.c_str()));
  }

  ModelLoadResult result;

  auto &vertices = result.vertices;
  auto &indices = result.indices;
  for (size_t mesh_i = 0; mesh_i < gltf->meshes_count; mesh_i++) {
    auto &mesh = gltf->meshes[mesh_i];
    for (size_t prim_i = 0; prim_i < mesh.primitives_count; prim_i++) {
      auto &primitive = mesh.primitives[prim_i];
      size_t base_vertex = vertices.size();
      vertices.resize(vertices.size() + gltf->accessors[primitive.attributes[0].index].count);
      if (primitive.indices) {
        indices.reserve(indices.size() + primitive.indices->count);
        for (size_t i = 0; i < primitive.indices->count; i++) {
          indices.push_back(cgltf_accessor_read_index(primitive.indices, i));
        }
      }
      for (size_t attr_i = 0; attr_i < primitive.attributes_count; attr_i++) {
        auto &attr = primitive.attributes[attr_i];
        cgltf_accessor *accessor = attr.data;
        if (attr.type == cgltf_attribute_type_position) {
          for (size_t i = 0; i < accessor->count; i++) {
            float pos[3] = {0, 0, 0};
            cgltf_accessor_read_float(accessor, i, pos, 3);
            if (base_vertex + i >= vertices.size()) {
              assert(base_vertex + i < vertices.size());
            }
            vertices[base_vertex + i].pos = glm::vec3{pos[0], pos[1], pos[2]};
            std::println("{}", glm::to_string(vertices[base_vertex + i].pos));
          }
        } else if (attr.type == cgltf_attribute_type_texcoord) {
          LINFO("has texcoord");
        }
      }
    }
  }

  return result;
}

std::string load_file_to_string(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::println("File not found or cannot be opened at path {}", path.string());
    return "";
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

std::filesystem::path get_resource_dir() {
  std::filesystem::path curr_path = std::filesystem::current_path();
  while (curr_path.has_parent_path()) {
    if (std::filesystem::exists(curr_path / "resources")) {
      return curr_path / "resources";
    }
    curr_path = curr_path.parent_path();
  }
  return "";
}

NS::String *ns_string(const char *v) { return NS::String::string(v, NS::ASCIIStringEncoding); }

struct Uniforms {
  glm::mat4 model;
  glm::mat4 vp;
};

}  // namespace

struct App {
  App() { window->init(device.get()); }
  App(const App &) = delete;
  App(App &&) = delete;
  App &operator=(const App &) = delete;
  App &operator=(App &&) = delete;
  ~App() = default;
  std::filesystem::path resource_dir_ = get_resource_dir();
  std::filesystem::path shader_dir_ = resource_dir_ / "shaders";

  void run() {
    auto model_load_res = load_model(resource_dir_ / "models" / "Cube/glTF/Cube.gltf");
    if (!model_load_res) {
      exit(1);
    }
    auto &model = model_load_res.value();
    size_t i = 0;
    glm::vec3 colors[] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}};
    for (auto &v : model.vertices) {
      v.color = glm::vec4{colors[i++ % 3], 1.0};
    }

    // vertices = {{{-0.5, -0.5, 0}, {1, 0, 0, 1}},
    //             {{0.5, 0.5, 0}, {0, 0, 1, 1}},
    //             {{0, 0.5, 0}, {0, 1, 0, 1}}};
    size_t vertices_size = model.vertices.size() * sizeof(Vertex);
    size_t indices_size = model.indices.size() * sizeof(uint16_t);
    raw_device_ = (MTL::Device *)device->get_native_device();
    auto vertex_buffer =
        NS::TransferPtr(raw_device_->newBuffer(vertices_size, MTL::ResourceStorageModeShared));
    memcpy(vertex_buffer->contents(), model.vertices.data(), vertices_size);
    auto index_buffer =
        NS::TransferPtr(raw_device_->newBuffer(indices_size, MTL::ResourceStorageModeShared));
    memcpy(index_buffer->contents(), model.indices.data(), indices_size);
    size_t frames_in_flight = 2;
    NS::SharedPtr<MTL::Buffer> uniform_buffer = NS::TransferPtr(raw_device_->newBuffer(
        frames_in_flight * align_256(sizeof(Uniforms)), MTL::ResourceStorageModeShared));

    MTL::CommandQueue *queue = raw_device_->newCommandQueue();
    queue->autorelease();

    MTL::RenderPipelineState *main_pipeline_state{};
    {
      std::string src = load_file_to_string(shader_dir_ / "basic1.metal");
      NS::String *path = NS::String::string(src.c_str(), NS::ASCIIStringEncoding);
      NS::Error *err{};
      MTL::Library *shader_lib = raw_device_->newLibrary(path, nullptr, &err);
      if (err != nullptr) {
        mtl_util::print_err(err);
        exit(1);
      }
      auto *names = shader_lib->functionNames();
      auto *vertex_fn = shader_lib->newFunction(ns_string("vertexMain"));
      auto *frag_fn = shader_lib->newFunction(ns_string("fragmentMain"));
      MTL::RenderPipelineDescriptor *pipeline_desc = MTL::RenderPipelineDescriptor::alloc()->init();
      pipeline_desc->setVertexFunction(vertex_fn);
      pipeline_desc->setFragmentFunction(frag_fn);
      pipeline_desc->setLabel(ns_string("basic"));
      pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
      pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

      main_pipeline_state = raw_device_->newRenderPipelineState(pipeline_desc, &err);

      pipeline_desc->release();
      vertex_fn->release();
      frag_fn->release();
      shader_lib->release();
    }

    MTL::TextureDescriptor *texture_descriptor = MTL::TextureDescriptor::alloc()->init();
    texture_descriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
    int w, h;
    glfwGetFramebufferSize(window->get_handle(), &w, &h);
    texture_descriptor->setHeight(h);
    texture_descriptor->setWidth(w);
    texture_descriptor->setDepth(1);
    texture_descriptor->setStorageMode(MTL::StorageModePrivate);
    texture_descriptor->setMipmapLevelCount(1);
    texture_descriptor->setSampleCount(1);
    texture_descriptor->setUsage(MTL::TextureUsageRenderTarget);
    MTL::Texture *depth_tex = raw_device_->newTexture(texture_descriptor);
    texture_descriptor->release();

    size_t curr_frame = 0;
    while (!should_quit()) {
      auto *frame_ar_pool = NS::AutoreleasePool::alloc()->init();
      window->poll_events();
      CA::MetalDrawable *drawable = window->metal_layer_->nextDrawable();
      if (!drawable) {
        frame_ar_pool->release();
        continue;
      }

      MTL::RenderPassDescriptor *render_pass_desc =
          MTL::RenderPassDescriptor::renderPassDescriptor();
      auto *color0 = render_pass_desc->colorAttachments()->object(0);
      MTL::RenderPassDepthAttachmentDescriptor *desc =
          MTL::RenderPassDepthAttachmentDescriptor::alloc()->init();
      desc->setTexture(depth_tex);
      // desc->setClearDepth(0);
      desc->setClearDepth(1.0);
      desc->setLoadAction(MTL::LoadActionClear);
      desc->setStoreAction(MTL::StoreActionStore);
      render_pass_desc->setDepthAttachment(desc);
      color0->setTexture(drawable->texture());
      color0->setLoadAction(MTL::LoadActionClear);
      color0->setClearColor(MTL::ClearColor::Make(0.5, 0.1, 0.12, 1.0));
      color0->setStoreAction(MTL::StoreActionStore);

      MTL::CommandBuffer *buf = queue->commandBuffer();
      MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(render_pass_desc);

      {
        MTL::DepthStencilDescriptor *depth_stencil_desc =
            MTL::DepthStencilDescriptor::alloc()->init();
        depth_stencil_desc->setDepthCompareFunction(MTL::CompareFunctionLess);
        depth_stencil_desc->setDepthWriteEnabled(true);
        enc->setDepthStencilState(raw_device_->newDepthStencilState(depth_stencil_desc));
      }
      enc->setRenderPipelineState(main_pipeline_state);
      enc->setVertexBuffer(vertex_buffer.get(), 0, 0);
      size_t uniforms_offset = (curr_frame % frames_in_flight) * align_256(sizeof(Uniforms));
      auto *uniform_data = reinterpret_cast<Uniforms *>(
          reinterpret_cast<uint8_t *>(uniform_buffer->contents()) + uniforms_offset);
      uniform_data->model = glm::mat4{1};
      int w, h;
      glfwGetFramebufferSize(window->get_handle(), &w, &h);
      float aspect = (h != 0) ? float(w) / float(h) : 1.0f;
      uniform_data->vp = glm::perspective(glm::radians(70.f), aspect, 0.1f, 1000.f) *
                         glm::lookAt(glm::vec3{2, 2, 2}, glm::vec3{0}, glm::vec3{0, 1, 0});

      enc->setVertexBuffer(uniform_buffer.get(), uniforms_offset, 1);
      enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
      // enc->setCullMode(MTL::CullModeBack);
      enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, indices_size / sizeof(uint16_t),
                                 MTL::IndexTypeUInt16, index_buffer.get(), 0, 1);
      enc->endEncoding();
      buf->presentDrawable(drawable);
      buf->commit();

      frame_ar_pool->release();
      curr_frame++;
    }

    window->shutdown();
    device->shutdown();
  }

 private:
  [[nodiscard]] bool should_quit() const { return window->should_close(); }

  MTL::Device *raw_device_{};
  std::unique_ptr<MetalDevice> device = create_metal_device();
  std::unique_ptr<WindowApple> window = create_apple_window();
};

int main() {
  auto *ar_pool = NS::AutoreleasePool::alloc()->init();
  {
    App app;
    app.run();
  }
  ar_pool->release();
}
