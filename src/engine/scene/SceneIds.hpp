#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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
  uint64_t high{};
  uint64_t low{};

  [[nodiscard]] bool is_valid() const { return high != 0 || low != 0; }
  explicit operator bool() const { return is_valid(); }

  [[nodiscard]] static AssetId from_parts(uint64_t high_bits, uint64_t low_bits) {
    return AssetId{.high = high_bits, .low = low_bits};
  }
  [[nodiscard]] static std::optional<AssetId> parse(std::string_view text);
  static AssetId from_path(const std::filesystem::path& resource_relative_path);
  [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] SceneId make_scene_id();
[[nodiscard]] EntityGuid make_entity_guid();
[[nodiscard]] AssetId make_asset_id();

[[nodiscard]] constexpr bool operator==(SceneId a, SceneId b) { return a.value == b.value; }
[[nodiscard]] constexpr bool operator!=(SceneId a, SceneId b) { return !(a == b); }
[[nodiscard]] constexpr bool operator<(SceneId a, SceneId b) { return a.value < b.value; }

[[nodiscard]] constexpr bool operator==(EntityGuid a, EntityGuid b) { return a.value == b.value; }
[[nodiscard]] constexpr bool operator!=(EntityGuid a, EntityGuid b) { return !(a == b); }
[[nodiscard]] constexpr bool operator<(EntityGuid a, EntityGuid b) { return a.value < b.value; }

[[nodiscard]] constexpr bool operator==(AssetId a, AssetId b) {
  return a.high == b.high && a.low == b.low;
}
[[nodiscard]] constexpr bool operator!=(AssetId a, AssetId b) { return !(a == b); }
[[nodiscard]] constexpr bool operator<(AssetId a, AssetId b) {
  return a.high == b.high ? a.low < b.low : a.high < b.high;
}

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
    const std::size_t high = std::hash<uint64_t>{}(id.high);
    const std::size_t low = std::hash<uint64_t>{}(id.low);
    return high ^ (low + 0x9e3779b97f4a7c15ull + (high << 6) + (high >> 2));
  }
};
