#pragma once

#include "core/Config.hpp"

struct ImFont;

namespace TENG_NAMESPACE {

void push_big_font();
void add_font(ImFont* font, float size);

}  // namespace TENG_NAMESPACE
