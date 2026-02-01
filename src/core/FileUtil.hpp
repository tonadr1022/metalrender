#pragma once

#include <filesystem>
#include <string>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {
namespace util {

std::string load_file_to_string(const std::filesystem::path &path);

}  // namespace util

} // namespace TENG_NAMESPACE
