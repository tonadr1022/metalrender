#include "FileUtil.hpp"

#include <fstream>
#include <sstream>

std::string util::load_file_to_string(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::println("File not found or cannot be opened at path {}", path.string());
    return "";
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}
