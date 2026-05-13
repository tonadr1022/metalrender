#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ComponentCodegenUtil.hpp"

namespace {

namespace fs = std::filesystem;

struct Options {
  fs::path out_dir;
  fs::path manifest_path;
  std::string module_name;
  std::string basename{"components.generated"};
  std::string cpp_namespace{"teng::component_generated"};
  std::string function_prefix{"component"};
  std::vector<std::string> includes;
  std::vector<std::string> headers;
  std::vector<std::string> compile_args;
};

struct EnumValue {
  std::string enumerator_expr;
  std::string key;
  int64_t stable_value{};
};

struct Field {
  std::string member;
  std::string key;
  std::string kind;
  std::string script_exposure{"None"};
  std::string asset_kind;
  std::string enum_key;
  std::string enum_type;
  std::vector<EnumValue> enum_values;
};

struct Component {
  std::string cpp_type;
  std::string component_key;
  std::string module_id;
  uint32_t module_version{1};
  uint32_t schema_version{1};
  std::string storage{"Authored"};
  std::string visibility{"Editable"};
  bool add_on_create{};
  std::vector<Field> fields;
};

[[nodiscard]] std::vector<std::string> split_top_level_commas(std::string_view text) {
  std::vector<std::string> items;
  size_t start = 0;
  bool in_string = false;
  bool escape = false;
  int paren_depth = 0;
  int brace_depth = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '(') {
      ++paren_depth;
    } else if (c == ')') {
      --paren_depth;
    } else if (c == '{') {
      ++brace_depth;
    } else if (c == '}') {
      --brace_depth;
    } else if (c == ',' && paren_depth == 0 && brace_depth == 0) {
      items.push_back(trim(text.substr(start, i - start)));
      start = i + 1;
    }
  }
  const std::string tail = trim(text.substr(start));
  if (!tail.empty()) {
    items.push_back(tail);
  }
  return items;
}

[[nodiscard]] AnnotationArgs parse_args_payload(std::string_view payload) {
  AnnotationArgs args;
  for (const std::string& item : split_top_level_commas(payload)) {
    const size_t eq = item.find('=');
    if (eq == std::string::npos) {
      throw CodegenError("annotation item missing '=': " + item);
    }
    args.values.emplace(trim(std::string_view{item}.substr(0, eq)),
                        unquote(trim(std::string_view{item}.substr(eq + 1))));
  }
  return args;
}

