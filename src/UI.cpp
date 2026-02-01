#include "UI.hpp"

#include <map>

#include "core/Config.hpp"
#include "imgui.h"

namespace TENG_NAMESPACE {

namespace {
std::multimap<float, ImFont*> font_size_map;

}

void push_big_font() {
  ImFont* biggest_font = font_size_map.rbegin()->second;
  ImGui::PushFont(biggest_font);
}

void add_font(ImFont* font, float size) { font_size_map.emplace(size, font); }

}  // namespace TENG_NAMESPACE
