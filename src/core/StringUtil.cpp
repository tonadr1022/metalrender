#include "StringUtil.hpp"

std::pair<std::string, std::string> core::split_string_at_first(const std::string& str,
                                                                char delimiter) {
  size_t pos = str.find(delimiter);
  if (pos == std::string::npos) {
    return {str, ""};
  }
  return {str.substr(0, pos), str.substr(pos + 1)};
}