[[nodiscard]] std::optional<AnnotationArgs> find_annotation(const clang::Decl& decl,
                                                            std::string_view prefix) {
  for (const clang::Attr* attr : decl.attrs()) {
    const auto* annotate = llvm::dyn_cast<clang::AnnotateAttr>(attr);
    if (!annotate) {
      continue;
    }
    const std::string annotation = annotate->getAnnotation().str();
    if (annotation.starts_with(prefix)) {
      return parse_args_payload(std::string_view{annotation}.substr(prefix.size()));
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::string qualified_name(const clang::NamedDecl& decl) {
  return decl.getQualifiedNameAsString();
}

[[nodiscard]] std::string c_ident(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  return out;
}

[[nodiscard]] std::string cpp_string(std::string_view text) {
  std::string out{"\""};
  for (const char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  out.push_back('"');
  return out;
}

[[nodiscard]] const clang::EnumDecl& enum_decl_for_field(const clang::FieldDecl& field) {
  const auto* enum_type = field.getType()->getAs<clang::EnumType>();
  if (!enum_type) {
    throw CodegenError("field '" + field.getNameAsString() + "' is not an enum");
  }
  return *enum_type->getDecl()->getDefinition();
}

class Collector : public clang::RecursiveASTVisitor<Collector> {
 public:
  explicit Collector(clang::ASTContext& context) : context_(context) {}

  // RecursiveASTVisitor's CRTP API requires overriding the same-named base method.
  bool VisitCXXRecordDecl(  // NOLINT(bugprone-derived-method-shadowing-base-method)
      clang::CXXRecordDecl* record) {
    if (!record->isThisDeclarationADefinition() || record->isImplicit()) {
      return true;
    }
    auto component_annotation = find_annotation(*record, "teng.component:");
    if (!component_annotation) {
      return true;
    }

    Component component;
    component.cpp_type = qualified_name(*record);
    component.component_key = component_annotation->required("key", component.cpp_type);
    component.module_id = component_annotation->required("module", component.cpp_type);
    component.module_version = component_annotation->optional_u32("module_version", 1);
    component.schema_version = component_annotation->optional_u32("schema_version", 1);
    component.storage = component_annotation->optional("storage", "Authored");
    component.visibility = component_annotation->optional("visibility", "Editable");
    component.add_on_create = component_annotation->optional_bool("add_on_create", false);

    validate_component_policy(component);

    std::set<std::string> field_keys;
    for (const clang::FieldDecl* field_decl : record->fields()) {
      auto field_annotation = find_annotation(*field_decl, "teng.field:");
      if (!field_annotation) {
        continue;
      }

      Field field;
      field.member = field_decl->getNameAsString();
      field.key = field_annotation->optional("key", field.member);
      field.kind = infer_kind(*field_decl);
      field.script_exposure = field_annotation->optional("script", "None");
      validate_script(field.script_exposure, component.component_key, field.member);
      if (!field_keys.insert(field.key).second) {
        throw CodegenError("duplicate field key '" + field.key + "' in component '" +
                           component.component_key + "'");
      }
      if (field.kind == "AssetId") {
        field.asset_kind = field_annotation->required("asset_kind", field.member);
      }
      if (field.kind == "Enum") {
        field.enum_key = field_annotation->required("enum_key", field.member);
        collect_enum_values(*field_decl, field);
      }
      component.fields.push_back(std::move(field));
    }

    components_.push_back(std::move(component));
    return true;
  }

  [[nodiscard]] const std::vector<Component>& components() const { return components_; }

 private:
  static void validate_script(std::string_view exposure, std::string_view component,
                              std::string_view field) {
    if (exposure == "None" || exposure == "Read" || exposure == "ReadWrite") {
      return;
    }
    throw CodegenError("unknown script exposure '" + std::string{exposure} + "' on " +
                       std::string{component} + "." + std::string{field});
  }

  static void validate_component_policy(const Component& component) {
    static const std::set<std::string> storages{"Authored", "RuntimeDerived", "RuntimeSession",
                                                "EditorOnly"};
    static const std::set<std::string> visibilities{"Editable", "DebugInspectable", "Hidden"};
    if (!storages.contains(component.storage)) {
      throw CodegenError("unknown storage policy '" + component.storage + "' on component '" +
                         component.component_key + "'");
    }
    if (!visibilities.contains(component.visibility)) {
      throw CodegenError("unknown visibility '" + component.visibility + "' on component '" +
                         component.component_key + "'");
    }
  }

  static void collect_enum_values(const clang::FieldDecl& field_decl, Field& field) {
    const clang::EnumDecl& enum_decl = enum_decl_for_field(field_decl);
    field.enum_type = qualified_name(enum_decl);
    std::set<std::string> keys;
    std::set<int64_t> values;
    for (const clang::EnumConstantDecl* constant : enum_decl.enumerators()) {
      auto annotation = find_annotation(*constant, "teng.enum_value:");
      if (!annotation) {
        continue;
      }
      EnumValue value;
      value.enumerator_expr = field.enum_type + "::" + constant->getNameAsString();
      value.key = annotation->required("key", value.enumerator_expr);
      value.stable_value = annotation->required_i64("value", value.enumerator_expr);
      if (!keys.insert(value.key).second) {
        throw CodegenError("duplicate enum key '" + value.key + "' in enum '" + field.enum_type +
                           "'");
      }
      if (!values.insert(value.stable_value).second) {
        throw CodegenError("duplicate enum stable value in enum '" + field.enum_type + "'");
      }
      field.enum_values.push_back(std::move(value));
    }
    if (field.enum_values.empty()) {
      throw CodegenError("enum field '" + field.member + "' has no annotated enum values");
    }
  }

  clang::ASTContext& context_;
  std::vector<Component> components_;
};

class Action : public clang::ASTFrontendAction {
 public:
  explicit Action(std::vector<Component>& components) : components_(components) {}

 protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& compiler,
                                                        llvm::StringRef) override {
    class Consumer : public clang::ASTConsumer {
     public:
      Consumer(clang::ASTContext& context, std::vector<Component>& components)
          : context_(context), components_(components) {}

      void HandleTranslationUnit(clang::ASTContext& context) override {
        Collector collector{context};
        collector.TraverseDecl(context.getTranslationUnitDecl());
        const auto& found = collector.components();
        components_.insert(components_.end(), found.begin(), found.end());
      }

     private:
      clang::ASTContext& context_;
      std::vector<Component>& components_;
    };
    return std::make_unique<Consumer>(compiler.getASTContext(), components_);
  }

 private:
  std::vector<Component>& components_;
};

class ActionFactory : public clang::tooling::FrontendActionFactory {
 public:
  explicit ActionFactory(std::vector<Component>& components) : components_(components) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<Action>(components_);
  }

 private:
  std::vector<Component>& components_;
};

[[nodiscard]] std::string storage_expr(std::string_view storage) {
  return "scene::ComponentStoragePolicy::" + std::string{storage};
}

[[nodiscard]] std::string visibility_expr(std::string_view visibility) {
  return "scene::ComponentSchemaVisibility::" + std::string{visibility};
}

[[nodiscard]] std::string kind_expr(std::string_view kind) {
  return "scene::ComponentFieldKind::" + std::string{kind};
}

[[nodiscard]] std::string script_expr(std::string_view script) {
  if (script == "None") {
    return "scene::ScriptExposure::None";
  }
  if (script == "Read") {
    return "scene::ScriptExposure::Read";
  }
  if (script == "ReadWrite") {
    return "scene::ScriptExposure::ReadWrite";
  }
  throw CodegenError("unknown script exposure '" + std::string{script} + "'");
}

[[nodiscard]] std::string default_expr(const Component& component, const Field& field) {
  const std::string member = component.cpp_type + "{}." + field.member;
  if (field.kind == "Bool" || field.kind == "F32" || field.kind == "String") {
    return "scene::ComponentFieldDefaultValue{" + member + "}";
  }
  if (field.kind == "I32") {
    return "scene::ComponentFieldDefaultValue{static_cast<int64_t>(" + member + ")}";
  }
  if (field.kind == "U32") {
    return "scene::ComponentFieldDefaultValue{static_cast<uint64_t>(" + member + ")}";
  }
  if (field.kind == "Vec2") {
    return "scene::ComponentFieldDefaultValue{scene::ComponentDefaultVec2{" + member + ".x, " +
           member + ".y}}";
  }
  if (field.kind == "Vec3") {
    return "scene::ComponentFieldDefaultValue{scene::ComponentDefaultVec3{" + member + ".x, " +
           member + ".y, " + member + ".z}}";
  }
  if (field.kind == "Vec4") {
    return "scene::ComponentFieldDefaultValue{scene::ComponentDefaultVec4{" + member + ".x, " +
           member + ".y, " + member + ".z, " + member + ".w}}";
  }
  if (field.kind == "Quat") {
    return "scene::ComponentFieldDefaultValue{scene::ComponentDefaultQuat{" + member + ".w, " +
           member + ".x, " + member + ".y, " + member + ".z}}";
  }
  if (field.kind == "Mat4") {
    return "scene::ComponentFieldDefaultValue{scene::ComponentDefaultMat4{{" + member + "[0][0], " +
           member + "[0][1], " + member + "[0][2], " + member + "[0][3], " + member + "[1][0], " +
           member + "[1][1], " + member + "[1][2], " + member + "[1][3], " + member + "[2][0], " +
           member + "[2][1], " + member + "[2][2], " + member + "[2][3], " + member + "[3][0], " +
           member + "[3][1], " + member + "[3][2], " + member + "[3][3]}}}";
  }
  if (field.kind == "AssetId") {
    return "scene::ComponentFieldDefaultValue{scene::ComponentDefaultAssetId{" + member +
           ".to_string()}}";
  }
  if (field.kind == "Enum") {
    return "scene::ComponentFieldDefaultValue{scene::ComponentDefaultEnum{std::string{to_key_" +
           c_ident(field.enum_key) + "(" + member + ")}}}";
  }
  throw CodegenError("no default emitter for field kind '" + field.kind + "'");
}

[[nodiscard]] std::string field_record_expr(const Component& component, const Field& field) {
  std::vector<std::string> parts;
  parts.push_back(".key = " + cpp_string(field.key));
  parts.push_back(".member_name = " + cpp_string(field.member));
  parts.push_back(".kind = " + kind_expr(field.kind));
  parts.emplace_back(".authored_required = true");
  parts.push_back(".default_value = " + default_expr(component, field));
  if (!field.asset_kind.empty()) {
    parts.push_back(".asset = scene::ComponentAssetFieldMetadata{.expected_kind = " +
                    cpp_string(field.asset_kind) + "}");
  }
  if (!field.enum_key.empty()) {
    std::string values;
    for (const EnumValue& value : field.enum_values) {
      if (!values.empty()) {
        values += ", ";
      }
      values += "scene::ComponentEnumValueRegistration{.key = " + cpp_string(value.key) +
                ", .value = " + std::to_string(value.stable_value) + "}";
    }
    parts.push_back(".enumeration = scene::ComponentEnumRegistration{.enum_key = " +
                    cpp_string(field.enum_key) + ", .values = {" + values + "}}");
  }
  parts.push_back(".script_exposure = " + script_expr(field.script_exposure));

  std::string joined = "scene::ComponentFieldDescriptor{";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      joined += ", ";
    }
    joined += parts[i];
  }
  joined += "}";
  return joined;
}

