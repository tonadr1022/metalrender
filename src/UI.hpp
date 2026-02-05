#pragma once

#include <string>

#include "core/Config.hpp"

struct ImFont;

namespace TENG_NAMESPACE {

void push_big_font();
void push_font(const std::string& name, float size);
void push_font(const std::string& name);
void add_font(const std::string& name, ImFont* font);

}  // namespace TENG_NAMESPACE
