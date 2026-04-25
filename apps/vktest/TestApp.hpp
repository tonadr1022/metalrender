#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "engine/Engine.hpp"

struct TestAppOptions {
  // When set, exit the main loop after this many full frames (poll + render).
  std::optional<std::uint32_t> quit_after_frames;
};

class TestApp {
 public:
  explicit TestApp(TestAppOptions options = {});
  ~TestApp();
  void run();

 private:
  std::unique_ptr<teng::engine::Engine> engine_;
};