[[nodiscard]] std::string json_serialize_expr(const Field& field, std::string_view component_var) {
  std::string member = std::string{component_var} + "." + field.member;
  if (field.kind == "Bool" || field.kind == "I32" || field.kind == "U32" || field.kind == "F32" ||
      field.kind == "String") {
    return member;
  }
  if (field.kind == "Vec2") {
    return "nlohmann::json::array({" + member + ".x, " + member + ".y})";
  }
  if (field.kind == "Vec3") {
    return "nlohmann::json::array({" + member + ".x, " + member + ".y, " + member + ".z})";
  }
  if (field.kind == "Vec4") {
    return "nlohmann::json::array({" + member + ".x, " + member + ".y, " + member + ".z, " +
           member + ".w})";
  }
  if (field.kind == "Quat") {
    return "nlohmann::json::array({" + member + ".w, " + member + ".x, " + member + ".y, " +
           member + ".z})";
  }
  if (field.kind == "Mat4") {
    return "nlohmann::json::array({" + member + "[0][0], " + member + "[0][1], " + member +
           "[0][2], " + member + "[0][3], " + member + "[1][0], " + member + "[1][1], " + member +
           "[1][2], " + member + "[1][3], " + member + "[2][0], " + member + "[2][1], " + member +
           "[2][2], " + member + "[2][3], " + member + "[3][0], " + member + "[3][1], " + member +
           "[3][2], " + member + "[3][3]})";
  }
  if (field.kind == "AssetId") {
    return member + ".to_string()";
  }
  if (field.kind == "Enum") {
    return "std::string{to_key_" + c_ident(field.enum_key) + "(" + member + ")}";
  }
  throw CodegenError("no JSON serializer for kind '" + field.kind + "'");
}

