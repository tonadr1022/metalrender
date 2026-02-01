#pragma once

#include <string>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace core {

std::pair<std::string, std::string> split_string_at_first(const std::string& str, char delimiter);

}  // namespace core

} // namespace TENG_NAMESPACE
