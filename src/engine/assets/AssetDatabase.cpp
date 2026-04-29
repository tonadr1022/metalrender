#include "engine/assets/AssetDatabase.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>

#include "core/EAssert.hpp"
#include "core/Result.hpp"
#include "core/TomlUtil.hpp"

namespace teng::engine::assets {
namespace {

constexpr uint32_t k_schema_version = 1;
constexpr uint64_t k_fnv_offset_basis = 14695981039346656037ull;
constexpr uint64_t k_fnv_prime = 1099511628211ull;

[[nodiscard]] std::string generic_normalized(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

[[nodiscard]] std::optional<AssetDependencyKind> parse_dependency_kind(std::string_view text) {
  if (text == "strong") {
    return AssetDependencyKind::Strong;
  }
  if (text == "soft") {
    return AssetDependencyKind::Soft;
  }
  if (text == "generated") {
    return AssetDependencyKind::Generated;
  }
  if (text == "tooling") {
    return AssetDependencyKind::Tooling;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<AssetRecordStatus> parse_status(std::string_view text) {
  if (text == "available") {
    return AssetRecordStatus::Available;
  }
  if (text == "missing-source") {
    return AssetRecordStatus::MissingSource;
  }
  if (text == "missing-metadata") {
    return AssetRecordStatus::MissingMetadata;
  }
  if (text == "tombstoned") {
    return AssetRecordStatus::Tombstoned;
  }
  return std::nullopt;
}

[[nodiscard]] uint64_t fnv1a_bytes(std::istream& stream) {
  uint64_t hash = k_fnv_offset_basis;
  char buffer[4096];
  while (stream) {
    stream.read(buffer, sizeof(buffer));
    const std::streamsize read = stream.gcount();
    for (std::streamsize i = 0; i < read; ++i) {
      hash ^= static_cast<unsigned char>(buffer[i]);
      hash *= k_fnv_prime;
    }
  }
  return hash == 0 ? 1 : hash;
}

[[nodiscard]] std::string hash_to_string(uint64_t hash) {
  std::ostringstream out;
  out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
  return out.str();
}

template <class T>
[[nodiscard]] Result<T> required(const toml::table& table, std::string_view key) {
  std::optional<T> value = table[key].value<T>();
  if (!value) {
    return make_unexpected("required field '" + std::string(key) + "' is missing or wrong type");
  }
  return *value;
}

[[nodiscard]] Result<uint32_t> required_u32(const toml::table& table, std::string_view key) {
  const Result<int64_t> value = required<int64_t>(table, key);
  if (!value) {
    return make_unexpected(value.error());
  }
  if (*value < 0 || *value > static_cast<int64_t>(UINT32_MAX)) {
    return make_unexpected("required field '" + std::string(key) +
                           "' must be an unsigned 32-bit integer");
  }
  return static_cast<uint32_t>(*value);
}

[[nodiscard]] Result<void> require_schema_version(uint32_t schema_version) {
  if (schema_version != k_schema_version) {
    return make_unexpected("schema_version must be " + std::to_string(k_schema_version));
  }
  return {};
}

struct SidecarRequiredFields {
  std::string id;
  std::string type;
  std::string source_path;
  std::string display_name;
  std::string importer;
  uint32_t importer_version{};
  std::string source_content_hash;
};

[[nodiscard]] Result<SidecarRequiredFields> parse_sidecar_required_fields(
    const toml::table& table) {
  Result<uint32_t> schema_version = required_u32(table, "schema_version");
  REQUIRED_OR_RETURN(schema_version);

  if (Result<void> schema_result = require_schema_version(*schema_version); !schema_result) {
    return make_unexpected(schema_result.error());
  }

  Result<std::string> id = required<std::string>(table, "id");
  REQUIRED_OR_RETURN(id);

  Result<std::string> type = required<std::string>(table, "type");
  REQUIRED_OR_RETURN(type);

  Result<std::string> source_path = required<std::string>(table, "source_path");
  REQUIRED_OR_RETURN(source_path);

  Result<std::string> display_name = required<std::string>(table, "display_name");
  REQUIRED_OR_RETURN(display_name);

  Result<std::string> importer = required<std::string>(table, "importer");
  REQUIRED_OR_RETURN(importer);

  Result<uint32_t> importer_version = required_u32(table, "importer_version");
  REQUIRED_OR_RETURN(importer_version);

  Result<std::string> source_content_hash = required<std::string>(table, "source_content_hash");
  REQUIRED_OR_RETURN(source_content_hash);

  return SidecarRequiredFields{
      .id = std::move(*id),
      .type = std::move(*type),
      .source_path = std::move(*source_path),
      .display_name = std::move(*display_name),
      .importer = std::move(*importer),
      .importer_version = *importer_version,
      .source_content_hash = std::move(*source_content_hash),
  };
}

[[nodiscard]] Result<AssetDependency> parse_dependency_table(const toml::table& table) {
  Result<std::string> id_text = required<std::string>(table, "id");
  REQUIRED_OR_RETURN(id_text);
  Result<std::string> kind_text = required<std::string>(table, "kind");
  REQUIRED_OR_RETURN(kind_text);

  const std::optional<AssetId> id = AssetId::parse(*id_text);
  const std::optional<AssetDependencyKind> kind = parse_dependency_kind(*kind_text);
  if (!id || !kind) {
    return make_unexpected("dependency entry has invalid id or kind");
  }
  return AssetDependency{.asset = *id, .kind = *kind};
}

[[nodiscard]] toml::table make_dependency_table(const AssetDependency& dependency) {
  toml::table table{{"id", dependency.asset.to_string()},
                    {"kind", std::string(to_string(dependency.kind))}};
  table.is_inline(true);
  return table;
}

[[nodiscard]] toml::array make_dependency_array(const std::vector<AssetDependency>& dependencies) {
  toml::array array;
  for (const AssetDependency& dependency : dependencies) {
    array.push_back(make_dependency_table(dependency));
  }
  return array;
}

[[nodiscard]] toml::array make_string_array(const std::vector<std::string>& values) {
  toml::array array;
  for (const std::string& value : values) {
    array.push_back(value);
  }
  return array;
}

void add_diagnostic(AssetScanReport& report, AssetDiagnosticKind kind,
                    const std::filesystem::path& path, const std::string& message,
                    AssetId asset = {}, AssetId referenced_asset = {}) {
  report.diagnostics.push_back(AssetDiagnostic{.kind = kind,
                                               .path = path,
                                               .asset = asset,
                                               .referenced_asset = referenced_asset,
                                               .message = message});
}

}  // namespace

size_t AssetScanReport::count(AssetDiagnosticKind kind) const {
  return static_cast<size_t>(std::ranges::count_if(
      diagnostics, [kind](const AssetDiagnostic& diagnostic) { return diagnostic.kind == kind; }));
}

AssetDatabase::AssetDatabase(AssetDatabaseConfig config) : config_(std::move(config)) {}

AssetScanReport AssetDatabase::scan() {
  registry_ = AssetRegistry{};
  AssetScanReport report;
  const std::filesystem::path root = absolute_content_root();
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return report;
  }

  std::vector<std::filesystem::path> sidecars;
  std::vector<std::filesystem::path> sources;
  for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(
           root, std::filesystem::directory_options::follow_directory_symlink, ec)) {
    if (ec || !entry.is_regular_file(ec)) {
      continue;
    }
    const std::filesystem::path& path = entry.path();
    const std::string filename = path.filename().generic_string();
    const std::filesystem::path rel = path.lexically_relative(root).lexically_normal();
    if (rel.generic_string().starts_with("local/asset_cache/")) {
      continue;
    }
    if (filename.ends_with(config_.sidecar_extension)) {
      sidecars.push_back(path);
    } else {
      sources.push_back(path);
    }
  }

  for (const std::filesystem::path& sidecar : sidecars) {
    std::optional<AssetRecord> record = load_sidecar(sidecar, report);
    if (!record) {
      continue;
    }
    const AssetRegistryResult result = registry_.add_record(*record);
    if (result == AssetRegistryResult::DuplicateAssetId) {
      add_diagnostic(report, AssetDiagnosticKind::DuplicateAssetId, sidecar,
                     "duplicate asset id " + record->id.to_string(), record->id);
      continue;
    }
    if (result != AssetRegistryResult::Ok) {
      add_diagnostic(report, AssetDiagnosticKind::SchemaError, sidecar,
                     "asset registry rejected record: " + std::string(to_string(result)),
                     record->id);
    }
  }

  for (const std::filesystem::path& source : sources) {
    const std::filesystem::path rel = relative_source_path(source);
    if (!registry_.asset_id_for_source_path(rel)) {
      add_diagnostic(report, AssetDiagnosticKind::MissingMetadata, rel,
                     "source file has no asset metadata sidecar");
    }
  }

  add_dependency_diagnostics(report);
  write_aggregate_index_best_effort();
  return report;
}

AssetRegistryResult AssetDatabase::register_source(const AssetRegisterSourceDesc& desc,
                                                   AssetId* out_id) {
  const std::filesystem::path rel_source = relative_source_path(desc.source_path);
  const std::filesystem::path abs_source = absolute_source_path(rel_source);
  if (!is_under_content_root(abs_source) || !std::filesystem::is_regular_file(abs_source)) {
    return AssetRegistryResult::MissingAsset;
  }
  if (!desc.type.is_valid()) {
    return AssetRegistryResult::InvalidAssetType;
  }

  AssetRecord record;
  record.id = desc.id.value_or(make_asset_id());
  record.type = desc.type;
  record.source_path = rel_source;
  record.display_name = desc.display_name.empty() ? rel_source.stem().string() : desc.display_name;
  record.importer = desc.importer;
  record.importer_version = desc.importer_version;
  record.source_content_hash = hash_source_file(abs_source);
  record.dependencies = desc.dependencies;
  if (!record.id.is_valid()) {
    return AssetRegistryResult::InvalidAssetId;
  }
  if (registry_.contains(record.id)) {
    return AssetRegistryResult::DuplicateAssetId;
  }
  if (!save_sidecar(record, desc.labels)) {
    return AssetRegistryResult::MissingAsset;
  }
  [[maybe_unused]] const AssetRegistryResult result = registry_.add_record(record);
  ASSERT(result == AssetRegistryResult::Ok);
  write_aggregate_index_best_effort();
  if (out_id) {
    *out_id = record.id;
  }
  return AssetRegistryResult::Ok;
}

AssetRegistryResult AssetDatabase::move_asset(AssetId id, const std::filesystem::path& new_path) {
  AssetRecord* record = registry_.find(id);
  if (!record) {
    return AssetRegistryResult::MissingAsset;
  }
  const std::filesystem::path old_rel = record->source_path;
  const std::filesystem::path old_abs = absolute_source_path(old_rel);
  const std::filesystem::path new_rel = relative_source_path(new_path);
  const std::filesystem::path new_abs = absolute_source_path(new_rel);
  if (!is_under_content_root(new_abs)) {
    return AssetRegistryResult::MissingAsset;
  }

  std::error_code ec;
  std::filesystem::create_directories(new_abs.parent_path(), ec);
  if (ec) {
    return AssetRegistryResult::MissingAsset;
  }
  std::filesystem::rename(old_abs, new_abs, ec);
  if (ec) {
    return AssetRegistryResult::MissingAsset;
  }

  const AssetRecord old_record = *record;
  const std::filesystem::path old_sidecar = sidecar_path_for_source(old_rel);
  const std::filesystem::path new_sidecar = sidecar_path_for_source(new_rel);
  bool moved_sidecar = false;
  std::filesystem::create_directories(new_sidecar.parent_path(), ec);
  if (ec) {
    std::filesystem::rename(new_abs, old_abs, ec);
    return AssetRegistryResult::MissingAsset;
  }
  if (std::filesystem::exists(old_sidecar, ec)) {
    std::filesystem::rename(old_sidecar, new_sidecar, ec);
    if (ec) {
      std::filesystem::rename(new_abs, old_abs, ec);
      return AssetRegistryResult::MissingAsset;
    }
    moved_sidecar = true;
  }

  record->source_path = new_rel;
  record->source_content_hash = hash_source_file(new_abs);
  if (!save_sidecar(*record)) {
    *record = old_record;
    if (moved_sidecar) {
      std::filesystem::rename(new_sidecar, old_sidecar, ec);
    }
    std::filesystem::rename(new_abs, old_abs, ec);
    return AssetRegistryResult::MissingAsset;
  }
  path_redirects_[generic_normalized(old_rel)] = id;
  write_aggregate_index_best_effort();
  return AssetRegistryResult::Ok;
}

AssetDeleteResult AssetDatabase::delete_asset(AssetId id, bool force) {
  AssetDeleteResult result;
  AssetRecord* record = registry_.find(id);
  if (!record) {
    result.diagnostics.push_back(
        AssetDiagnostic{.kind = AssetDiagnosticKind::BrokenReference, .asset = id});
    return result;
  }

  for (const AssetDependency& dependent : registry_.dependents(id)) {
    if (dependent.kind == AssetDependencyKind::Strong) {
      result.blocking_dependents.push_back(dependent);
    }
  }
  if (!force && !result.blocking_dependents.empty()) {
    return result;
  }

  const AssetRecord old_record = *record;
  AssetRecord tombstoned = old_record;
  tombstoned.status = AssetRecordStatus::Tombstoned;
  if (!save_sidecar(tombstoned)) {
    result.diagnostics.push_back(AssetDiagnostic{.kind = AssetDiagnosticKind::SchemaError,
                                                 .path = tombstoned.source_path,
                                                 .asset = id,
                                                 .message = "failed to save tombstone sidecar"});
    return result;
  }

  std::error_code ec;
  std::filesystem::remove(absolute_source_path(old_record.source_path), ec);
  if (ec) {
    (void)save_sidecar(old_record);
    result.diagnostics.push_back(AssetDiagnostic{.kind = AssetDiagnosticKind::MissingSource,
                                                 .path = old_record.source_path,
                                                 .asset = id,
                                                 .message = "failed to remove asset source"});
    return result;
  }

  record->status = AssetRecordStatus::Tombstoned;
  write_aggregate_index_best_effort();
  result.deleted = true;
  for (const AssetDependency& dependent : registry_.dependents(id)) {
    result.diagnostics.push_back(
        AssetDiagnostic{.kind = AssetDiagnosticKind::BrokenReference,
                        .asset = dependent.asset,
                        .referenced_asset = id,
                        .message = "dependent references tombstoned asset"});
  }
  return result;
}

AssetRedirectFixupReport AssetDatabase::fixup_redirects() {
  AssetRedirectFixupReport report;
  for (auto it = path_redirects_.begin(); it != path_redirects_.end();) {
    const AssetRecord* target = registry_.find(it->second);
    if (!target || target->status == AssetRecordStatus::Tombstoned) {
      report.diagnostics.push_back(AssetDiagnostic{.kind = AssetDiagnosticKind::UnresolvedRedirect,
                                                   .path = it->first,
                                                   .asset = it->second,
                                                   .message = "path redirect target is missing"});
      ++it;
      continue;
    }
    it = path_redirects_.erase(it);
    ++report.collapsed_redirects;
  }

  for (const auto& [from, to] : registry_.redirects()) {
    const AssetId resolved = *registry_.resolve_redirect(from);
    if (!registry_.find(resolved)) {
      report.diagnostics.push_back(AssetDiagnostic{.kind = AssetDiagnosticKind::RedirectCycle,
                                                   .asset = from,
                                                   .referenced_asset = to,
                                                   .message = "asset redirect is unresolved"});
    }
  }
  write_aggregate_index_best_effort();
  return report;
}

std::optional<std::filesystem::path> AssetDatabase::source_path(AssetId id) const {
  return registry_.source_path(id);
}

std::optional<AssetId> AssetDatabase::asset_id_for_source_path(
    const std::filesystem::path& source_path) const {
  const std::filesystem::path rel = relative_source_path(source_path);
  if (const std::optional<AssetId> id = registry_.asset_id_for_source_path(rel)) {
    return id;
  }
  return resolve_path_redirect(rel);
}

std::optional<AssetId> AssetDatabase::resolve_path_redirect(
    const std::filesystem::path& source_path) const {
  const auto it = path_redirects_.find(generic_normalized(relative_source_path(source_path)));
  if (it == path_redirects_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::filesystem::path AssetDatabase::absolute_content_root() const {
  if (config_.content_root.is_absolute()) {
    return config_.content_root.lexically_normal();
  }
  return (config_.project_root / config_.content_root).lexically_normal();
}

std::filesystem::path AssetDatabase::absolute_cache_path() const {
  if (config_.aggregate_cache_path.is_absolute()) {
    return config_.aggregate_cache_path.lexically_normal();
  }
  return (config_.project_root / config_.aggregate_cache_path).lexically_normal();
}

std::filesystem::path AssetDatabase::absolute_source_path(
    const std::filesystem::path& source_path) const {
  if (source_path.is_absolute()) {
    return source_path.lexically_normal();
  }
  return (absolute_content_root() / source_path).lexically_normal();
}

std::filesystem::path AssetDatabase::relative_source_path(
    const std::filesystem::path& source_path) const {
  std::filesystem::path abs;
  if (source_path.is_absolute()) {
    abs = source_path.lexically_normal();
  } else {
    const std::filesystem::path project_relative =
        (config_.project_root / source_path).lexically_normal();
    abs = is_under_content_root(project_relative)
              ? project_relative
              : (absolute_content_root() / source_path).lexically_normal();
  }
  return abs.lexically_relative(absolute_content_root()).lexically_normal();
}

std::filesystem::path AssetDatabase::sidecar_path_for_source(
    const std::filesystem::path& source_path) const {
  const std::filesystem::path abs_source = absolute_source_path(source_path);
  return abs_source.parent_path() /
         (abs_source.filename().generic_string() + config_.sidecar_extension);
}

bool AssetDatabase::is_under_content_root(const std::filesystem::path& path) const {
  const std::filesystem::path root = absolute_content_root().lexically_normal();
  const std::filesystem::path abs = path.lexically_normal();
  const std::string root_string = root.generic_string();
  const std::string abs_string = abs.generic_string();
  return abs_string == root_string || abs_string.starts_with(root_string + "/");
}

std::optional<AssetRecord> AssetDatabase::load_sidecar(const std::filesystem::path& sidecar_path,
                                                       AssetScanReport& report) const {
  Result<toml::table> parsed_table = parse_toml_file(sidecar_path);
  if (!parsed_table) {
    add_diagnostic(report, AssetDiagnosticKind::TomlParseError, sidecar_path, parsed_table.error());
    return std::nullopt;
  }
  toml::table table = std::move(*parsed_table);

  Result<SidecarRequiredFields> parsed_fields = parse_sidecar_required_fields(table);
  if (!parsed_fields) {
    add_diagnostic(report, AssetDiagnosticKind::SchemaError, sidecar_path, parsed_fields.error());
    return std::nullopt;
  }
  const SidecarRequiredFields fields = std::move(*parsed_fields);

  const std::optional<AssetId> id = AssetId::parse(fields.id);
  if (!id) {
    add_diagnostic(report, AssetDiagnosticKind::SchemaError, sidecar_path,
                   "asset sidecar has invalid id");
    return std::nullopt;
  }

  AssetRecord record;
  record.id = *id;
  record.type = {.value = fields.type};
  record.source_path = std::filesystem::path(fields.source_path).lexically_normal();
  record.display_name = fields.display_name;
  record.importer = fields.importer;
  record.importer_version = fields.importer_version;
  record.source_content_hash = fields.source_content_hash;
  record.imported_artifact_hash = table["imported_artifact_hash"].value_or<std::string>("");
  const std::optional<AssetRecordStatus> status =
      parse_status(table["status"].value_or<std::string>("available"));
  if (!status) {
    add_diagnostic(report, AssetDiagnosticKind::SchemaError, sidecar_path,
                   "asset sidecar has invalid status", record.id);
    return std::nullopt;
  }
  record.status = *status;

  if (const toml::array* deps = table["dependencies"].as_array()) {
    for (const toml::node& dep_node : *deps) {
      const toml::table* dep_table = dep_node.as_table();
      if (!dep_table) {
        add_diagnostic(report, AssetDiagnosticKind::SchemaError, sidecar_path,
                       "dependency entry is not an inline table", record.id);
        continue;
      }
      Result<AssetDependency> dependency = parse_dependency_table(*dep_table);
      if (!dependency) {
        add_diagnostic(report, AssetDiagnosticKind::SchemaError, sidecar_path, dependency.error(),
                       record.id);
        continue;
      }
      record.dependencies.push_back(*dependency);
    }
  }

  const std::string sidecar_filename = sidecar_path.filename().generic_string();
  const std::string source_filename =
      sidecar_filename.substr(0, sidecar_filename.size() - config_.sidecar_extension.size());
  const std::filesystem::path actual_source_abs = sidecar_path.parent_path() / source_filename;
  const std::filesystem::path actual_source_rel = relative_source_path(actual_source_abs);
  if (actual_source_rel.lexically_normal() != record.source_path.lexically_normal()) {
    add_diagnostic(report, AssetDiagnosticKind::SourcePathMismatch, sidecar_path,
                   "sidecar source_path does not match sidecar/source location", record.id);
  }
  if (!std::filesystem::exists(actual_source_abs) &&
      record.status != AssetRecordStatus::Tombstoned) {
    record.status = AssetRecordStatus::MissingSource;
    add_diagnostic(report, AssetDiagnosticKind::MissingSource, record.source_path,
                   "asset metadata points at a missing source file", record.id);
  } else if (record.status != AssetRecordStatus::Tombstoned) {
    const std::string current_hash = hash_source_file(actual_source_abs);
    if (current_hash != record.source_content_hash) {
      add_diagnostic(report, AssetDiagnosticKind::StaleSourceHash, record.source_path,
                     "asset source hash differs from sidecar metadata", record.id);
    }
  }

  return record;
}

bool AssetDatabase::save_sidecar(const AssetRecord& record,
                                 const std::vector<std::string>& labels) const {
  const std::filesystem::path sidecar = sidecar_path_for_source(record.source_path);
  std::error_code ec;
  std::filesystem::create_directories(sidecar.parent_path(), ec);
  std::ofstream out(sidecar);
  if (!out) {
    return false;
  }

  const toml::table table{
      {"schema_version", static_cast<int64_t>(k_schema_version)},
      {"id", record.id.to_string()},
      {"type", record.type.value},
      {"source_path", generic_normalized(record.source_path)},
      {"display_name", record.display_name},
      {"importer", record.importer},
      {"importer_version", static_cast<int64_t>(record.importer_version)},
      {"source_content_hash", record.source_content_hash},
      {"imported_artifact_hash", record.imported_artifact_hash},
      {"status", std::string(to_string(record.status))},
      {"dependencies", make_dependency_array(record.dependencies)},
      {"labels", make_string_array(labels)},
  };
  out << table << '\n';
  return true;
}

void AssetDatabase::write_aggregate_index_best_effort() const {
  const std::filesystem::path cache = absolute_cache_path();
  std::error_code ec;
  std::filesystem::create_directories(cache.parent_path(), ec);
  std::ofstream out(cache);
  if (!out) {
    return;
  }

  toml::array assets;
  for (const AssetId id : registry_.records()) {
    const AssetRecord& record = *registry_.find(id);
    assets.push_back(toml::table{
        {"id", id.to_string()},
        {"type", record.type.value},
        {"source_path", generic_normalized(record.source_path)},
        {"status", std::string(to_string(record.status))},
    });
  }

  toml::array path_redirects;
  for (const auto& [path, id] : path_redirects_) {
    path_redirects.push_back(toml::table{{"from", path}, {"to", id.to_string()}});
  }

  const toml::table table{
      {"schema_version", static_cast<int64_t>(k_schema_version)},
      {"generated", true},
      {"assets", std::move(assets)},
      {"path_redirects", std::move(path_redirects)},
  };
  out << table << '\n';
}

std::string AssetDatabase::hash_source_file(const std::filesystem::path& source_path) const {
  std::ifstream in(source_path, std::ios::binary);
  if (!in) {
    return {};
  }
  return hash_to_string(fnv1a_bytes(in));
}

void AssetDatabase::add_dependency_diagnostics(AssetScanReport& report) const {
  for (const AssetId id : registry_.records()) {
    const AssetRecord& record = *registry_.find(id);
    for (const AssetDependency& dependency : record.dependencies) {
      const AssetRecord* target = registry_.find(dependency.asset);
      if (!target || target->status == AssetRecordStatus::Tombstoned) {
        add_diagnostic(report, AssetDiagnosticKind::BrokenReference, record.source_path,
                       "asset dependency points at a missing or tombstoned asset", id,
                       dependency.asset);
      }
    }
  }
}

std::string_view to_string(AssetDiagnosticKind kind) {
  switch (kind) {
    case AssetDiagnosticKind::DuplicateAssetId:
      return "duplicate-asset-id";
    case AssetDiagnosticKind::MissingSource:
      return "missing-source";
    case AssetDiagnosticKind::MissingMetadata:
      return "missing-metadata";
    case AssetDiagnosticKind::StaleSourceHash:
      return "stale-source-hash";
    case AssetDiagnosticKind::SourcePathMismatch:
      return "source-path-mismatch";
    case AssetDiagnosticKind::TomlParseError:
      return "toml-parse-error";
    case AssetDiagnosticKind::SchemaError:
      return "schema-error";
    case AssetDiagnosticKind::BrokenReference:
      return "broken-reference";
    case AssetDiagnosticKind::RedirectCycle:
      return "redirect-cycle";
    case AssetDiagnosticKind::UnresolvedRedirect:
      return "unresolved-redirect";
  }
  return "unknown";
}

}  // namespace teng::engine::assets
