#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/Result.hpp"

namespace teng::engine::content {

inline constexpr uint32_t k_cooked_artifact_endian_marker = 0x1234abcdU;
inline constexpr size_t k_cooked_artifact_magic_size = 8;
inline constexpr size_t k_cooked_artifact_kind_size = 16;
inline constexpr uint32_t k_cooked_artifact_header_reserved_u32_count = 4;

struct CookedSectionDesc {
  uint32_t id{};
  uint64_t offset{};
  uint64_t size{};
};

[[nodiscard]] std::array<char, k_cooked_artifact_magic_size> fixed_magic(std::string_view text);
[[nodiscard]] std::array<char, k_cooked_artifact_kind_size> fixed_artifact_kind(
    std::string_view text);
[[nodiscard]] Result<void> validate_sections(std::span<const std::byte> bytes,
                                             std::span<const CookedSectionDesc> sections);
[[nodiscard]] const CookedSectionDesc* find_section(
    std::span<const CookedSectionDesc> sections, uint32_t id);

}  // namespace teng::engine::content
