#pragma once

#include <glm/vec2.hpp>
#include <unordered_set>

#include "engine/scene/ComponentReflectionMacros.hpp"

namespace teng::engine {

namespace KeyCode {
inline constexpr int A = 65;
inline constexpr int B = 66;
inline constexpr int D = 68;
inline constexpr int F = 70;
inline constexpr int H = 72;
inline constexpr int I = 73;
inline constexpr int J = 74;
inline constexpr int K = 75;
inline constexpr int L = 76;
inline constexpr int R = 82;
inline constexpr int S = 83;
inline constexpr int V = 86;
inline constexpr int W = 87;
inline constexpr int Y = 89;
inline constexpr int Escape = 256;
}  // namespace KeyCode

struct TENG_COMPONENT(key = "teng.core.engine_input_snapshot", module = "teng.core",
                      schema_version = 1, storage = "RuntimeSession", visibility = "Hidden")
    EngineInputSnapshot {
  [[nodiscard]] bool key_down(int key) const { return held_keys.contains(key); }
  [[nodiscard]] bool key_pressed(int key) const { return pressed_keys.contains(key); }
  [[nodiscard]] bool key_released(int key) const { return released_keys.contains(key); }

  // TODO: arrays
  std::unordered_set<int> held_keys;
  std::unordered_set<int> pressed_keys;
  std::unordered_set<int> released_keys;
  glm::vec2 cursor_delta{};
  float delta_seconds{};
  bool imgui_blocks_keyboard{};
};

}  // namespace teng::engine
