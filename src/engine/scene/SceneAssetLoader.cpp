#include "engine/scene/SceneAssetLoader.hpp"

#include <array>
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

#include "core/Result.hpp"
#include "core/TomlUtil.hpp"
#include "engine/scene/SceneComponents.hpp"

namespace teng::engine {
namespace {

constexpr int64_t k_schema_version = 1;

[[nodiscard]] std::string field_error(std::string_view field) {
  return "scene asset field '" + std::string(field) + "' is missing or has the wrong type";
}

template <class T>
[[nodiscard]] Result<T> required_value(const toml::table& table, std::string_view field) {
  std::optional<T> value = table[field].value<T>();
  if (!value) {
    return make_unexpected(field_error(field));
  }
  return *value;
}

[[nodiscard]] Result<std::array<float, 3>> required_vec3(const toml::table& table,
                                                         std::string_view field) {
  const toml::array* array = table[field].as_array();
  if (!array || array->size() != 3) {
    return make_unexpected(field_error(field));
  }

  std::array<float, 3> out{};
  for (size_t i = 0; i < out.size(); ++i) {
    const std::optional<double> value = array->get(i)->value<double>();
    if (!value) {
      return make_unexpected(field_error(field));
    }
    out[i] = static_cast<float>(*value);
  }
  return out;
}

[[nodiscard]] Result<std::array<float, 4>> required_vec4(const toml::table& table,
                                                         std::string_view field) {
  const toml::array* array = table[field].as_array();
  if (!array || array->size() != 4) {
    return make_unexpected(field_error(field));
  }

  std::array<float, 4> out{};
  for (size_t i = 0; i < out.size(); ++i) {
    const std::optional<double> value = array->get(i)->value<double>();
    if (!value) {
      return make_unexpected(field_error(field));
    }
    out[i] = static_cast<float>(*value);
  }
  return out;
}

[[nodiscard]] Result<glm::mat4> required_mat4(const toml::table& table, std::string_view field) {
  const toml::array* array = table[field].as_array();
  if (!array || array->size() != 16) {
    return make_unexpected(field_error(field));
  }

  glm::mat4 out{1.f};
  for (size_t i = 0; i < 16; ++i) {
    const std::optional<double> value = array->get(i)->value<double>();
    if (!value) {
      return make_unexpected(field_error(field));
    }
    out[static_cast<int>(i / 4)][static_cast<int>(i % 4)] = static_cast<float>(*value);
  }
  return out;
}

[[nodiscard]] Result<Transform> parse_transform(const toml::table& table) {
  Result<std::array<float, 3>> translation = required_vec3(table, "translation");
  REQUIRED_OR_RETURN(translation);
  Result<std::array<float, 4>> rotation = required_vec4(table, "rotation");
  REQUIRED_OR_RETURN(rotation);
  Result<std::array<float, 3>> scale = required_vec3(table, "scale");
  REQUIRED_OR_RETURN(scale);

  return Transform{
      .translation = {(*translation)[0], (*translation)[1], (*translation)[2]},
      .rotation = {(*rotation)[0], (*rotation)[1], (*rotation)[2], (*rotation)[3]},
      .scale = {(*scale)[0], (*scale)[1], (*scale)[2]},
  };
}

[[nodiscard]] Result<Camera> parse_camera(const toml::table& table) {
  Result<double> fov_y = required_value<double>(table, "fov_y");
  REQUIRED_OR_RETURN(fov_y);
  Result<double> z_near = required_value<double>(table, "z_near");
  REQUIRED_OR_RETURN(z_near);
  Result<double> z_far = required_value<double>(table, "z_far");
  REQUIRED_OR_RETURN(z_far);
  Result<bool> primary = required_value<bool>(table, "primary");
  REQUIRED_OR_RETURN(primary);

  return Camera{.fov_y = static_cast<float>(*fov_y),
                .z_near = static_cast<float>(*z_near),
                .z_far = static_cast<float>(*z_far),
                .primary = *primary};
}

[[nodiscard]] Result<DirectionalLight> parse_directional_light(const toml::table& table) {
  Result<std::array<float, 3>> direction = required_vec3(table, "direction");
  REQUIRED_OR_RETURN(direction);
  Result<std::array<float, 3>> color = required_vec3(table, "color");
  REQUIRED_OR_RETURN(color);
  Result<double> intensity = required_value<double>(table, "intensity");
  REQUIRED_OR_RETURN(intensity);

  return DirectionalLight{.direction = {(*direction)[0], (*direction)[1], (*direction)[2]},
                          .color = {(*color)[0], (*color)[1], (*color)[2]},
                          .intensity = static_cast<float>(*intensity)};
}

[[nodiscard]] Result<MeshRenderable> parse_mesh_renderable(const toml::table& table) {
  Result<std::string> model_text = required_value<std::string>(table, "model");
  REQUIRED_OR_RETURN(model_text);
  const std::optional<AssetId> model = AssetId::parse(*model_text);
  if (!model) {
    return make_unexpected("mesh_renderable.model is not a valid AssetId");
  }
  return MeshRenderable{.model = *model};
}

[[nodiscard]] Result<void> load_entity(Scene& scene, const toml::table& table) {
  Result<int64_t> guid_value = required_value<int64_t>(table, "guid");
  REQUIRED_OR_RETURN(guid_value);
  if (*guid_value <= 0) {
    return make_unexpected("scene entity guid must be a positive integer");
  }

  Result<std::string> name = required_value<std::string>(table, "name");
  REQUIRED_OR_RETURN(name);

  const EntityGuid guid{static_cast<uint64_t>(*guid_value)};
  scene.create_entity(guid, *name);

  const toml::table* transform_table = table["transform"].as_table();
  if (!transform_table) {
    return make_unexpected("scene entity is missing transform table");
  }
  Result<Transform> transform = parse_transform(*transform_table);
  REQUIRED_OR_RETURN(transform);
  scene.set_transform(guid, *transform);

  if (const toml::table* local_to_world_table = table["local_to_world"].as_table()) {
    Result<glm::mat4> local_to_world = required_mat4(*local_to_world_table, "matrix");
    REQUIRED_OR_RETURN(local_to_world);
    scene.set_local_to_world(guid, {.value = *local_to_world});
  } else {
    scene.set_local_to_world(guid, {.value = transform_to_matrix(*transform)});
  }

  if (const toml::table* camera_table = table["camera"].as_table()) {
    Result<Camera> camera = parse_camera(*camera_table);
    REQUIRED_OR_RETURN(camera);
    scene.set_camera(guid, *camera);
  }
  if (const toml::table* light_table = table["directional_light"].as_table()) {
    Result<DirectionalLight> light = parse_directional_light(*light_table);
    REQUIRED_OR_RETURN(light);
    scene.set_directional_light(guid, *light);
  }
  if (const toml::table* mesh_table = table["mesh_renderable"].as_table()) {
    Result<MeshRenderable> mesh = parse_mesh_renderable(*mesh_table);
    REQUIRED_OR_RETURN(mesh);
    scene.set_mesh_renderable(guid, *mesh);
  }

  return {};
}

}  // namespace

Result<SceneAssetLoadResult> load_scene_asset(SceneManager& scenes,
                                              const std::filesystem::path& path) {
  Result<toml::table> parsed = parse_toml_file(path);
  REQUIRED_OR_RETURN(parsed);

  Result<int64_t> schema_version = required_value<int64_t>(*parsed, "schema_version");
  REQUIRED_OR_RETURN(schema_version);
  if (*schema_version != k_schema_version) {
    return make_unexpected("scene asset schema_version must be 1");
  }

  Result<std::string> name = required_value<std::string>(*parsed, "name");
  REQUIRED_OR_RETURN(name);

  const toml::array* entities = (*parsed)["entities"].as_array();
  if (!entities) {
    return make_unexpected("scene asset is missing entities array");
  }

  Scene& scene = scenes.create_scene(*name);
  for (const toml::node& node : *entities) {
    const toml::table* entity_table = node.as_table();
    if (!entity_table) {
      return make_unexpected("scene entities entry must be a table");
    }
    Result<void> loaded = load_entity(scene, *entity_table);
    REQUIRED_OR_RETURN(loaded);
  }

  scenes.set_active_scene(scene.id());
  return SceneAssetLoadResult{.scene_id = scene.id(), .scene = &scene};
}

}  // namespace teng::engine
