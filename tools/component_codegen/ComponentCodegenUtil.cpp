#include "ComponentCodegenUtil.hpp"

#include <charconv>

[[nodiscard]] uint32_t AnnotationArgs::optional_u32(std::string_view key, uint32_t fallback) const {
  const auto it = values.find(std::string{key});
  if (it == values.end()) {
    return fallback;
  }
  uint32_t value{};
  const char* begin = it->second.data();
  const char* end = begin + it->second.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    throw CodegenError("expected integer annotation value for '" + std::string{key} + "'");
  }
  return value;
}
[[nodiscard]] std::string AnnotationArgs::required(std::string_view key,
                                                   std::string_view context) const {
  const auto it = values.find(std::string{key});
  if (it == values.end() || it->second.empty()) {
    throw CodegenError(std::string{context} + " missing required annotation key '" +
                       std::string{key} + "'");
  }
  return it->second;
}
[[nodiscard]] std::string AnnotationArgs::optional(std::string_view key,
                                                   std::string_view fallback) const {
  const auto it = values.find(std::string{key});
  if (it == values.end()) {
    return std::string{fallback};
  }
  return it->second;
}
[[nodiscard]] bool AnnotationArgs::optional_bool(std::string_view key, bool fallback) const {
  const auto it = values.find(std::string{key});
  if (it == values.end()) {
    return fallback;
  }
  if (it->second == "true") {
    return true;
  }
  if (it->second == "false") {
    return false;
  }
  throw CodegenError("expected boolean annotation value for '" + std::string{key} + "'");
}
[[nodiscard]] int64_t AnnotationArgs::required_i64(std::string_view key,
                                                   std::string_view context) const {
  const std::string raw = required(key, context);
  int64_t value{};
  const char* begin = raw.data();
  const char* end = begin + raw.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    throw CodegenError(std::string{context} + " expected integer annotation value for '" +
                       std::string{key} + "'");
  }
  return value;
}
std::string trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(" \t\r\n");
  return std::string{text.substr(begin, end - begin + 1)};
}
std::string unquote(std::string value) {
  value = trim(value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    std::string out;
    out.reserve(value.size() - 2);
    for (size_t i = 1; i + 1 < value.size(); ++i) {
      if (value[i] == '\\' && i + 2 < value.size()) {
        ++i;
      }
      out.push_back(value[i]);
    }
    return out;
  }
  return value;
}

namespace {

[[nodiscard]] std::string normalize_type(clang::QualType type) {
  type = type.getCanonicalType();
  return type.getAsString();
}

}  // namespace

std::string infer_kind(const clang::FieldDecl& field) {
  const std::string type = normalize_type(field.getType());
  if (type == "bool" || type == "_Bool") {
    return "Bool";
  }
  if (type == "int" || type == "int32_t" || type == "std::int32_t") {
    return "I32";
  }
  if (type == "unsigned int" || type == "uint32_t" || type == "std::uint32_t") {
    return "U32";
  }
  if (type == "float") {
    return "F32";
  }
  if (type == "std::basic_string<char>" || type == "std::string") {
    return "String";
  }
  if (type.contains("glm::vec<2, float")) {
    return "Vec2";
  }
  if (type.contains("glm::vec<3, float")) {
    return "Vec3";
  }
  if (type.contains("glm::vec<4, float")) {
    return "Vec4";
  }
  if (type.contains("glm::qua<float")) {
    return "Quat";
  }
  if (type.contains("glm::mat<4, 4, float")) {
    return "Mat4";
  }
  if (type == "teng::engine::AssetId" || type == "struct teng::engine::AssetId") {
    return "AssetId";
  }
  if (const auto* enum_type = field.getType()->getAs<clang::EnumType>()) {
    (void)enum_type;
    return "Enum";
  }
  throw CodegenError("unsupported reflected field type '" + type + "' for member '" +
                     field.getNameAsString() + "'");
}
