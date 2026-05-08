#include <catch2/catch_test_macros.hpp>
#include <string>

#include "core/Diagnostic.hpp"

namespace teng::core {

// NOLINTBEGIN(misc-use-anonymous-namespace): Catch2 TEST_CASE expands to static functions.

TEST_CASE("diagnostic severity renders stable lowercase labels", "[diagnostic]") {
  CHECK(to_string(DiagnosticSeverity::Info) == "info");
  CHECK(to_string(DiagnosticSeverity::Warning) == "warning");
  CHECK(to_string(DiagnosticSeverity::Error) == "error");
}

TEST_CASE("diagnostic paths render generic object keys and array indices", "[diagnostic]") {
  DiagnosticPath path;
  path.object_key("entities")
      .array_index(2)
      .object_key("components")
      .object_key("teng.core.transform")
      .object_key("translation");

  CHECK(path.render() == "$.entities[2].components[\"teng.core.transform\"].translation");
}

TEST_CASE("diagnostic paths quote and escape non-identifier object keys", "[diagnostic]") {
  DiagnosticPath path;
  path.object_key("display name").object_key("quote\"and\\slash");

  CHECK(path.render() == "$[\"display name\"][\"quote\\\"and\\\\slash\"]");
}

TEST_CASE("diagnostic reports preserve order and detect errors", "[diagnostic]") {
  DiagnosticPath warning_path;
  warning_path.object_key("schema").object_key("components").object_key("teng.core.transform");

  DiagnosticPath error_path;
  error_path.object_key("entities").array_index(0).object_key("components");

  DiagnosticReport report;
  CHECK(report.empty());
  CHECK(!report.has_errors());

  report.add_info(DiagnosticCode{"schema.registry_loaded"}, warning_path, "registry is available");
  report.add_warning(DiagnosticCode{"schema.deprecated_field"}, warning_path,
                     "field is accepted for migration");
  CHECK(!report.empty());
  CHECK(!report.has_errors());
  CHECK(report.size() == 2);

  report.add_error(DiagnosticCode{"scene.unknown_component"}, error_path,
                   "component is not registered");
  REQUIRE(report.has_errors());
  REQUIRE(report.size() == 3);

  const Diagnostic& warning = report.diagnostics()[1];
  CHECK(warning.severity == DiagnosticSeverity::Warning);
  CHECK(warning.code == DiagnosticCode{"schema.deprecated_field"});
}

TEST_CASE("diagnostic reports render one diagnostic per line", "[diagnostic]") {
  DiagnosticPath warning_path;
  warning_path.object_key("schema")
      .object_key("required_components")
      .object_key("teng.core.transform");

  DiagnosticPath error_path;
  error_path.object_key("entities").array_index(0).object_key("components");

  DiagnosticReport report;
  report.add_warning(DiagnosticCode{"schema.deprecated_field"}, warning_path,
                     "field is accepted for migration");
  report.add_error(DiagnosticCode{"scene.unknown_component"}, error_path,
                   "component is not registered");

  const std::string expected =
      "warning schema.deprecated_field at "
      "$.schema.required_components[\"teng.core.transform\"]: "
      "field is accepted for migration\n"
      "error scene.unknown_component at "
      "$.entities[0].components: "
      "component is not registered";

  CHECK(render_diagnostic_report(report) == expected);
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::core
