#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <glm/mat4x4.hpp>
#include <optional>
#include <string>
#include <vector>

#include "../metalrender/Camera.hpp"

namespace teng::demo_scenes {

inline constexpr const char* k_sponza_path = "Models/Sponza/glTF_ktx2/Sponza.gltf";
inline constexpr const char* k_chessboard_path =
    "Models/ABeautifulGame/glTF_ktx2/ABeautifulGame.gltf";
inline constexpr const char* k_suzanne_path = "Models/Suzanne/glTF_ktx2/Suzanne.gltf";
inline constexpr const char* k_cube_path = "Models/Cube/glTF_ktx2/Cube.gltf";

[[nodiscard]] std::filesystem::path resolve_model_path(const std::filesystem::path& resource_dir,
                                                       const std::string& path);

void seed_demo_scene_rng(unsigned seed);

struct ScenePresetLoaders {
  std::function<void(const std::filesystem::path& resolved_path, const glm::mat4& root_transform)>
      add_model;
  std::function<void(const std::filesystem::path& resolved_path,
                     std::vector<glm::mat4>&& instance_transforms)>
      add_instanced;
};

struct ScenePreset {
  struct CsmDefaults {
    float z_near{0.1f};
    float z_far{200.f};
    uint32_t cascade_count{3};
    float split_lambda{0.95f};
  };

  std::function<void()> load_fn;
  std::string name;
  Camera cam;
  std::optional<CsmDefaults> csm_defaults;
};

struct DemoSceneModelInstances {
  std::string source_path;
  std::vector<glm::mat4> instance_transforms;
};

struct DemoScenePresetData {
  std::string name;
  Camera cam;
  std::optional<ScenePreset::CsmDefaults> csm_defaults;
  std::vector<DemoSceneModelInstances> models;
};

void append_default_scene_preset_data(std::vector<DemoScenePresetData>& out,
                                      const std::filesystem::path& resource_dir);

void append_default_scene_presets(std::vector<ScenePreset>& out,
                                  const std::filesystem::path& resource_dir,
                                  const ScenePresetLoaders& loaders);

}  // namespace teng::demo_scenes
