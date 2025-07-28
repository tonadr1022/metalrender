#include <memory>

#include "WindowApple.hpp"
#include "gfx/Device.hpp"
#include "gfx/DeviceMetal.hpp"

struct App {
  App() { window->init(device.get()); }
  App(const App &) = delete;
  App(App &&) = delete;
  App &operator=(const App &) = delete;
  App &operator=(App &&) = delete;
  ~App() = default;

  void run() {
    while (!should_quit()) {
      window->poll_events();
    }
    window->shutdown();
    device->shutdown();
  }

 private:
  [[nodiscard]] bool should_quit() const { return window->should_close(); }

  std::unique_ptr<RHIDevice> device = create_metal_device();
  std::unique_ptr<Window> window = create_apple_window();
};

int main() {
  App app;
  app.run();
}
