#include "core/Diagnostic.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace TENG_NAMESPACE::core {

namespace {

[[nodiscard]] bool is_identifier(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  const auto is_alpha = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  if (!is_alpha(value.front())) {
    return false;
  }
  return std::ranges::all_of(value.substr(1), [&](char c) { return is_alpha(c) || is_digit(c); });
}

void append_object_key(std::string& out, std::string_view key) {
  if (is_identifier(key)) {
    out += '.';
    out += key;
    return;
  }

  out += "[\"";
  for (const char c : key) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  out += "\"]";
}

}  // namespace

std::string_view to_string(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Info:
      return "info";
    case DiagnosticSeverity::Warning:
      return "warning";
    case DiagnosticSeverity::Error:
      return "error";
  }
  return "unknown";
}

DiagnosticCode::DiagnosticCode(std::string value) : value_(std::move(value)) {}

DiagnosticPathSegment DiagnosticPathSegment::object_key(std::string key) {
  return DiagnosticPathSegment{.kind = DiagnosticPathSegmentKind::ObjectKey,
                               .value = std::move(key)};
}

DiagnosticPathSegment DiagnosticPathSegment::array_index(size_t index) {
  return DiagnosticPathSegment{.kind = DiagnosticPathSegmentKind::ArrayIndex, .index = index};
}

DiagnosticPath& DiagnosticPath::object_key(std::string key) {
  segments_.push_back(DiagnosticPathSegment::object_key(std::move(key)));
  return *this;
}

DiagnosticPath& DiagnosticPath::array_index(size_t index) {
  segments_.push_back(DiagnosticPathSegment::array_index(index));
  return *this;
}

std::string DiagnosticPath::render() const {
  std::string out{"$"};
  for (const DiagnosticPathSegment& segment : segments_) {
    switch (segment.kind) {
      case DiagnosticPathSegmentKind::ObjectKey:
        append_object_key(out, segment.value);
        break;
      case DiagnosticPathSegmentKind::ArrayIndex:
        out += '[';
        out += std::to_string(segment.index);
        out += ']';
        break;
    }
  }
  return out;
}

void DiagnosticReport::add(Diagnostic diagnostic) { diagnostics_.push_back(std::move(diagnostic)); }

void DiagnosticReport::add_error(DiagnosticCode code, DiagnosticPath path, std::string message) {
  add(Diagnostic{.severity = DiagnosticSeverity::Error,
                 .code = std::move(code),
                 .path = std::move(path),
                 .message = std::move(message)});
}

void DiagnosticReport::add_warning(DiagnosticCode code, DiagnosticPath path, std::string message) {
  add(Diagnostic{.severity = DiagnosticSeverity::Warning,
                 .code = std::move(code),
                 .path = std::move(path),
                 .message = std::move(message)});
}

void DiagnosticReport::add_info(DiagnosticCode code, DiagnosticPath path, std::string message) {
  add(Diagnostic{.severity = DiagnosticSeverity::Info,
                 .code = std::move(code),
                 .path = std::move(path),
                 .message = std::move(message)});
}

bool DiagnosticReport::has_errors() const {
  return std::ranges::any_of(diagnostics_, [](const Diagnostic& diagnostic) {
    return diagnostic.severity == DiagnosticSeverity::Error;
  });
}

std::string render_diagnostic(const Diagnostic& diagnostic) {
  std::string out;
  out += to_string(diagnostic.severity);
  out += ' ';
  out += diagnostic.code.value();
  out += " at ";
  out += diagnostic.path.render();
  out += ": ";
  out += diagnostic.message;
  return out;
}

std::string render_diagnostic_report(const DiagnosticReport& report) {
  std::ostringstream out;
  for (size_t i = 0; i < report.diagnostics().size(); ++i) {
    if (i != 0) {
      out << '\n';
    }
    out << render_diagnostic(report.diagnostics()[i]);
  }
  return out.str();
}

}  // namespace TENG_NAMESPACE::core
