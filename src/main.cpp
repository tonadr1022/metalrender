#include <Metal/Metal.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <print>
#include <sstream>

#include "QuartzCore/CAMetalLayer.hpp"
#include "WindowApple.hpp"
#include "gfx/Device.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

struct Vertex {
  glm::vec3 pos;
  // glm::vec4 color;
};

namespace {

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

}  // namespace

struct App {
  App() { window->init(device.get()); }
  App(const App &) = delete;
  App(App &&) = delete;
  App &operator=(const App &) = delete;
  App &operator=(App &&) = delete;
  ~App() = default;
  std::filesystem::path resource_dir_ = get_resource_dir();

  void run() {
    auto *ar_pool = NS::AutoreleasePool::alloc()->init();
    std::vector<Vertex> vertices;
    // glm::vec3 colors[] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}};
    {
      int i = 0;
      for (int z = -1; z <= 1; z += 2) {
        for (int y = -1; y <= 1; y += 2) {
          for (int x = -1; x <= 1; x += 2, i++) {
            vertices.emplace_back(glm::vec3{x, y, z});
          }
        }
      }
    }

    vertices = {{{-0.5, -0.5, 0}}, {{0.5, 0.5, 0}}, {{0, 0.5, 0}}};
    size_t size = vertices.size() * sizeof(Vertex);

    raw_device_ = (MTL::Device *)device->get_native_device();

    MTL::Buffer *buffer = raw_device_->newBuffer(size, MTL::ResourceStorageModeShared);

    memcpy(buffer->contents(), vertices.data(), vertices.size() * sizeof(Vertex));

    MTL::CommandQueue *queue = raw_device_->newCommandQueue();
    CA::MetalLayer *layer = window->metal_layer_;

    MTL::RenderPipelineState *state{};
    {
      std::filesystem::path shader_path = resource_dir_ / "shaders/basic1.metal";
      std::string src;
      {
        std::ifstream file(shader_path);
        assert(file.is_open());
        std::ostringstream ss;
        ss << file.rdbuf();
        src = ss.str();
      }
      NS::String *path = NS::String::string(src.c_str(), NS::ASCIIStringEncoding);
      NS::Error *err{};
      MTL::Library *shader_lib = raw_device_->newLibrary(path, nullptr, &err);
      if (err != nullptr) {
        std::println("{}", err->localizedDescription()->cString(NS::ASCIIStringEncoding));
        exit(1);
      }
      auto *names = shader_lib->functionNames();
      for (size_t i = 0; i < names->count(); i++) {
        std::println("{}", names->object(i)->description()->cString(NS::ASCIIStringEncoding));
      }
      auto *vertex_fn = shader_lib->newFunction(ns_string("vertexMain"));
      auto *frag_fn = shader_lib->newFunction(ns_string("fragmentMain"));
      MTL::RenderPipelineDescriptor *pipeline_desc = MTL::RenderPipelineDescriptor::alloc()->init();
      pipeline_desc->setVertexFunction(vertex_fn);
      pipeline_desc->setFragmentFunction(frag_fn);
      pipeline_desc->setLabel(ns_string("basic"));
      pipeline_desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
      state = raw_device_->newRenderPipelineState(pipeline_desc, &err);
    }
    while (!should_quit()) {
      window->poll_events();
      CA::MetalDrawable *drawable = layer->nextDrawable();
      if (!drawable) {
        continue;
      }

      MTL::RenderPassDescriptor *render_pass_desc =
          MTL::RenderPassDescriptor::renderPassDescriptor();
      auto *color0 = render_pass_desc->colorAttachments()->object(0);
      color0->setTexture(drawable->texture());
      color0->setLoadAction(MTL::LoadActionClear);
      color0->setClearColor(MTL::ClearColor::Make(0.5, 0.1, 0.12, 1.0));
      color0->setStoreAction(MTL::StoreActionStore);

      MTL::CommandBuffer *buf = queue->commandBuffer();
      MTL::RenderCommandEncoder *enc = buf->renderCommandEncoder(render_pass_desc);

      enc->setVertexBuffer(buffer, 0, 0);
      {
        NS::Error *err;
        enc->setRenderPipelineState(state);
      }
      enc->drawPrimitives(MTL::PrimitiveTypeTriangle, 0, vertices.size(), 1);
      enc->endEncoding();

      buf->presentDrawable(drawable);
      buf->commit();
    }

    ar_pool->release();
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
  App app;
  app.run();
}
