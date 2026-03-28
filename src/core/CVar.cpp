#include "CVar.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <span>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_stdlib.h"

namespace TENG_NAMESPACE {

enum class CVarKind : char {
  Int,
  Float,
  String,
};

class CVarParameter {
 public:
  friend class CVarSystemImpl;

  uint32_t array_idx;

  CVarKind type;
  CVarFlags flags;
  std::string name;
  std::string description;
};

template <typename T>
struct CVarStorage {
  T default_value;
  T current;
  CVarParameter* parameter;
};

template <typename T>
struct CVarArray {
  std::vector<CVarStorage<T>> cvars;
  uint32_t last_cvar{0};
  explicit CVarArray(size_t size) { cvars.reserve(size); }
  T get_current(uint32_t idx) { return cvars[idx].current; }
  T* get_current_ptr(uint32_t idx) { return &cvars[idx].current; }
  template <typename U>
  void set_current(U&& val, uint32_t idx) {
    cvars[idx].current = std::forward<U>(val);
  }

  uint32_t add(const T& default_value, const T& current_value, CVarParameter* param) {
    uint32_t idx = cvars.size();
    cvars.emplace_back(default_value, current_value, param);
    param->array_idx = idx;
    return idx;
  }
  uint32_t add(const T& value, CVarParameter* param) {
    uint32_t idx = cvars.size();
    cvars.emplace_back(value, value, param);
    param->array_idx = idx;
    return idx;
  }
};

class CVarSystemImpl : public CVarSystem {
 public:
  CVarParameter* get_cvar(util::hash::HashedString hash) final {
    auto it = saved_cvars_.find(hash);
    if (it != saved_cvars_.end()) {
      return &it->second;
    }
    return nullptr;
  }
  void load_from_file(const std::string& path) override;

  CVarParameter* create_float_cvar(const char* name, const char* description, double default_value,
                                   double current_value) final;

  CVarParameter* create_string_cvar(const char* name, const char* description,
                                    const char* default_value, const char* current_value) final;
  CVarParameter* create_int_cvar(const char* name, const char* description, int32_t default_value,
                                 int32_t current_value) final;

  template <typename T>
  T* get_cvar_current(uint32_t hash);
  template <typename T>
  void set_cvar_current(uint32_t hash, const T& value);

  double* get_float_cvar(util::hash::HashedString hash) final {
    return get_cvar_current<double>(hash);
  }

  void set_float_cvar(util::hash::HashedString hash, double value) final {
    set_cvar_current<double>(hash, value);
  }

  int32_t* get_int_cvar(util::hash::HashedString hash) final {
    return get_cvar_current<int32_t>(hash);
  }
  void set_int_cvar(util::hash::HashedString hash, int32_t value) final {
    set_cvar_current<int32_t>(hash, value);
  }
  const char* get_string_cvar(util::hash::HashedString hash) final {
    return get_cvar_current<std::string>(hash)->c_str();
  }
  void set_string_cvar(util::hash::HashedString hash, const char* value) final {
    set_cvar_current<std::string>(hash, value);
  }

  template <typename T>
  CVarArray<T>& get_cvar_array();

  static CVarSystemImpl& get() { return static_cast<CVarSystemImpl&>(CVarSystem::get()); }

  void im_gui_label(const char* label, float text_width);
  void draw_imgui_edit_cvar_parameter(CVarParameter* p, float text_width);

  bool show_advanced = true;
  std::string search_txt;
  std::unordered_map<std::string, std::vector<CVarParameter*>> categorized_params;
  void draw_imgui_editor() final;
  void merge_cvar_flags(util::hash::HashedString hash, CVarFlags or_flags) final;

 private:
  CVarParameter* init_cvar(const char* name, const char* description);

