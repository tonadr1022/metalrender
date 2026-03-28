#include "CVar.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/Console.hpp"
#include "core/Logger.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "util/FuzzyMatch.hpp"

namespace TENG_NAMESPACE {

enum class CVarKind : char {
  Int,
  Float,
  String,
};

class CVarSystemImpl;

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
  friend class CVarSystemImpl;

  std::vector<CVarStorage<T>> cvars;
  uint32_t last_cvar{0};
  explicit CVarArray(size_t size) { cvars.reserve(size); }
  T get_current(uint32_t idx) { return cvars[idx].current; }
  T* get_current_ptr(uint32_t idx) { return &cvars[idx].current; }

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

 private:
  template <typename U>
  void set_current(U&& val, uint32_t idx) {
    cvars[idx].current = std::forward<U>(val);
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
  void save_to_file(const std::string& path) override;

  CVarParameter* create_float_cvar(const char* name, const char* description, float default_value,
                                   float current_value) final;

  CVarParameter* create_string_cvar(const char* name, const char* description,
                                    const char* default_value, const char* current_value) final;
  CVarParameter* create_int_cvar(const char* name, const char* description, int32_t default_value,
                                 int32_t current_value) final;

  template <typename T>
  T* get_cvar_current(uint32_t hash);

  float* get_float_cvar(util::hash::HashedString hash) final {
    return get_cvar_current<float>(hash);
  }

  void set_float_cvar(util::hash::HashedString hash, float value) final {
    CVarParameter* param = get_cvar(hash);
    if (param) {
      assign_float(param, value);
    }
  }

  int32_t* get_int_cvar(util::hash::HashedString hash) final {
    return get_cvar_current<int32_t>(hash);
  }
  void set_int_cvar(util::hash::HashedString hash, int32_t value) final {
    CVarParameter* param = get_cvar(hash);
    if (param) {
      assign_int(param, value);
    }
  }
  const char* get_string_cvar(util::hash::HashedString hash) final {
    return get_cvar_current<std::string>(hash)->c_str();
  }
  void set_string_cvar(util::hash::HashedString hash, const char* value) final {
    CVarParameter* param = get_cvar(hash);
    if (param) {
      assign_string(param, value ? std::string(value) : std::string{});
    }
  }

  void add_change_callback(util::hash::HashedString name, std::function<void()> cb) final;
  void add_change_callback(const AutoCVarInt& cv, std::function<void()> cb) final;
  void add_change_callback(const AutoCVarFloat& cv, std::function<void()> cb) final;
  void add_change_callback(const AutoCVarString& cv, std::function<void()> cb) final;

  void assign_int(CVarParameter* p, int32_t v);
  void assign_float(CVarParameter* p, float v);
  void assign_string(CVarParameter* p, std::string v);
  void fire_change_callbacks(util::hash::HashedString name);

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
  void for_each_cvar(std::function<void(const CVarInfoView&)> visitor) final;
  CVarApplyResult set_cvar_from_string(std::string_view name, std::string_view value,
                                       std::string* error_msg) final;

 private:
  CVarParameter* init_cvar(const char* name, const char* description);

  std::vector<CVarParameter*> active_edit_parameters_;
  std::unordered_map<uint32_t, CVarParameter> saved_cvars_;
  std::unordered_map<uint32_t, std::vector<std::function<void()>>> change_callbacks_;
  CVarArray<int32_t> int_cvars_{200};
  CVarArray<float> float_cvars_{200};
  CVarArray<std::string> string_cvars_{200};
};

template <>
CVarArray<int32_t>& CVarSystemImpl::get_cvar_array() {
  return int_cvars_;
}

template <>
CVarArray<float>& CVarSystemImpl::get_cvar_array() {
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
  p->flags =
      static_cast<CVarFlags>(static_cast<uint16_t>(p->flags) | static_cast<uint16_t>(or_flags));
}

void CVarSystemImpl::for_each_cvar(std::function<void(const CVarInfoView&)> visitor) {
  for (auto& entry : saved_cvars_) {
    CVarParameter& p = entry.second;
    CVarInfoView view{
        .name = p.name,
        .description = p.description,
        .type = p.type == CVarKind::Int     ? CVarValueType::Int
                : p.type == CVarKind::Float ? CVarValueType::Float
                                            : CVarValueType::String,
        .flags = p.flags,
    };
    visitor(view);
  }
}

namespace {

bool has_flag(CVarFlags flags, CVarFlags test) {
  return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(test)) != 0;
}

std::string_view trim_sv(std::string_view s);

}  // namespace