[[nodiscard]] std::string json_load_expr(const Field& field) {
  const std::string payload = "payload[" + cpp_string(field.key) + "]";
  if (field.kind == "Bool") {
    return payload + ".get<bool>()";
  }
  if (field.kind == "I32") {
    return payload + ".get<int32_t>()";
  }
  if (field.kind == "U32") {
    return payload + ".get<uint32_t>()";
  }
  if (field.kind == "F32") {
    return payload + ".get<float>()";
  }
  if (field.kind == "String") {
    return payload + ".get<std::string>()";
  }
  if (field.kind == "Vec2") {
    return "{" + payload + "[0].get<float>(), " + payload + "[1].get<float>()}";
  }
  if (field.kind == "Vec3") {
    return "{" + payload + "[0].get<float>(), " + payload + "[1].get<float>(), " + payload +
           "[2].get<float>()}";
  }
  if (field.kind == "Vec4") {
    return "{" + payload + "[0].get<float>(), " + payload + "[1].get<float>(), " + payload +
           "[2].get<float>(), " + payload + "[3].get<float>()}";
  }
  if (field.kind == "Quat") {
    return "{" + payload + "[0].get<float>(), " + payload + "[1].get<float>(), " + payload +
           "[2].get<float>(), " + payload + "[3].get<float>()}";
  }
  if (field.kind == "Mat4") {
    return "glm::make_mat4(" + payload + ".get<std::array<float, 16>>().data())";
  }
  if (field.kind == "AssetId") {
    return "AssetId::parse(" + payload + ".get<std::string>()).value()";
  }
  if (field.kind == "Enum") {
    return "from_key_" + c_ident(field.enum_key) + "(" + payload + ".get<std::string>())";
  }
  throw CodegenError("no JSON loader for kind '" + field.kind + "'");
}

