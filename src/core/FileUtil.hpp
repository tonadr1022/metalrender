#pragma once

#include <filesystem>
#include <string>
namespace util {

std::string load_file_to_string(const std::filesystem::path &path);

}  // namespace util
