#pragma once

#include <unordered_set>

#include <glm/vec2.hpp>

namespace teng::engine {

struct EngineInputSnapshot {
  [[nodiscard]] bool key_down(int key) const { return held_keys.contains(key); }
  [[nodiscard]] bool key_pressed(int key) const { return pressed_keys.contains(key); }
  [[nodiscard]] bool key_released(int key) const { return released_keys.contains(key); }

  std::unordered_set<int> held_keys;
  std::unordered_set<int> pressed_keys;
  std::unordered_set<int> released_keys;
  glm::vec2 cursor_delta{};
  float delta_seconds{};
  bool imgui_blocks_keyboard{};
};

}  // namespace teng::engine