CVarApplyResult CVarSystemImpl::set_cvar_from_string(std::string_view name, std::string_view value,
                                                     std::string* error_msg) {
  if (error_msg) {
    error_msg->clear();
  }
  name = trim_sv(name);
  value = trim_sv(value);
  if (name.empty() || value.empty()) {
    if (error_msg) {
      *error_msg = "Missing name or value";
    }
    return CVarApplyResult::InvalidValue;
  }

  using util::hash::HashedString;
  CVarParameter* param = get_cvar(HashedString{name});
  if (!param) {
    if (error_msg) {
      *error_msg = "Unknown cvar";
    }
    return CVarApplyResult::NotFound;
  }

  if (has_flag(param->flags, CVarFlags::NoEdit) ||
      has_flag(param->flags, CVarFlags::EditReadOnly)) {
    if (error_msg) {
      *error_msg = "CVar is read-only";
    }
    return CVarApplyResult::ReadOnly;
  }

  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  } else if (!value.empty() && value.front() == '"' && value.back() != '"') {
    if (error_msg) {
      *error_msg = "Unterminated quoted string";
    }
    return CVarApplyResult::InvalidValue;
  }

  switch (param->type) {
    case CVarKind::Int: {
      int32_t v{};
      const auto* begin = value.data();
      const auto* end = value.data() + value.size();
      const auto r = std::from_chars(begin, end, v);
      if (r.ec != std::errc{} || r.ptr != end) {
        if (error_msg) {
          *error_msg = "Invalid integer value";
        }
        return CVarApplyResult::InvalidValue;
      }
      set_int_cvar(HashedString{name}, v);
      return CVarApplyResult::Ok;
    }
    case CVarKind::Float: {
      std::string val_str(value);
      char* end = nullptr;
      const float v = std::strtod(val_str.c_str(), &end);
      if (end != val_str.c_str() + val_str.size()) {
        if (error_msg) {
          *error_msg = "Invalid float value";
        }
        return CVarApplyResult::InvalidValue;
      }
      set_float_cvar(HashedString{name}, v);
      return CVarApplyResult::Ok;
    }
    case CVarKind::String: {
      std::string val_str(value);
      set_string_cvar(HashedString{name}, val_str.c_str());
      return CVarApplyResult::Ok;
    }
  }
  return CVarApplyResult::InvalidValue;
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
  for (auto& v : get_cvar_array<float>().cvars) {
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
                                                 float default_value, float current_value) {
  auto* param = init_cvar(name, description);
  if (!param) return nullptr;
  param->type = CVarKind::Float;
  get_cvar_array<float>().add(default_value, current_value, param);
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

void CVarSystemImpl::add_change_callback(util::hash::HashedString name, std::function<void()> cb) {
  if (!get_cvar(name)) {
    return;
  }
  change_callbacks_[name.hash_value].push_back(std::move(cb));
}

void CVarSystemImpl::add_change_callback(const AutoCVarInt& cv, std::function<void()> cb) {
  add_change_callback(cv.hashed_name(), std::move(cb));
}

void CVarSystemImpl::add_change_callback(const AutoCVarFloat& cv, std::function<void()> cb) {
  add_change_callback(cv.hashed_name(), std::move(cb));
}

void CVarSystemImpl::add_change_callback(const AutoCVarString& cv, std::function<void()> cb) {
  add_change_callback(cv.hashed_name(), std::move(cb));
}

void CVarSystemImpl::fire_change_callbacks(util::hash::HashedString name) {
  auto it = change_callbacks_.find(name.hash_value);
  if (it == change_callbacks_.end()) {
    return;
  }
  for (const auto& cb : it->second) {
    cb();
  }
}

void CVarSystemImpl::assign_int(CVarParameter* p, int32_t v) {
  if (p->type != CVarKind::Int) {
    LWARN("incorrect cvar type");
    return;
  }
  if (int_cvars_.get_current(p->array_idx) == v) {
    return;
  }
  int_cvars_.set_current(v, p->array_idx);
  fire_change_callbacks(util::hash::HashedString{p->name});
}

void CVarSystemImpl::assign_float(CVarParameter* p, float v) {
  if (p->type != CVarKind::Float) {
    LWARN("incorrect cvar type");
    return;
  }
  if (float_cvars_.get_current(p->array_idx) == v) {
    return;
  }
  float_cvars_.set_current(v, p->array_idx);
  fire_change_callbacks(util::hash::HashedString{p->name});
}

void CVarSystemImpl::assign_string(CVarParameter* p, std::string v) {
  if (p->type != CVarKind::String) {
    LWARN("incorrect cvar type");
    return;
  }
  if (string_cvars_.get_current(p->array_idx) == v) {
    return;
  }
  string_cvars_.set_current(std::move(v), p->array_idx);
  fire_change_callbacks(util::hash::HashedString{p->name});
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

  using util::hash::HashedString;

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
            set_int_cvar(HashedString{p->name}, static_cast<int32_t>(is_checked));
          }
          ImGui::PopID();
        } else {
          im_gui_label(p->name.c_str(), text_width);
          ImGui::PushID(p);
          int v = get_cvar_array<int32_t>().get_current(p->array_idx);
          if (ImGui::InputInt("", &v)) {
            set_int_cvar(HashedString{p->name}, static_cast<int32_t>(v));
          }
          ImGui::PopID();
        }
      }
      break;
    case CVarKind::Float:
      if (is_read_only) {
        std::string display_format = p->name + "= %f";
        ImGui::Text(display_format.c_str(), get_cvar_array<float>().get_current(p->array_idx));
      } else {
        im_gui_label(p->name.c_str(), text_width);
        ImGui::PushID(p);
        if (is_drag) {
          auto d = static_cast<float>(get_cvar_array<float>().get_current(p->array_idx));
          if (ImGui::DragFloat("", &d, .01f)) {
            set_float_cvar(HashedString{p->name}, d);
          }
        } else {
          float d = get_cvar_array<float>().get_current(p->array_idx);
          if (ImGui::InputFloat("", &d)) {
            set_float_cvar(HashedString{p->name}, d);
          }
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
        static std::unordered_map<uintptr_t, std::string> string_edit_bufs;
        std::string& edit_buf = string_edit_bufs[reinterpret_cast<uintptr_t>(p)];
        const ImGuiID str_id = ImGui::GetID("##cvarstr");
        const bool editing = (ImGui::GetActiveID() == str_id);
        if (!editing) {
          edit_buf = get_cvar_array<std::string>().get_current(p->array_idx);
        }
        ImGui::InputText("##cvarstr", &edit_buf);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          set_string_cvar(HashedString{p->name}, edit_buf.c_str());
        }
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
  auto& impl = CVarSystemImpl::get();
  if constexpr (std::is_same_v<T, int32_t>) {
    CVarParameter* p = impl.get_cvar_array<int32_t>().cvars[idx].parameter;
    impl.assign_int(p, static_cast<int32_t>(data));
  } else if constexpr (std::is_same_v<T, float>) {
    CVarParameter* p = impl.get_cvar_array<float>().cvars[idx].parameter;
    impl.assign_float(p, static_cast<float>(data));
  } else if constexpr (std::is_same_v<T, std::string>) {
    CVarParameter* p = impl.get_cvar_array<std::string>().cvars[idx].parameter;
    impl.assign_string(p, std::forward<U>(data));
  }
}

}  // namespace