void write_generated(const Options& options, const std::vector<Component>& components) {
  fs::create_directories(options.out_dir);
  const fs::path hpp = options.out_dir / (options.basename + ".hpp");
  const fs::path cpp = options.out_dir / (options.basename + ".cpp");
  const fs::path manifest = options.manifest_path.empty()
                                ? options.out_dir / (options.basename + ".manifest.json")
                                : options.manifest_path;

  {
    std::ofstream out{hpp};
    out << "#pragma once\n\n";
    out << "#include <cstddef>\n";
    out << "#include <span>\n";
    out << "#include <string_view>\n\n";
    out << "#include \"engine/scene/ComponentRegistry.hpp\"\n\n";
    out << "namespace " << options.cpp_namespace << " {\n\n";
    out << "inline constexpr std::string_view k_banner = "
        << cpp_string(options.basename + " (module=" + options.module_name + ")") << ";\n";
    out << "inline constexpr std::size_t k_component_count = " << components.size() << ";\n";
    size_t field_count = 0;
    for (const Component& component : components) {
      field_count += component.fields.size();
    }
    out << "inline constexpr std::size_t k_field_count = " << field_count << ";\n\n";
    out << "extern const char* const k_dump_lines[];\n";
    out << "extern const std::size_t k_dump_line_count;\n\n";
    out << "[[nodiscard]] std::span<const engine::scene::ComponentModuleDescriptor> "
        << options.function_prefix << "_modules();\n\n";
    out << "}  // namespace " << options.cpp_namespace << "\n";
  }

  {
    std::ofstream out{cpp};
    out << "#include \"" << options.basename << ".hpp\"\n\n";
    out << "#include <array>\n";
    out << "#include <cstdint>\n";
    out << "#include <string>\n";
    out << "#include <string_view>\n\n";
    out << "#include <glm/gtc/type_ptr.hpp>\n";
    out << "#include <nlohmann/json.hpp>\n\n";
    out << "#include \"core/EAssert.hpp\"\n";
    for (const std::string& include : options.includes) {
      out << "#include \"" << include << "\"\n";
    }
    out << "\nnamespace " << options.cpp_namespace << " {\n\n";
    out << "namespace {\n\n";
    out << "namespace scene = teng::engine::scene;\n\n";

    std::set<std::string> emitted_enums;
    for (const Component& component : components) {
      for (const Field& field : component.fields) {
        if (field.kind != "Enum" || !emitted_enums.insert(field.enum_key).second) {
          continue;
        }
        const std::string helper = c_ident(field.enum_key);
        out << "[[nodiscard, maybe_unused]] std::string_view to_key_" << helper << "("
            << field.enum_type << " value) {\n";
        out << "  switch (value) {\n";
        for (const EnumValue& value : field.enum_values) {
          out << "    case " << value.enumerator_expr << ":\n";
          out << "      return " << cpp_string(value.key) << ";\n";
        }
        out << "  }\n";
        out << "  ALWAYS_ASSERT(false, \"unknown reflected enum value\");\n";
        out << "  return \"\";\n";
        out << "}\n\n";
        out << "[[nodiscard, maybe_unused]] " << field.enum_type << " from_key_" << helper
            << "(std::string_view key) {\n";
        for (const EnumValue& value : field.enum_values) {
          out << "  if (key == " << cpp_string(value.key) << ") {\n";
          out << "    return " << value.enumerator_expr << ";\n";
          out << "  }\n";
        }
        out << "  ALWAYS_ASSERT(false, \"unknown reflected enum key {}\", key);\n";
        out << "  return " << field.enum_values.front().enumerator_expr << ";\n";
        out << "}\n\n";
      }
    }

    for (const Component& component : components) {
      const std::string array_name = "k_" + c_ident(component.component_key) + "_fields";
      out << "const std::array<scene::ComponentFieldDescriptor, " << component.fields.size() << "> "
          << array_name << " = {{\n";
      for (const Field& field : component.fields) {
        out << "  " << field_record_expr(component, field) << ",\n";
      }
      out << "}};\n\n";
    }

    for (const Component& component : components) {
      const std::string ident = c_ident(component.component_key);
      out << "void register_flecs_" << ident << "(flecs::world& world) {\n";
      out << "  world.component<" << component.cpp_type << ">();\n";
      out << "}\n\n";
      if (component.add_on_create) {
        out << "void apply_on_create_" << ident << "(flecs::entity entity) {\n";
        out << "  entity.set<" << component.cpp_type << ">(" << component.cpp_type << "{});\n";
        out << "}\n\n";
      }
      if (component.storage == "Authored") {
        out << "bool has_component_" << ident << "(flecs::entity entity) {\n";
        out << "  return entity.has<" << component.cpp_type << ">();\n";
        out << "}\n\n";
        out << "nlohmann::json serialize_component_" << ident << "(flecs::entity entity) {\n";
        out << "  const auto& component = entity.get<" << component.cpp_type << ">();\n";
        out << "  return nlohmann::json{\n";
        for (const Field& field : component.fields) {
          out << "      {" << cpp_string(field.key) << ", "
              << json_serialize_expr(field, "component") << "},\n";
        }
        out << "  };\n";
        out << "}\n\n";
        out << "void deserialize_component_" << ident
            << "(flecs::entity entity, const nlohmann::json& payload) {\n";
        out << "  entity.set<" << component.cpp_type << ">(" << component.cpp_type << "{\n";
        for (const Field& field : component.fields) {
          out << "      ." << field.member << " = " << json_load_expr(field) << ",\n";
        }
        out << "  });\n";
        out << "}\n\n";
      }
    }

    std::vector<std::pair<std::string, uint32_t>> modules;
    for (const Component& component : components) {
      const auto existing =
          std::ranges::find(modules, component.module_id, &std::pair<std::string, uint32_t>::first);
      if (existing == modules.end()) {
        modules.emplace_back(component.module_id, component.module_version);
      } else if (existing->second != component.module_version) {
        throw CodegenError("module '" + component.module_id +
                           "' has conflicting module_version annotations");
      }
    }
    std::ranges::sort(modules);

    auto emit_component_descriptor = [&](const Component& component) {
      const std::string ident = c_ident(component.component_key);
      const std::string array_name = "k_" + c_ident(component.component_key) + "_fields";
      out << "  scene::ComponentDescriptor{\n";
      out << "      .component_key = " << cpp_string(component.component_key) << ",\n";
      out << "      .schema_version = " << component.schema_version << ",\n";
      out << "      .storage = " << storage_expr(component.storage) << ",\n";
      out << "      .visibility = " << visibility_expr(component.visibility) << ",\n";
      out << "      .add_on_create = " << (component.add_on_create ? "true" : "false") << ",\n";
      out << "      .fields = std::span<const scene::ComponentFieldDescriptor>{" << array_name
          << "},\n";
      out << "      .ops = scene::ComponentTypeOps{\n";
      out << "          .register_flecs_fn = "
          << (component.storage == "EditorOnly" ? "nullptr" : "register_flecs_" + ident) << ",\n";
      out << "          .apply_on_create_fn = "
          << (component.add_on_create ? "apply_on_create_" + ident : "nullptr") << ",\n";
      out << "          .has_component_fn = "
          << (component.storage == "Authored" ? "has_component_" + ident : "nullptr") << ",\n";
      out << "          .serialize_fn = "
          << (component.storage == "Authored" ? "serialize_component_" + ident : "nullptr")
          << ",\n";
      out << "          .deserialize_fn = "
          << (component.storage == "Authored" ? "deserialize_component_" + ident : "nullptr")
          << ",\n";
      out << "      },\n";
      out << "  },\n";
    };

    for (const auto& [module_id, module_version] : modules) {
      const std::string module_ident = c_ident(module_id);
      const auto module_component_count =
          static_cast<size_t>(std::ranges::count(components, module_id, &Component::module_id));
      out << "const std::array<scene::ComponentDescriptor, " << module_component_count << "> k_"
          << module_ident << "_components = {{\n";
      for (const Component& component : components) {
        if (component.module_id == module_id) {
          emit_component_descriptor(component);
        }
      }
      out << "}};\n\n";
      (void)module_version;
    }

    out << "const std::array<scene::ComponentModuleDescriptor, " << modules.size()
        << "> k_modules = {{\n";
    for (const auto& [module_id, module_version] : modules) {
      const std::string module_ident = c_ident(module_id);
      out << "  scene::ComponentModuleDescriptor{\n";
      out << "      .module_id = " << cpp_string(module_id) << ",\n";
      out << "      .module_version = " << module_version << ",\n";
      out << "      .components = std::span<const scene::ComponentDescriptor>{k_" << module_ident
          << "_components},\n";
      out << "  },\n";
    }
    out << "}};\n\n";
    out << "}  // namespace\n\n";

    std::vector<std::string> dump_lines;
    for (const Component& component : components) {
      dump_lines.push_back("component " + component.component_key + " type=" + component.cpp_type +
                           " storage=" + component.storage + " visibility=" + component.visibility);
      for (const Field& field : component.fields) {
        dump_lines.push_back("  field " + field.key + " member=" + field.member +
                             " kind=" + field.kind + " script=" + field.script_exposure);
      }
    }
    out << "const std::size_t k_dump_line_count = " << dump_lines.size() << ";\n";
    out << "const char* const k_dump_lines[] = {\n";
    for (const std::string& line : dump_lines) {
      out << "  " << cpp_string(line) << ",\n";
    }
    out << "};\n\n";
    out << "std::span<const engine::scene::ComponentModuleDescriptor> " << options.function_prefix
        << "_modules() {\n";
    out << "  return k_modules;\n";
    out << "}\n\n";
    out << "}  // namespace " << options.cpp_namespace << "\n";
  }

  {
    std::ofstream out{manifest};
    out << "{\n";
    out << "  \"tool\": \"teng-component-codegen\",\n";
    out << "  \"module_name\": " << cpp_string(options.module_name) << ",\n";
    out << "  \"components\": [\n";
    for (size_t i = 0; i < components.size(); ++i) {
      const Component& component = components[i];
      out << "    {\"cpp_type\": " << cpp_string(component.cpp_type)
          << ", \"component_key\": " << cpp_string(component.component_key)
          << ", \"field_count\": " << component.fields.size() << "}";
      out << (i + 1 == components.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
  }
}

[[nodiscard]] Options parse_options(int argc, const char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](std::string_view name) -> std::string {
      if (i + 1 >= argc) {
        throw CodegenError("missing value for " + std::string{name});
      }
      return argv[++i];
    };
    if (arg == "--out-dir") {
      options.out_dir = require_value(arg);
    } else if (arg == "--manifest") {
      options.manifest_path = require_value(arg);
    } else if (arg == "--module-name") {
      options.module_name = require_value(arg);
    } else if (arg == "--output-basename") {
      options.basename = require_value(arg);
    } else if (arg == "--namespace") {
      options.cpp_namespace = require_value(arg);
    } else if (arg == "--function-prefix") {
      options.function_prefix = require_value(arg);
    } else if (arg == "--include") {
      options.includes.push_back(require_value(arg));
    } else if (arg == "--header") {
      options.headers.push_back(require_value(arg));
    } else if (arg.starts_with("--extra-arg=")) {
      options.compile_args.push_back(arg.substr(std::string{"--extra-arg="}.size()));
    } else if (arg == "--extra-arg") {
      options.compile_args.push_back(require_value(arg));
    } else {
      throw CodegenError("unknown argument: " + arg);
    }
  }
  if (options.out_dir.empty()) {
    throw CodegenError("--out-dir is required");
  }
  if (options.module_name.empty()) {
    throw CodegenError("--module-name is required");
  }
  if (options.headers.empty()) {
    throw CodegenError("at least one --header is required");
  }
  return options;
}

}  // namespace

int main(int argc, const char** argv) {
  try {
    Options options = parse_options(argc, argv);
    if (options.compile_args.empty()) {
      options.compile_args = {"-std=c++23"};
    }
    const clang::tooling::FixedCompilationDatabase compilations(".", options.compile_args);
    clang::tooling::ClangTool tool(compilations, options.headers);

    std::vector<Component> components;
    ActionFactory factory{components};
    const int run_result = tool.run(&factory);
    if (run_result != 0) {
      return run_result;
    }

    std::set<std::string> component_keys;
    for (const Component& component : components) {
      if (!component_keys.insert(component.component_key).second) {
        throw CodegenError("duplicate component key '" + component.component_key + "'");
      }
    }
    std::ranges::sort(components, {}, &Component::component_key);
    write_generated(options, components);
    return 0;
  } catch (const std::exception& exc) {
    llvm::errs() << "teng-component-codegen: error: " << exc.what() << "\n";
    return 2;
  }
}
