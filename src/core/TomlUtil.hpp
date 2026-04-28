#pragma once

#include <filesystem>
#include <string>
#include <toml++/toml.hpp>

#include "core/Config.hpp"
#include "core/Result.hpp"

namespace TENG_NAMESPACE {

[[nodiscard]] inline Result<toml::table> parse_toml_file(const std::filesystem::path& path) {
  try {
    return toml::parse_file(path.string());
  } catch (const toml::parse_error& error) {
    return make_unexpected(std::string(error.description()));
  }
}

}  // namespace TENG_NAMESPACE
