#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>

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
  glm::vec4 pos;
  glm::vec2 uv;
  glm::vec3 normal;
};

struct Material {
  uint64_t albedo_tex;
};

namespace mtl_util {

void print_err(NS::Error *err) {
  assert(err);
  std::println("{}", err->localizedDescription()->cString(NS::ASCIIStringEncoding));
}

}  // namespace mtl_util
namespace {

class IndexAllocator {
 public:
  explicit IndexAllocator(uint32_t capacity) : capacity_(capacity) {
    for (int64_t i = capacity - 1; i >= 0; i--) {
      free_indices.push_back(i);
    }
  }

  uint32_t allocate_idx() {
    if (free_indices.empty()) {
      LERROR("increasing capacity on index allocator, debug this");
      return capacity_++;
    }
    auto ret = free_indices.back();
    free_indices.pop_back();
    return ret;
  }

  void release_idx(uint32_t idx) { free_indices.push_back(idx); }

 private:
  size_t capacity_;
  std::vector<uint32_t> free_indices;
};

struct TextureUpload {
  void *data;
  MTL::Texture *tex;
  glm::uvec3 dims;
  uint32_t bytes_per_row;
};

struct ModelLoadResult {
  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
  std::vector<TextureUpload> texture_uploads;
};

enum class TextureFormat { Undefined, R8G8B8A8Srgb, R8G8B8A8Unorm };

MTL::PixelFormat convert_format(TextureFormat format) {
  switch (format) {
    case TextureFormat::R8G8B8A8Srgb:
      return MTL::PixelFormatRGBA8Unorm_sRGB;
    case TextureFormat::R8G8B8A8Unorm:
      return MTL::PixelFormatRGBA8Unorm;
    default:
      assert(0 && "unhandled texture format");
      return MTL::PixelFormatInvalid;
  }
  return MTL::PixelFormatInvalid;
}

enum class StorageMode { GPUOnly, CPUAndGPU, CPUOnly };

MTL::StorageMode convert_storage_mode(StorageMode mode) {
  switch (mode) {
    case StorageMode::CPUAndGPU:
    case StorageMode::CPUOnly:
      return MTL::StorageModeShared;
    case StorageMode::GPUOnly:
      return MTL::StorageModePrivate;
    default:
      assert(0 && "invalid storage mode");
      return MTL::StorageModePrivate;
  }
  assert(0 && "unreachable");
  return MTL::StorageModePrivate;
}

struct TextureDesc {
  TextureFormat format{TextureFormat::Undefined};
  StorageMode storage_mode{StorageMode::GPUOnly};
  glm::uvec3 dims{1};
  uint32_t mip_levels{1};
  uint32_t array_length{1};
  void *data{};
};

MTL::Texture *load_image(const TextureDesc &desc, MTL::Device *device) {
  MTL::TextureDescriptor *texture_desc = MTL::TextureDescriptor::alloc()->init();
  texture_desc->setWidth(desc.dims.x);
  texture_desc->setHeight(desc.dims.y);
  texture_desc->setDepth(desc.dims.z);
  texture_desc->setPixelFormat(convert_format(desc.format));
  texture_desc->setStorageMode(convert_storage_mode(desc.storage_mode));
  texture_desc->setMipmapLevelCount(desc.mip_levels);
  texture_desc->setArrayLength(desc.array_length);
  texture_desc->setAllowGPUOptimizedContents(true);
  texture_desc->setUsage(MTL::TextureUsageShaderRead);
  MTL::Texture *tex = device->newTexture(texture_desc);
  texture_desc->release();
  return tex;
}

std::expected<ModelLoadResult, std::string> load_model(const std::filesystem::path &path,
                                                       MTL::Device *device) {
  cgltf_options gltf_load_opts{};
  cgltf_data *raw_gltf{};
  cgltf_result gltf_res = cgltf_parse_file(&gltf_load_opts, path.c_str(), &raw_gltf);
  std::unique_ptr<cgltf_data, void (*)(cgltf_data *)> gltf(raw_gltf, cgltf_free);
  std::filesystem::path directory_path = path.parent_path();

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

  auto &texture_uploads = result.texture_uploads;
  texture_uploads.reserve(gltf->images_count);

  for (size_t i = 0; i < gltf->images_count; i++) {
    const cgltf_image &img = gltf->images[i];
    if (!img.buffer_view) {
      int w, h, comp;
      std::filesystem::path full_img_path = directory_path / img.uri;
      uint8_t *data = stbi_load(full_img_path.c_str(), &w, &h, &comp, 4);
      uint32_t mip_levels = std::floor(std::log2(std::max(w, h))) + 1;
      TextureDesc desc{.format = TextureFormat::R8G8B8A8Unorm,
                       .storage_mode = StorageMode::GPUOnly,
                       .dims = glm::uvec3{w, h, 1},
                       .mip_levels = mip_levels,
                       .array_length = 1,
                       .data = data};
      MTL::Texture *mtl_img = load_image(desc, device);
      texture_uploads.emplace_back(TextureUpload{
          .data = data, .tex = mtl_img, .dims = desc.dims, .bytes_per_row = desc.dims.x * 4});
      if (!data) {
        assert(0);
      }
    } else {
      assert(0 && "need to handle yet");
    }
  }

  auto &vertices = result.vertices;
  auto &indices = result.indices;
  for (size_t mesh_i = 0; mesh_i < gltf->meshes_count; mesh_i++) {
    const auto &mesh = gltf->meshes[mesh_i];
    for (size_t prim_i = 0; prim_i < mesh.primitives_count; prim_i++) {
      const auto &primitive = mesh.primitives[prim_i];
      size_t base_vertex = vertices.size();
      vertices.resize(vertices.size() + gltf->accessors[primitive.attributes[0].index].count);
      if (primitive.indices) {
        indices.reserve(indices.size() + primitive.indices->count);
        for (size_t i = 0; i < primitive.indices->count; i++) {
          indices.push_back(cgltf_accessor_read_index(primitive.indices, i));
        }
      }
      for (size_t attr_i = 0; attr_i < primitive.attributes_count; attr_i++) {
        if (primitive.material) {
          const cgltf_material &material = *primitive.material;
          const cgltf_texture_view &base_color_tex =
              material.pbr_metallic_roughness.base_color_texture;
        }
        const auto &attr = primitive.attributes[attr_i];
        cgltf_accessor *accessor = attr.data;
        if (attr.type == cgltf_attribute_type_position) {
          for (size_t i = 0; i < accessor->count; i++) {
            float pos[3] = {0, 0, 0};
            cgltf_accessor_read_float(accessor, i, pos, 3);
            vertices[base_vertex + i].pos = glm::vec4{pos[0], pos[1], pos[2], 0};
          }
        } else if (attr.type == cgltf_attribute_type_texcoord) {
          float uv[2] = {0, 0};
          for (size_t i = 0; i < accessor->count; i++) {
            cgltf_accessor_read_float(accessor, i, uv, 2);
            vertices[base_vertex + i].uv = glm::vec2{uv[0], uv[1]};
          }
        } else if (attr.type == cgltf_attribute_type_normal) {
          float normal[3] = {0, 0, 1};
          for (size_t i = 0; i < accessor->count; i++) {
            cgltf_accessor_read_float(accessor, i, normal, 3);
            vertices[base_vertex + i].normal = glm::vec3{normal[0], normal[1], normal[2]};
          }
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
    raw_device_ = (MTL::Device *)device->get_native_device();
    auto model_load_res = load_model(resource_dir_ / "models" / "Cube/glTF/Cube.gltf", raw_device_);
    if (!model_load_res) {
      exit(1);
    }
    auto &model = model_load_res.value();
    size_t vertices_size = model.vertices.size() * sizeof(Vertex);
    size_t indices_size = model.indices.size() * sizeof(uint16_t);
    NS::SharedPtr<MTL::Buffer> vertex_buffer =
        NS::TransferPtr(raw_device_->newBuffer(vertices_size, MTL::ResourceStorageModeShared));
    memcpy(vertex_buffer->contents(), model.vertices.data(), vertices_size);
    NS::SharedPtr<MTL::Buffer> index_buffer =
        NS::TransferPtr(raw_device_->newBuffer(indices_size, MTL::ResourceStorageModeShared));
    memcpy(index_buffer->contents(), model.indices.data(), indices_size);
    size_t frames_in_flight = 2;
    NS::SharedPtr<MTL::Buffer> uniform_buffer = NS::TransferPtr(raw_device_->newBuffer(
        frames_in_flight * align_256(sizeof(Uniforms)), MTL::ResourceStorageModeShared));

    MTL::CommandQueue *queue = raw_device_->newCommandQueue();
    queue->autorelease();

    MTL::RenderPipelineState *main_pipeline_state{};
    struct Shader {
      MTL::Function *vert_func;
      MTL::Function *frag_func;
    };
    Shader main_shader{};

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
      main_shader.vert_func = shader_lib->newFunction(ns_string("vertexMain"));
      main_shader.frag_func = shader_lib->newFunction(ns_string("fragmentMain"));
      MTL::RenderPipelineDescriptor *pipeline_desc = MTL::RenderPipelineDescriptor::alloc()->init();
      pipeline_desc->setVertexFunction(main_shader.vert_func);
      pipeline_desc->setFragmentFunction(main_shader.frag_func);
      pipeline_desc->setLabel(ns_string("basic"));
      pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
      pipeline_desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

      main_pipeline_state = raw_device_->newRenderPipelineState(pipeline_desc, &err);

      pipeline_desc->release();
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

    MTL::ArgumentEncoder *frag_enc = main_shader.frag_func->newArgumentEncoder(0);
    NS::SharedPtr<MTL::Buffer> materials_buffer = NS::TransferPtr(
        raw_device_->newBuffer(frag_enc->encodedLength(), MTL::ResourceStorageModeShared));
    frag_enc->setArgumentBuffer(materials_buffer.get(), 0);

    size_t curr_frame = 0;

    uint64_t img_handle = 0;
    std::vector<MTL::Texture *> uploaded_textures;
    {
      MTL::CommandBuffer *buf = queue->commandBuffer();
      if (!model.texture_uploads.empty()) {
        MTL::BlitCommandEncoder *blit_enc = buf->blitCommandEncoder();
        for (auto &upload : model.texture_uploads) {
          auto &tex = upload.tex;
          MTL::Region region = MTL::Region::Make2D(0, 0, upload.dims.x, upload.dims.y);
          size_t src_img_size = upload.bytes_per_row * upload.dims.y;
          MTL::Buffer *upload_buf =
              raw_device_->newBuffer(src_img_size, MTL::ResourceStorageModeShared);
          memcpy(upload_buf->contents(), upload.data, src_img_size);
          MTL::Origin origin = MTL::Origin::Make(0, 0, 0);
          MTL::Size img_size = MTL::Size::Make(upload.dims.x, upload.dims.y, upload.dims.z);
          blit_enc->copyFromBuffer(upload_buf, 0, upload.bytes_per_row, 0, img_size, tex, 0, 0,
                                   origin);
          img_handle = static_cast<uint64_t>(tex->gpuResourceID()._impl);
          uploaded_textures.push_back(tex);
          frag_enc->setTexture(tex, 0);
          LINFO("{} handle", img_handle);
          blit_enc->generateMipmaps(tex);
          tex->retain();
        }
        blit_enc->endEncoding();
        model.texture_uploads.clear();
      }
      buf->commit();
      buf->waitUntilCompleted();
    }

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
      static float t = 0;
      t++;
      float ch = t / 500.f;

      uniform_data->vp = glm::perspective(glm::radians(70.f), aspect, 0.1f, 1000.f) *
                         glm::lookAt(glm::vec3{glm::sin(ch) * 2, 2, glm::cos(ch) * 2}, glm::vec3{0},
                                     glm::vec3{0, 1, 0});

      for (auto *tex : uploaded_textures) {
        enc->useResource(tex, MTL::ResourceUsageRead);
      }
      enc->setVertexBuffer(uniform_buffer.get(), uniforms_offset, 1);
      enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
      enc->setFragmentBuffer(materials_buffer.get(), 0, 0);
      enc->setCullMode(MTL::CullModeBack);
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
