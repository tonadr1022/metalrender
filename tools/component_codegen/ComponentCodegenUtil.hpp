#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "clang/AST/Decl.h"

struct CodegenError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct AnnotationArgs {
  std::map<std::string, std::string> values;

  [[nodiscard]] std::string required(std::string_view key, std::string_view context) const;
  [[nodiscard]] std::string optional(std::string_view key, std::string_view fallback) const;
  [[nodiscard]] bool optional_bool(std::string_view key, bool fallback) const;
  [[nodiscard]] uint32_t optional_u32(std::string_view key, uint32_t fallback) const;
  [[nodiscard]] int64_t required_i64(std::string_view key, std::string_view context) const;
  [[nodiscard]] std::optional<int64_t> try_i64(std::string_view key) const;
};

[[nodiscard]] std::string trim(std::string_view text);
[[nodiscard]] std::string unquote(std::string value);

[[nodiscard]] std::string infer_kind(const clang::FieldDecl& field);