AutoCVarFloat::AutoCVarFloat(const char* name, const char* description, float default_value,
                             CVarFlags flags) {
  name_hash_ = util::hash::HashedString{name}.hash_value;
  CVarParameter* param =
      CVarSystemImpl::get().create_float_cvar(name, description, default_value, default_value);
  assert_unique_cvar_registered(param);
  param->flags = flags;
  idx_ = param->array_idx;
}

float AutoCVarFloat::get() { return get_cvar_current_by_index<float>(idx_); }

float* AutoCVarFloat::get_ptr() { return get_cvar_current_ptr_by_index<float>(idx_); }

void AutoCVarFloat::set(float val) { set_cvar_by_idx<float>(idx_, val); }

AutoCVarInt::AutoCVarInt(const char* name, const char* desc, int initial_value, CVarFlags flags) {
  name_hash_ = util::hash::HashedString{name}.hash_value;
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
  name_hash_ = util::hash::HashedString{name}.hash_value;
  CVarParameter* param =
      CVarSystemImpl::get().create_string_cvar(name, description, default_value, default_value);
  assert_unique_cvar_registered(param);
  param->flags = flags;
  idx_ = param->array_idx;
}

const char* AutoCVarString::get() {
  return get_cvar_current_ptr_by_index<std::string>(idx_)->c_str();
}

