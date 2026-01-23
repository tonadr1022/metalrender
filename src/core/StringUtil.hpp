#pragma once

#include <string>

namespace core {

std::pair<std::string, std::string> split_string_at_first(const std::string& str, char delimiter);

}  // namespace core
