#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/Config.hpp"

namespace TENG_NAMESPACE::core {

enum class DiagnosticSeverity : uint8_t {
  Info,
  Warning,
  Error,
};

[[nodiscard]] std::string_view to_string(DiagnosticSeverity severity);

class DiagnosticCode {
 public:
  DiagnosticCode() = default;
  explicit DiagnosticCode(std::string value);

  [[nodiscard]] const std::string& value() const { return value_; }
  [[nodiscard]] bool empty() const { return value_.empty(); }

  friend bool operator==(const DiagnosticCode&, const DiagnosticCode&) = default;

 private:
  std::string value_;
};

enum class DiagnosticPathSegmentKind : uint8_t {
  ObjectKey,
  ArrayIndex,
};

struct DiagnosticPathSegment {
  DiagnosticPathSegmentKind kind{};
  std::string value;
  size_t index{};

  [[nodiscard]] static DiagnosticPathSegment object_key(std::string key);
  [[nodiscard]] static DiagnosticPathSegment array_index(size_t index);
};

class DiagnosticPath {
 public:
  DiagnosticPath() = default;

  [[nodiscard]] bool empty() const { return segments_.empty(); }
  [[nodiscard]] const std::vector<DiagnosticPathSegment>& segments() const { return segments_; }

  DiagnosticPath& object_key(std::string key);
  DiagnosticPath& array_index(size_t index);

  [[nodiscard]] std::string render() const;

 private:
  std::vector<DiagnosticPathSegment> segments_;
};

struct Diagnostic {
  DiagnosticSeverity severity{DiagnosticSeverity::Error};
  DiagnosticCode code;
  DiagnosticPath path;
  std::string message;
};

class DiagnosticReport {
 public:
  void add(Diagnostic diagnostic);
  void add_error(DiagnosticCode code, DiagnosticPath path, std::string message);
  void add_warning(DiagnosticCode code, DiagnosticPath path, std::string message);
  void add_info(DiagnosticCode code, DiagnosticPath path, std::string message);

  [[nodiscard]] bool empty() const { return diagnostics_.empty(); }
  [[nodiscard]] bool has_errors() const;
  [[nodiscard]] size_t size() const { return diagnostics_.size(); }
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

  [[nodiscard]] std::string to_string() const;

 private:
  std::vector<Diagnostic> diagnostics_;
};

[[nodiscard]] std::string render_diagnostic(const Diagnostic& diagnostic);
[[nodiscard]] std::string render_diagnostic_report(const DiagnosticReport& report);

}  // namespace TENG_NAMESPACE::core
