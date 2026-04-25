#include "engine/scene/SceneIds.hpp"

#include <atomic>
#include <string>

namespace teng::engine {

namespace {

std::atomic_uint64_t next_scene_id{1};
std::atomic_uint64_t next_entity_guid{1};

constexpr uint64_t k_fnv_offset_basis = 14695981039346656037ull;
constexpr uint64_t k_fnv_prime = 1099511628211ull;

uint64_t fnv1a_64(std::string_view text) {
  uint64_t hash = k_fnv_offset_basis;
  for (const char c : text) {
    hash ^= static_cast<unsigned char>(c);
    hash *= k_fnv_prime;
  }
  return hash == 0 ? 1 : hash;
}

}  // namespace

SceneId make_scene_id() { return SceneId{next_scene_id.fetch_add(1, std::memory_order_relaxed)}; }

EntityGuid make_entity_guid() {
  return EntityGuid{next_entity_guid.fetch_add(1, std::memory_order_relaxed)};
}

AssetId AssetId::from_path(const std::filesystem::path& resource_relative_path) {
  const auto normalized = resource_relative_path.lexically_normal().generic_string();
  return AssetId{fnv1a_64(normalized)};
}

}  // namespace teng::engine
