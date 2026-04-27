#include "engine/scene/SceneIds.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdio>
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

[[nodiscard]] constexpr bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] std::optional<uint64_t> parse_hex_u64(std::string_view text) {
  if (text.size() != 16) {
    return std::nullopt;
  }
  uint64_t value{};
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value, 16);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<std::array<char, 32>> compact_asset_id_text(std::string_view text) {
  std::array<char, 32> compact{};
  size_t out_i = 0;
  for (const char c : text) {
    if (c == '-') {
      continue;
    }
    if (!is_hex_digit(c) || out_i >= compact.size()) {
      return std::nullopt;
    }
    compact[out_i++] = c;
  }
  if (out_i != compact.size()) {
    return std::nullopt;
  }
  return compact;
}

}  // namespace

SceneId make_scene_id() { return SceneId{next_scene_id.fetch_add(1, std::memory_order_relaxed)}; }

EntityGuid make_entity_guid() {
  return EntityGuid{next_entity_guid.fetch_add(1, std::memory_order_relaxed)};
}

AssetId AssetId::from_path(const std::filesystem::path& resource_relative_path) {
  const auto normalized = resource_relative_path.lexically_normal().generic_string();
  return AssetId::from_parts(0, fnv1a_64(normalized));
}

std::optional<AssetId> AssetId::parse(std::string_view text) {
  const std::optional<std::array<char, 32>> compact = compact_asset_id_text(text);
  if (!compact) {
    return std::nullopt;
  }
  const std::string_view compact_view{compact->data(), compact->size()};
  const std::optional<uint64_t> high_bits = parse_hex_u64(compact_view.substr(0, 16));
  const std::optional<uint64_t> low_bits = parse_hex_u64(compact_view.substr(16, 16));
  if (!high_bits || !low_bits) {
    return std::nullopt;
  }
  AssetId id = AssetId::from_parts(*high_bits, *low_bits);
  if (!id.is_valid()) {
    return std::nullopt;
  }
  return id;
}

std::string AssetId::to_string() const {
  std::array<char, 37> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%08llx-%04llx-%04llx-%04llx-%012llx",
                static_cast<unsigned long long>((high >> 32) & 0xffffffffull),
                static_cast<unsigned long long>((high >> 16) & 0xffffull),
                static_cast<unsigned long long>(high & 0xffffull),
                static_cast<unsigned long long>((low >> 48) & 0xffffull),
                static_cast<unsigned long long>(low & 0x0000ffffffffffffull));
  return buffer.data();
}

}  // namespace teng::engine