  std::vector<CVarParameter*> active_edit_parameters_;
  std::unordered_map<uint32_t, CVarParameter> saved_cvars_;
  CVarArray<int32_t> int_cvars_{200};
  CVarArray<double> float_cvars_{200};
  CVarArray<std::string> string_cvars_{200};
};

template <>
CVarArray<int32_t>& CVarSystemImpl::get_cvar_array() {
  return int_cvars_;
}

template <>
CVarArray<double>& CVarSystemImpl::get_cvar_array() {
  return float_cvars_;
}

template <>
CVarArray<std::string>& CVarSystemImpl::get_cvar_array() {
  return string_cvars_;
}

CVarParameter* CVarSystemImpl::init_cvar(const char* name, const char* description) {
  if (get_cvar(name)) return nullptr;
  auto r = saved_cvars_.emplace(util::hash::HashedString{name}, CVarParameter{});
  CVarParameter* new_param = &r.first->second;
  new_param->name = name;
  new_param->description = description;
  return new_param;
}

void CVarSystemImpl::merge_cvar_flags(util::hash::HashedString hash, CVarFlags or_flags) {
  CVarParameter* p = get_cvar(hash);
  if (!p) {
    return;
  }
  p->flags = static_cast<CVarFlags>(static_cast<uint16_t>(p->flags) |
                                    static_cast<uint16_t>(or_flags));
}

void CVarSystemImpl::draw_imgui_editor() {
  ImGui::InputText("Filter", &search_txt);
  ImGui::Checkbox("Advanced", &show_advanced);
  active_edit_parameters_.clear();
  auto add_to_edit_list = [&](CVarParameter* param) {
    bool hidden = static_cast<uint32_t>(param->flags) & static_cast<uint32_t>(CVarFlags::NoEdit);
    bool advanced =
        static_cast<uint32_t>(param->flags) & static_cast<uint32_t>(CVarFlags::Advanced);
    if (!hidden && (show_advanced || !advanced) &&
        param->name.find(search_txt) != std::string::npos) {
      active_edit_parameters_.emplace_back(param);
    }
  };

  for (auto& v : get_cvar_array<int32_t>().cvars) {
    add_to_edit_list(v.parameter);
  }
  for (auto& v : get_cvar_array<double>().cvars) {
    add_to_edit_list(v.parameter);
  }
  for (auto& v : get_cvar_array<std::string>().cvars) {
    add_to_edit_list(v.parameter);
  }
  auto edit_params = [this](std::span<CVarParameter*> params) {
    std::ranges::sort(params, [](CVarParameter* a, CVarParameter* b) { return a->name < b->name; });
    float max_text_width = 0;
    for (CVarParameter* p : params) {
      max_text_width = std::max(max_text_width, ImGui::CalcTextSize(p->name.c_str()).x);
    }
    for (CVarParameter* p : params) {
      draw_imgui_edit_cvar_parameter(p, max_text_width);
    }
  };
  // categorize by "."
  if (active_edit_parameters_.size() > 10) {
    categorized_params.clear();
    for (CVarParameter* p : active_edit_parameters_) {
      size_t dot_pos = p->name.find_first_of('.');
      std::string category;
      if (dot_pos != std::string::npos) {
        category = p->name.substr(0, dot_pos);
      }
      categorized_params[category].emplace_back(p);
    }
    std::vector<std::string> category_order;
    category_order.reserve(categorized_params.size());
    for (const auto& entry : categorized_params) {
      category_order.push_back(entry.first);
    }
    std::ranges::sort(category_order);
    for (const std::string& category : category_order) {
      std::vector<CVarParameter*>& params = categorized_params[category];
      const char* name = category.empty() ? "uncategorized" : category.c_str();
      if (ImGui::BeginMenu(name)) {
        edit_params(params);
        ImGui::EndMenu();
      }
    }
  } else {
    edit_params(active_edit_parameters_);
  }
}

CVarParameter* CVarSystemImpl::create_float_cvar(const char* name, const char* description,
                                                 double default_value, double current_value) {
  auto* param = init_cvar(name, description);
  if (!param) return nullptr;
  param->type = CVarKind::Float;
  get_cvar_array<double>().add(default_value, current_value, param);
  return param;
}

CVarParameter* CVarSystemImpl::create_string_cvar(const char* name, const char* description,
                                                  const char* default_value,
                                                  const char* current_value) {
  auto* param = init_cvar(name, description);
  if (!param) return nullptr;
  param->type = CVarKind::String;
  get_cvar_array<std::string>().add(default_value, current_value, param);
  return param;
}

CVarParameter* CVarSystemImpl::create_int_cvar(const char* name, const char* description,
                                               int32_t default_value, int32_t current_value) {
  auto* param = init_cvar(name, description);
  if (!param) return nullptr;
  param->type = CVarKind::Int;
  get_cvar_array<int32_t>().add(default_value, current_value, param);
  return param;
}

template <typename T>
T* CVarSystemImpl::get_cvar_current(uint32_t hash) {
  CVarParameter* param = get_cvar(hash);
  if (!param) return nullptr;
  return get_cvar_array<T>().get_current_ptr(param->array_idx);
}

template <typename T>
void CVarSystemImpl::set_cvar_current(uint32_t hash, const T& value) {
  CVarParameter* param = get_cvar(hash);
  if (param) {
    get_cvar_array<T>().set_current(value, param->array_idx);
  }
}

void CVarSystemImpl::im_gui_label(const char* label, float text_width) {
  constexpr const float left_pad = 50;
  constexpr const float editor_width = 100;
  const ImVec2 line_start = ImGui::GetCursorScreenPos();
  float full_width = text_width + left_pad;
  ImGui::Text("%s", label);
  ImGui::SameLine();
  ImGui::SetCursorScreenPos(ImVec2{line_start.x + full_width, line_start.y});
  ImGui::SetNextItemWidth(editor_width);
}

void CVarSystemImpl::draw_imgui_edit_cvar_parameter(CVarParameter* p, float text_width) {
  const bool is_read_only =
      static_cast<uint32_t>(p->flags) & static_cast<uint32_t>(CVarFlags::EditReadOnly);
  const bool is_checkbox =
      static_cast<uint32_t>(p->flags) & static_cast<uint32_t>(CVarFlags::EditCheckbox);
  const bool is_drag =
      static_cast<uint32_t>(p->flags) & static_cast<uint32_t>(CVarFlags::EditFloatDrag);

  switch (p->type) {
    case CVarKind::Int:
      if (is_read_only) {
        std::string display_format = p->name + "= %i";
        ImGui::Text(display_format.c_str(), get_cvar_array<int32_t>().get_current(p->array_idx));
      } else {
        if (is_checkbox) {
          im_gui_label(p->name.c_str(), text_width);
          ImGui::PushID(p);
          bool is_checked = get_cvar_array<int32_t>().get_current(p->array_idx) != 0;
          if (ImGui::Checkbox("", &is_checked)) {
            get_cvar_array<int32_t>().set_current(static_cast<int32_t>(is_checked), p->array_idx);
          }
          ImGui::PopID();
        } else {
          im_gui_label(p->name.c_str(), text_width);
          ImGui::PushID(p);
          ImGui::InputInt("", get_cvar_array<int32_t>().get_current_ptr(p->array_idx));
          ImGui::PopID();
        }
      }
      break;
    case CVarKind::Float:
      if (is_read_only) {
        std::string display_format = p->name + "= %f";
        ImGui::Text(display_format.c_str(), get_cvar_array<double>().get_current(p->array_idx));
      } else {
        im_gui_label(p->name.c_str(), text_width);
        ImGui::PushID(p);
        if (is_drag) {
          float d = get_cvar_array<double>().get_current(p->array_idx);
          if (ImGui::DragFloat("", &d, .01)) {
            *get_cvar_array<double>().get_current_ptr(p->array_idx) = static_cast<double>(d);
          }
        } else {
          ImGui::InputDouble("", get_cvar_array<double>().get_current_ptr(p->array_idx));
        }
        ImGui::PopID();
      }
      break;
    case CVarKind::String:
      if (is_read_only) {
        std::string display_format = p->name + "= %s";
        ImGui::Text(display_format.c_str(),
                    get_cvar_array<std::string>().get_current(p->array_idx).c_str());
      } else {
        im_gui_label(p->name.c_str(), text_width);
        ImGui::PushID(p);
        ImGui::InputText("", get_cvar_array<std::string>().get_current_ptr(p->array_idx));
        ImGui::PopID();
      }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", p->description.c_str());
  }
}

CVarSystem& CVarSystem::get() {
  static CVarSystemImpl impl{};
  return impl;
}

namespace {

void assert_unique_cvar_registered(CVarParameter* param) {
  if (param != nullptr) return;
  assert(false && "duplicate CVar name; each name must be unique");
  std::abort();
}

template <typename T>
T get_cvar_current_by_index(uint32_t idx) {
  return CVarSystemImpl::get().get_cvar_array<T>().get_current(idx);
}
template <typename T>
T* get_cvar_current_ptr_by_index(uint32_t idx) {
  return CVarSystemImpl::get().get_cvar_array<T>().get_current_ptr(idx);
}

template <typename T, typename U>
void set_cvar_by_idx(uint32_t idx, U&& data) {
  CVarSystemImpl::get().get_cvar_array<T>().set_current(std::forward<U>(data), idx);
}

}  // namespace

AutoCVarFloat::AutoCVarFloat(const char* name, const char* description, double default_value,
                             CVarFlags flags) {
  CVarParameter* param =
      CVarSystemImpl::get().create_float_cvar(name, description, default_value, default_value);
  assert_unique_cvar_registered(param);
  param->flags = flags;
  idx_ = param->array_idx;
}

double AutoCVarFloat::get() { return get_cvar_current_by_index<double>(idx_); }

double* AutoCVarFloat::get_ptr() { return get_cvar_current_ptr_by_index<double>(idx_); }

float AutoCVarFloat::get_float() { return static_cast<float>(get()); }

void AutoCVarFloat::set(double val) { set_cvar_by_idx<double>(idx_, val); }

AutoCVarInt::AutoCVarInt(const char* name, const char* desc, int initial_value, CVarFlags flags) {
  CVarParameter* param =
      CVarSystemImpl::get().create_int_cvar(name, desc, initial_value, initial_value);
  assert_unique_cvar_registered(param);
  param->flags = flags;
  idx_ = param->array_idx;
}

int32_t AutoCVarInt::get() { return get_cvar_current_by_index<int32_t>(idx_); }

int32_t* AutoCVarInt::get_ptr() { return get_cvar_current_ptr_by_index<int32_t>(idx_); }

void AutoCVarInt::set(int32_t val) { set_cvar_by_idx<int32_t>(idx_, val); }

AutoCVarString::AutoCVarString(const char* name, const char* description, const char* default_value,
                                 CVarFlags flags) {
  CVarParameter* param =
      CVarSystemImpl::get().create_string_cvar(name, description, default_value, default_value);
  assert_unique_cvar_registered(param);
  param->flags = flags;
  idx_ = param->array_idx;
}

const char* AutoCVarString::get() {
  return get_cvar_current_ptr_by_index<std::string>(idx_)->c_str();
}

void AutoCVarString::set(std::string_view val) { set_cvar_by_idx<std::string>(idx_, std::string(val)); }

void AutoCVarString::set(std::string&& val) {
  set_cvar_by_idx<std::string>(idx_, std::move(val));
}

void CVarSystemImpl::load_from_file(const std::string&) {
#if !defined(NDEBUG)
  assert(false && "CVarSystem::load_from_file is not implemented");
#endif
}

}  // namespace TENG_NAMESPACE