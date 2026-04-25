#include "ScenePresets.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <random>

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

void append_default_scene_presets(std::vector<ScenePreset>& out,
                                  const std::filesystem::path& resource_dir,
                                  const ScenePresetLoaders& loaders) {
  out.reserve(out.size() + 10);

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            loaders.add_model(resolve_model_path(resource_dir, k_suzanne_path), glm::mat4{1.f});
          },
      .name = "suzanne",
      .cam = Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f},
  });

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            loaders.add_model(resolve_model_path(resource_dir, k_sponza_path), glm::mat4{1.f});
          },
      .name = "sponza",
      .cam = Camera{.pos = {-6, 2.5, 0}, .move_speed = 2.0f},
  });

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            glm::ivec3 iter{};
            glm::ivec3 dims{2, 1, 2};
            std::vector<glm::mat4> transforms;
            transforms.reserve((2ull * dims.x + 1) * (2 * dims.y + 1) * (2 * dims.z + 1));
            for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
              for (iter.y = -dims.y; iter.y <= dims.y; iter.y++) {
                for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
                  glm::vec3 pos = glm::vec3{iter} * 40.0f;
                  transforms.emplace_back(
                      glm::translate(glm::scale(glm::mat4{1}, glm::vec3(1.0f)), pos));
                }
              }
            }
            loaders.add_instanced(resolve_model_path(resource_dir, k_sponza_path),
                                  std::move(transforms));
          },
      .name = "sponza grid",
      .cam = Camera{.pos = {100, 100, -100}, .pitch = -40, .yaw = 145, .move_speed = 15.f},
  });

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            loaders.add_model(resolve_model_path(resource_dir, k_chessboard_path), glm::mat4{1.f});
          },
      .name = "chessboard",
      .cam = Camera{.pos = {0.4, 0.4, 0.4}, .pitch = -30, .yaw = -130, .move_speed = .25f},
      .csm_defaults =
          ScenePreset::CsmDefaults{
              .z_near = 0.02f, .z_far = 10.0f, .cascade_count = 2, .split_lambda = 0.8f},
  });

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            glm::ivec3 iter{};
            glm::ivec3 dims{4, 0, 4};
            std::vector<glm::mat4> transforms;
            transforms.reserve((2ull * dims.x + 1) * (2 * dims.z + 1));
            for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
              for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
                glm::vec3 pos = glm::vec3{iter} * 1.0f;
                transforms.emplace_back(
                    glm::translate(glm::scale(glm::mat4{1}, glm::vec3(10.0f)), pos));
              }
            }
            loaders.add_instanced(resolve_model_path(resource_dir, k_chessboard_path),
                                  std::move(transforms));
          },
      .name = "chessboard grid",
      .cam = Camera{.pos = {-30, 10, -20}, .pitch = -25, .yaw = 40, .move_speed = 2.f},
      .csm_defaults =
          ScenePreset::CsmDefaults{
              .z_near = 0.02f, .z_far = 20.0f, .cascade_count = 3, .split_lambda = 0.8f},
  });

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            const auto resolved = resolve_model_path(resource_dir, k_suzanne_path);
            for (size_t i = 0; i < 3000; i++) {
              auto rand_f = [](float radius) { return uniform_float(-radius, radius); };
              const float radius = 1000.f;
              auto pos = glm::vec3{rand_f(radius), rand_f(radius), rand_f(radius)};
              glm::vec3 random_axis = glm::linearRand(glm::vec3(-1.0f), glm::vec3(1.0f));
              float random_angle = glm::linearRand(0.0f, glm::two_pi<float>());
              auto rot = glm::angleAxis(random_angle, glm::normalize(random_axis));
              loaders.add_model(resolved, glm::translate(glm::mat4{1}, pos) * glm::mat4_cast(rot) *
                                              glm::scale(glm::mat4{1}, glm::vec3{30.f}));
            }
          },
      .name = "random suzannes",
      .cam = Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f},
  });

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            const auto resolved = resolve_model_path(resource_dir, k_cube_path);
            for (size_t i = 0; i < 50'000; i++) {
              auto rand_f = [](float radius) { return uniform_float(-radius, radius); };
              const float radius = 3000.f;
              auto pos = glm::vec3{rand_f(radius), rand_f(radius), rand_f(radius)};
              glm::vec3 random_axis = glm::linearRand(glm::vec3(-1.0f), glm::vec3(1.0f));
              float random_angle = glm::linearRand(0.0f, glm::two_pi<float>());
              auto rot = glm::angleAxis(random_angle, glm::normalize(random_axis));
              loaders.add_model(resolved, glm::translate(glm::mat4{1}, pos) * glm::mat4_cast(rot) *
                                              glm::scale(glm::mat4{1}, glm::vec3{10.f}));
            }
          },
      .name = "random cubes",
      .cam = Camera{.pos = {0, 0, 3}, .pitch = 0, .yaw = 270, .move_speed = 1.f},
  });

  {
    const std::filesystem::path bistro_path = "/Users/tony/models/Bistro_Godot_opt.glb";
    if (std::filesystem::exists(bistro_path)) {
      out.push_back(ScenePreset{
          .load_fn = [loaders, bistro_path]() { loaders.add_model(bistro_path, glm::mat4{1.f}); },
          .name = "bistro",
          .cam = Camera{},
      });
    }
  }

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            int n = 22;
            glm::ivec3 iter{};
            glm::ivec3 dims{n, 0, n};
            std::vector<glm::mat4> transforms;
            transforms.reserve((2ull * dims.x + 1) * (2 * dims.z + 1));
            for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
              for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
                glm::vec3 pos = glm::vec3{iter} * 40.0f;
                transforms.emplace_back(
                    glm::translate(glm::scale(glm::mat4{1}, glm::vec3(1.0f)), pos));
              }
            }
            loaders.add_instanced(resolve_model_path(resource_dir, k_sponza_path),
                                  std::move(transforms));
          },
      .name = "many many sponzas",
      .cam = Camera{.pos = {900, 300, -900}, .pitch = -28, .yaw = 134, .move_speed = 15.f},
  });

  out.push_back(ScenePreset{
      .load_fn =
          [loaders, resource_dir]() {
            loaders.add_model(resolve_model_path(resource_dir, k_sponza_path), glm::mat4{1.f});
            loaders.add_model(resolve_model_path(resource_dir, k_chessboard_path),
                              glm::translate(glm::mat4{1}, glm::vec3{0, -10, 0}));
          },
      .name = "chess-sponza",
      .cam = Camera{.pos = {-6, 2.5, 0}, .move_speed = 2.0f},
  });
}

}  // namespace teng::demo_scenes
