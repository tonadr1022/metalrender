#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

namespace teng::engine {

struct SceneId {
  uint64_t value{};

  [[nodiscard]] bool is_valid() const { return value != 0; }
  explicit operator bool() const { return is_valid(); }
};

struct EntityGuid {
  uint64_t value{};

  [[nodiscard]] bool is_valid() const { return value != 0; }
  explicit operator bool() const { return is_valid(); }
};

struct AssetId {
  uint64_t value{};

  [[nodiscard]] bool is_valid() const { return value != 0; }
  explicit operator bool() const { return is_valid(); }

  static AssetId from_path(const std::filesystem::path& resource_relative_path);
};

[[nodiscard]] SceneId make_scene_id();
[[nodiscard]] EntityGuid make_entity_guid();

[[nodiscard]] constexpr bool operator==(SceneId a, SceneId b) { return a.value == b.value; }
[[nodiscard]] constexpr bool operator!=(SceneId a, SceneId b) { return !(a == b); }
[[nodiscard]] constexpr bool operator<(SceneId a, SceneId b) { return a.value < b.value; }

[[nodiscard]] constexpr bool operator==(EntityGuid a, EntityGuid b) { return a.value == b.value; }
[[nodiscard]] constexpr bool operator!=(EntityGuid a, EntityGuid b) { return !(a == b); }
[[nodiscard]] constexpr bool operator<(EntityGuid a, EntityGuid b) { return a.value < b.value; }

[[nodiscard]] constexpr bool operator==(AssetId a, AssetId b) { return a.value == b.value; }
[[nodiscard]] constexpr bool operator!=(AssetId a, AssetId b) { return !(a == b); }
[[nodiscard]] constexpr bool operator<(AssetId a, AssetId b) { return a.value < b.value; }

}  // namespace teng::engine

template <>
struct std::hash<teng::engine::SceneId> {
  std::size_t operator()(teng::engine::SceneId id) const noexcept {
    return std::hash<uint64_t>{}(id.value);
  }
};

template <>
struct std::hash<teng::engine::EntityGuid> {
  std::size_t operator()(teng::engine::EntityGuid id) const noexcept {
    return std::hash<uint64_t>{}(id.value);
  }
};

template <>
struct std::hash<teng::engine::AssetId> {
  std::size_t operator()(teng::engine::AssetId id) const noexcept {
    return std::hash<uint64_t>{}(id.value);
  }
};