void AutoCVarString::set(std::string_view val) {
  set_cvar_by_idx<std::string>(idx_, std::string(val));
}

void AutoCVarString::set(std::string&& val) { set_cvar_by_idx<std::string>(idx_, std::move(val)); }

namespace {

constexpr std::string_view kLegacyMeshShadersKey = "mesh_shaders_enabled";
constexpr std::string_view kMeshShadersCVar = "renderer.pipeline.mesh_shaders";

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }

std::string_view trim_sv(std::string_view s) {
  while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

void apply_cvar_line(CVarSystemImpl& sys, std::string_view key_sv, std::string_view value_sv) {
  key_sv = trim_sv(key_sv);
  value_sv = trim_sv(value_sv);
  if (key_sv.empty()) {
    return;
  }

  using util::hash::HashedString;

  if (key_sv == kLegacyMeshShadersKey) {
    const int32_t mesh = (value_sv == "1") ? 1 : 0;
    sys.set_int_cvar(HashedString{kMeshShadersCVar}, mesh);
    return;
  }

  CVarParameter* param = sys.get_cvar(HashedString{key_sv});
  if (!param) {
    return;
  }

  switch (param->type) {
    case CVarKind::Int: {
      const std::string val_str(value_sv);
      int32_t v{};
      const auto r = std::from_chars(val_str.data(), val_str.data() + val_str.size(), v);
      if (r.ec != std::errc{} || r.ptr != val_str.data() + val_str.size()) {
        return;
      }
      sys.set_int_cvar(HashedString{key_sv}, v);
      break;
    }
    case CVarKind::Float: {
      const std::string val_str(value_sv);
      char* end = nullptr;
      const float v = std::strtod(val_str.c_str(), &end);
      if (end != val_str.c_str() + val_str.size()) {
        return;
      }
      sys.set_float_cvar(HashedString{key_sv}, v);
      break;
    }
    case CVarKind::String: {
      const std::string val_str(value_sv);
      sys.set_string_cvar(HashedString{key_sv}, val_str.c_str());
      break;
    }
  }
}

}  // namespace

void CVarSystemImpl::load_from_file(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return;
  }
  std::string line;
  while (std::getline(file, line)) {
    std::string_view line_sv = trim_sv(line);
    if (line_sv.empty() || line_sv.front() == '#') {
      continue;
    }
    const size_t eq = line_sv.find('=');
    if (eq == std::string_view::npos) {
      continue;
    }
    apply_cvar_line(*this, line_sv.substr(0, eq), line_sv.substr(eq + 1));
  }
}

