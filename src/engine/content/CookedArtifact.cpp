#include "engine/content/CookedArtifact.hpp"

#include <algorithm>
#include <string>

namespace teng::engine::content {

std::array<char, k_cooked_artifact_magic_size> fixed_magic(std::string_view text) {
  std::array<char, k_cooked_artifact_magic_size> out{};
  std::ranges::copy(text.substr(0, out.size()), out.begin());
  return out;
}

std::array<char, k_cooked_artifact_kind_size> fixed_artifact_kind(std::string_view text) {
  std::array<char, k_cooked_artifact_kind_size> out{};
  std::ranges::copy(text.substr(0, out.size()), out.begin());
  return out;
}

Result<void> validate_sections(std::span<const std::byte> bytes,
                               std::span<const CookedSectionDesc> sections) {
  for (size_t i = 0; i < sections.size(); ++i) {
    const CookedSectionDesc& section = sections[i];
    if (section.offset > bytes.size() || section.size > bytes.size() - section.offset) {
      return make_unexpected("cooked section " + std::to_string(section.id) +
                             " is out of bounds");
    }
    for (size_t j = i + 1; j < sections.size(); ++j) {
      const CookedSectionDesc& other = sections[j];
      const uint64_t section_end = section.offset + section.size;
      const uint64_t other_end = other.offset + other.size;
      if (section.offset < other_end && other.offset < section_end) {
        return make_unexpected("cooked sections overlap");
      }
    }
  }
  return {};
}

const CookedSectionDesc* find_section(std::span<const CookedSectionDesc> sections, uint32_t id) {
  const auto it = std::ranges::find(sections, id, &CookedSectionDesc::id);
  return it == sections.end() ? nullptr : &*it;
}

}  // namespace teng::engine::content
