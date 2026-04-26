#include "ScenePresets.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <random>
#include <utility>

namespace teng::demo_scenes {

namespace {

std::default_random_engine& demo_rng() {
  thread_local std::default_random_engine eng{std::random_device{}()};
  return eng;
}

float uniform_float(float min_v, float max_v) {
  std::uniform_real_distribution<float> dist{min_v, max_v};
  return dist(demo_rng());
}

}  // namespace

std::filesystem::path resolve_model_path(const std::filesystem::path& resource_dir,
                                         const std::string& path) {
  if (path.starts_with("Models")) {
    return resource_dir / "models" / "gltf" / path;
  }
  return path;
}

void seed_demo_scene_rng(unsigned seed) { demo_rng().seed(seed); }

namespace {

void add_model(DemoScenePresetData& preset, std::string source_path, const glm::mat4& transform) {
  preset.models.push_back(DemoSceneModelInstances{
      .source_path = std::move(source_path),
      .instance_transforms = {transform},
  });
}

void add_instanced(DemoScenePresetData& preset, std::string source_path,
                   std::vector<glm::mat4>&& transforms) {
  preset.models.push_back(DemoSceneModelInstances{
      .source_path = std::move(source_path),
      .instance_transforms = std::move(transforms),
  });
}

}  // namespace

void append_default_scene_preset_data(std::vector<DemoScenePresetData>& out,
                                      const std::filesystem::path& resource_dir) {
  (void)resource_dir;
  out.reserve(out.size() + 10);

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "suzanne",
      .cam = Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f},
    });
    add_model(preset, k_suzanne_path, glm::mat4{1.f});
  }

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "sponza",
      .cam = Camera{.pos = {-6, 2.5, 0}, .move_speed = 2.0f},
    });
    add_model(preset, k_sponza_path, glm::mat4{1.f});
  }

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "sponza grid",
      .cam = Camera{.pos = {100, 100, -100}, .pitch = -40, .yaw = 145, .move_speed = 15.f},
    });
    glm::ivec3 iter{};
    const glm::ivec3 dims{2, 1, 2};
    std::vector<glm::mat4> transforms;
    transforms.reserve((2ull * dims.x + 1) * (2 * dims.y + 1) * (2 * dims.z + 1));
    for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
      for (iter.y = -dims.y; iter.y <= dims.y; iter.y++) {
        for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
          const glm::vec3 pos = glm::vec3{iter} * 40.0f;
          transforms.emplace_back(glm::translate(glm::scale(glm::mat4{1}, glm::vec3(1.0f)), pos));
        }
      }
    }
    add_instanced(preset, k_sponza_path, std::move(transforms));
  }

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "chessboard",
      .cam = Camera{.pos = {0.4, 0.4, 0.4}, .pitch = -30, .yaw = -130, .move_speed = .25f},
      .csm_defaults =
          ScenePreset::CsmDefaults{
              .z_near = 0.02f, .z_far = 10.0f, .cascade_count = 2, .split_lambda = 0.8f},
    });
    add_model(preset, k_chessboard_path, glm::mat4{1.f});
  }

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "chessboard grid",
      .cam = Camera{.pos = {-30, 10, -20}, .pitch = -25, .yaw = 40, .move_speed = 2.f},
      .csm_defaults =
          ScenePreset::CsmDefaults{
              .z_near = 0.02f, .z_far = 20.0f, .cascade_count = 3, .split_lambda = 0.8f},
    });
    glm::ivec3 iter{};
    const glm::ivec3 dims{4, 0, 4};
    std::vector<glm::mat4> transforms;
    transforms.reserve((2ull * dims.x + 1) * (2 * dims.z + 1));
    for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
      for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
        const glm::vec3 pos = glm::vec3{iter} * 1.0f;
        transforms.emplace_back(glm::translate(glm::scale(glm::mat4{1}, glm::vec3(10.0f)), pos));
      }
    }
    add_instanced(preset, k_chessboard_path, std::move(transforms));
  }

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "random suzannes",
      .cam = Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f},
    });
    std::vector<glm::mat4> transforms;
    transforms.reserve(3000);
    for (size_t i = 0; i < 3000; i++) {
      auto rand_f = [](float radius) { return uniform_float(-radius, radius); };
      const float radius = 1000.f;
      auto pos = glm::vec3{rand_f(radius), rand_f(radius), rand_f(radius)};
      const glm::vec3 random_axis = glm::linearRand(glm::vec3(-1.0f), glm::vec3(1.0f));
      const float random_angle = glm::linearRand(0.0f, glm::two_pi<float>());
      auto rot = glm::angleAxis(random_angle, glm::normalize(random_axis));
      transforms.emplace_back(glm::translate(glm::mat4{1}, pos) * glm::mat4_cast(rot) *
                              glm::scale(glm::mat4{1}, glm::vec3{30.f}));
    }
    add_instanced(preset, k_suzanne_path, std::move(transforms));
  }

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "random cubes",
      .cam = Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f},
    });
    std::vector<glm::mat4> transforms;
    transforms.reserve(50'000);
    for (size_t i = 0; i < 50'000; i++) {
      auto rand_f = [](float radius) { return uniform_float(-radius, radius); };
      const float radius = 3000.f;
      auto pos = glm::vec3{rand_f(radius), rand_f(radius), rand_f(radius)};
      const glm::vec3 random_axis = glm::linearRand(glm::vec3(-1.0f), glm::vec3(1.0f));
      const float random_angle = glm::linearRand(0.0f, glm::two_pi<float>());
      auto rot = glm::angleAxis(random_angle, glm::normalize(random_axis));
      transforms.emplace_back(glm::translate(glm::mat4{1}, pos) * glm::mat4_cast(rot) *
                              glm::scale(glm::mat4{1}, glm::vec3{10.f}));
    }
    add_instanced(preset, k_cube_path, std::move(transforms));
  }

  {
    const std::filesystem::path bistro_path = "/Users/tony/models/Bistro_Godot_opt.glb";
    if (std::filesystem::exists(bistro_path)) {
      auto& preset = out.emplace_back(DemoScenePresetData{
          .name = "bistro",
          .cam = Camera{},
      });
      add_model(preset, bistro_path.string(), glm::mat4{1.f});
    }
  }

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "many many sponzas",
      .cam = Camera{.pos = {900, 300, -900}, .pitch = -28, .yaw = 134, .move_speed = 15.f},
    });
    const int n = 22;
    glm::ivec3 iter{};
    const glm::ivec3 dims{n, 0, n};
    std::vector<glm::mat4> transforms;
    transforms.reserve((2ull * dims.x + 1) * (2 * dims.z + 1));
    for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
      for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
        const glm::vec3 pos = glm::vec3{iter} * 40.0f;
        transforms.emplace_back(glm::translate(glm::scale(glm::mat4{1}, glm::vec3(1.0f)), pos));
      }
    }
    add_instanced(preset, k_sponza_path, std::move(transforms));
  }

  {
    auto& preset = out.emplace_back(DemoScenePresetData{
      .name = "chess-sponza",
      .cam = Camera{.pos = {-6, 2.5, 0}, .move_speed = 2.0f},
    });
    add_model(preset, k_sponza_path, glm::mat4{1.f});
    add_model(preset, k_chessboard_path, glm::translate(glm::mat4{1}, glm::vec3{0, -10, 0}));
  }
}

void append_default_scene_presets(std::vector<ScenePreset>& out,
                                  const std::filesystem::path& resource_dir,
                                  const ScenePresetLoaders& loaders) {
  std::vector<DemoScenePresetData> data_presets;
  append_default_scene_preset_data(data_presets, resource_dir);
  out.reserve(out.size() + data_presets.size());
  for (auto& data : data_presets) {
    out.push_back(ScenePreset{
        .load_fn =
            [loaders, resource_dir, models = data.models]() {
              for (const auto& model : models) {
                const auto resolved = resolve_model_path(resource_dir, model.source_path);
                if (model.instance_transforms.size() == 1) {
                  loaders.add_model(resolved, model.instance_transforms.front());
                } else {
                  loaders.add_instanced(resolved, std::vector<glm::mat4>{model.instance_transforms});
                }
              }
            },
        .name = std::move(data.name),
        .cam = data.cam,
        .csm_defaults = data.csm_defaults,
    });
  }
}

}  // namespace teng::demo_scenes