void CVarSystemImpl::save_to_file(const std::string& path) {
  ZoneScoped;
  std::ofstream out(path);
  if (!out.is_open()) {
    return;
  }
  out << "# teng cvars\n";

  std::vector<CVarParameter*> params;
  params.reserve(saved_cvars_.size());
  for (auto& entry : saved_cvars_) {
    params.push_back(&entry.second);
  }
  std::ranges::sort(params, [](CVarParameter* a, CVarParameter* b) { return a->name < b->name; });

  std::ostringstream oss;
  oss << std::setprecision(std::numeric_limits<float>::max_digits10);

  for (CVarParameter* p : params) {
    switch (p->type) {
      case CVarKind::Int:
        out << p->name << '=' << int_cvars_.get_current(p->array_idx) << '\n';
        break;
      case CVarKind::Float:
        oss.str({});
        oss.clear();
        oss << float_cvars_.get_current(p->array_idx);
        out << p->name << '=' << oss.str() << '\n';
        break;
      case CVarKind::String:
        out << p->name << '=' << string_cvars_.get_current(p->array_idx) << '\n';
        break;
    }
  }
}

namespace {

bool parse_console_assignment(std::string_view line, std::string_view& name,
                              std::string_view& value, std::string& error_msg) {
  error_msg.clear();
  std::string_view trimmed = trim_sv(line);
  if (trimmed.empty()) {
    error_msg = "Empty input";
    return false;
  }

  bool in_quotes = false;
  size_t eq_pos = std::string_view::npos;
  for (size_t i = 0; i < trimmed.size(); i++) {
    char c = trimmed[i];
    if (c == '"') {
      in_quotes = !in_quotes;
    } else if (c == '=' && !in_quotes) {
      eq_pos = i;
      break;
    }
  }

  if (eq_pos != std::string_view::npos) {
    name = trim_sv(trimmed.substr(0, eq_pos));
    value = trim_sv(trimmed.substr(eq_pos + 1));
  } else {
    size_t space = trimmed.find_first_of(" \t");
    if (space == std::string_view::npos) {
      error_msg = "Missing value";
      return false;
    }
    name = trim_sv(trimmed.substr(0, space));
    value = trim_sv(trimmed.substr(space + 1));
  }

  if (name.empty() || value.empty()) {
    error_msg = "Missing name or value";
    return false;
  }
  return true;
}

}  // namespace

void register_cvar_console(Console& console) {
  console.set_fallback_handler([](std::string_view line, std::string* error) {
    std::string_view name;
    std::string_view value;
    std::string parse_error;
    if (!parse_console_assignment(line, name, value, parse_error)) {
      if (error) {
        *error = parse_error;
      }
      return false;
    }

    std::string err;
    CVarApplyResult result = CVarSystem::get().set_cvar_from_string(name, value, &err);
    if (result != CVarApplyResult::Ok) {
      if (error) {
        *error = err.empty() ? "Failed to set cvar" : err;
      }
      return false;
    }
    return true;
  });

  console.set_completion_provider([](std::string_view input, std::vector<ConsoleSuggestion>& out,
                                     size_t& replace_start, size_t& replace_len) {
    std::string_view trimmed = trim_sv(input);
    size_t leading = input.find_first_not_of(" \t");
    if (leading == std::string_view::npos) {
      return;
    }
    if (trimmed.find_first_of(" \t") != std::string_view::npos ||
        trimmed.find('=') != std::string_view::npos) {
      return;
    }
    if (trimmed.empty()) {
      return;
    }

    const std::string needle(trimmed);

    replace_start = leading;
    replace_len = needle.size();

    std::vector<ConsoleSuggestion> matches;
    CVarSystem::get().for_each_cvar([&](const CVarInfoView& info) {
      if (util::fuzzy_match(needle.c_str(), info.name.c_str())) {
        ConsoleSuggestion s;
        s.label = std::string(info.name);
        switch (info.type) {
          case CVarValueType::Int:
            s.detail = "int";
            break;
          case CVarValueType::Float:
            s.detail = "float";
            break;
          case CVarValueType::String:
            s.detail = "string";
            break;
        }
        s.insert_text = s.label;
        matches.push_back(std::move(s));
      }
    });

    std::ranges::sort(matches, [](const ConsoleSuggestion& a, const ConsoleSuggestion& b) {
      return a.label < b.label;
    });

    constexpr size_t kMaxSuggestions = 10;
    for (size_t i = 0; i < matches.size() && i < kMaxSuggestions; i++) {
      out.push_back(std::move(matches[i]));
    }
  });
}

}  // namespace TENG_NAMESPACE
