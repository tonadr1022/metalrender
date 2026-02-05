#include "UI.hpp"

#include <unordered_map>

#include "core/Config.hpp"
#include "core/EAssert.hpp"
#include "imgui.h"

namespace TENG_NAMESPACE {

namespace {
std::unordered_map<std::string, ImFont*> fonts;
}

void add_font(const std::string& name, ImFont* font) { fonts.emplace(name, font); }

void push_font(const std::string& name, float size) {
  auto it = fonts.find(name);
  ASSERT(it != fonts.end());
  if (it != fonts.end()) {
    ImGui::PushFont(it->second, size);
  }
}

void push_font(const std::string& name) {
  auto it = fonts.find(name);
  ASSERT(it != fonts.end());
  if (it != fonts.end()) {
    ImGui::PushFont(it->second);
  }
}

}  // namespace TENG_NAMESPACE
