#pragma once

// inspired by https://github.com/vblanco20-1/vulkan-guide/blob/engine/extra-engine/cvars.h

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "Hash.hpp"
#include "core/Config.hpp"

namespace TENG_NAMESPACE {

enum class CVarFlags : uint16_t {
  None = 0,
  NoEdit = 1 << 0,
  EditReadOnly = 1 << 1,
  Advanced = 1 << 2,
  EditCheckbox = 1 << 8,
  EditFloatDrag = 1 << 9,
};

class CVarParameter;

enum class CVarValueType : uint8_t {
  Int,
  Float,
  String,
};

struct CVarInfoView {
  const std::string& name;
  const std::string& description;
  CVarValueType type;
  CVarFlags flags;
};

enum class CVarApplyResult : uint8_t {
  Ok,
  NotFound,
  ReadOnly,
  InvalidValue,
};

struct AutoCVarInt;
struct AutoCVarFloat;
struct AutoCVarString;

class CVarSystem {
 public:
  static CVarSystem& get();
  virtual CVarParameter* get_cvar(util::hash::HashedString hash) = 0;
  virtual CVarParameter* create_float_cvar(const char* name, const char* description,
                                           float default_value, float current_value) = 0;
  virtual CVarParameter* create_int_cvar(const char* name, const char* description,
                                         int32_t default_value, int32_t current_value) = 0;
  virtual CVarParameter* create_string_cvar(const char* name, const char* description,
                                            const char* default_value,
                                            const char* current_value) = 0;
  virtual float* get_float_cvar(util::hash::HashedString hash) = 0;
  virtual void set_float_cvar(util::hash::HashedString hash, float value) = 0;
  virtual int32_t* get_int_cvar(util::hash::HashedString hash) = 0;
  virtual void set_int_cvar(util::hash::HashedString hash, int32_t value) = 0;
  // Pointer is valid only until the stored string is next assigned (reallocation may invalidate).
  virtual const char* get_string_cvar(util::hash::HashedString hash) = 0;
  virtual void set_string_cvar(util::hash::HashedString hash, const char* value) = 0;
  virtual void draw_imgui_editor() = 0;
  virtual void load_from_file(const std::string& path) = 0;
  virtual void save_to_file(const std::string& path) = 0;
  virtual void merge_cvar_flags(util::hash::HashedString hash, CVarFlags or_flags) = 0;
  virtual void for_each_cvar(std::function<void(const CVarInfoView&)> visitor) = 0;
  virtual CVarApplyResult set_cvar_from_string(std::string_view name, std::string_view value,
                                               std::string* error_msg) = 0;
  /// Fired after a successful value change via set_*_cvar / AutoCVar::set (not when mutating
  /// through get_*_cvar / get_ptr()).
  virtual void add_change_callback(util::hash::HashedString name, std::function<void()> cb) = 0;
  virtual void add_change_callback(const AutoCVarInt& cv, std::function<void()> cb) = 0;
  virtual void add_change_callback(const AutoCVarFloat& cv, std::function<void()> cb) = 0;
  virtual void add_change_callback(const AutoCVarString& cv, std::function<void()> cb) = 0;
};

template <typename T>
struct AutoCVar {
 public:
  [[nodiscard]] util::hash::HashedString hashed_name() const {
    return util::hash::HashedString{name_hash_};
  }

 protected:
  uint32_t idx_{};
  uint32_t name_hash_{};
};

struct AutoCVarInt : AutoCVar<int32_t> {
  AutoCVarInt(const char* name, const char* desc, int initial_value,
              CVarFlags flags = CVarFlags::None);
  int32_t get();
  /// Raw storage; writes do not run change callbacks — prefer set() or CVarSystem::set_int_cvar.
  int32_t* get_ptr();
  void set(int32_t val);
};

struct AutoCVarFloat : AutoCVar<float> {
  AutoCVarFloat(const char* name, const char* description, float default_value,
                CVarFlags flags = CVarFlags::None);
  float get();
  /// Raw storage; writes do not run change callbacks — prefer set() or CVarSystem::set_float_cvar.
  float* get_ptr();
  void set(float val);
};

struct AutoCVarString : AutoCVar<std::string> {
  AutoCVarString(const char* name, const char* description, const char* default_value,
                 CVarFlags flags = CVarFlags::None);
  // Same lifetime caveats as CVarSystem::get_string_cvar.
  const char* get();
  void set(std::string_view val);
  void set(std::string&& val);
};

class Console;
void register_cvar_console(Console& console);

}  // namespace TENG_NAMESPACE
