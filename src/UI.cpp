#include "UI.hpp"

#include <map>

#include "imgui.h"

namespace {
std::multimap<float, ImFont*> font_size_map;

}

void push_big_font() {
  auto* biggest_font = font_size_map.rbegin()->second;
  ImGui::PushFont(biggest_font);
}

void add_font(ImFont* font, float size) { font_size_map.emplace(size, font); }
