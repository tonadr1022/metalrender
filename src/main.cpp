#include "gfx/DeviceMetal.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "WindowApple.hpp"

struct App {
  App() {
    window.init(&device);
    [[maybe_unused]] MTL::Device *device = MTL::CreateSystemDefaultDevice();
  }
  App(const App &) = delete;
  App(App &&) = delete;
  App &operator=(const App &) = delete;
  App &operator=(App &&) = delete;
  ~App() = default;

  void run() {
    while (!should_quit()) {
      window.poll_events();
    }
    window.shutdown();
    device.shutdown();
  }

 private:
  [[nodiscard]] bool should_quit() const { return window.should_close(); }
  DeviceMetal device;
  WindowApple window;
};

int main() {
  App app;
  app.run();
}
