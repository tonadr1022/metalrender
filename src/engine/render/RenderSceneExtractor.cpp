#include "engine/render/RenderSceneExtractor.hpp"

#include <algorithm>

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"

namespace teng::engine {

namespace {

[[nodiscard]] constexpr bool guid_less(EntityGuid a, EntityGuid b) { return a.value < b.value; }

}  // namespace

RenderScene extract_render_scene(Scene& scene, const RenderSceneExtractOptions& options) {
  RenderScene output;
  output.frame = options.frame;

  flecs::world& world = scene.world();
  RenderSceneExtractStats local_stats;
  RenderSceneExtractStats& stats = options.stats ? *options.stats : local_stats;

  world.each(
      [&output](const EntityGuidComponent& guid, const LocalToWorld& local_to_world,
                const Camera& camera) {
        if (!guid.guid.is_valid()) {
          return;
        }
        output.cameras.push_back(RenderCamera{
            .entity = guid.guid,
            .local_to_world = local_to_world.value,
            .fov_y = camera.fov_y,
            .z_near = camera.z_near,
            .z_far = camera.z_far,
            .primary = camera.primary,
        });
      });

  world.each(
      [&output](const EntityGuidComponent& guid, const LocalToWorld& local_to_world,
                const DirectionalLight& light) {
        if (!guid.guid.is_valid()) {
          return;
        }
        output.directional_lights.push_back(RenderDirectionalLight{
            .entity = guid.guid,
            .local_to_world = local_to_world.value,
            .direction = light.direction,
            .color = light.color,
            .intensity = light.intensity,
        });
      });

  world.each(
      [&output, &stats](const EntityGuidComponent& guid, const LocalToWorld& local_to_world,
                        const MeshRenderable& mesh) {
        if (!guid.guid.is_valid()) {
          return;
        }
        if (!mesh.model.is_valid()) {
          ++stats.skipped_meshes_missing_asset;
          return;
        }
        output.meshes.push_back(RenderMesh{
            .entity = guid.guid,
            .model = mesh.model,
            .local_to_world = local_to_world.value,
        });
      });

  world.each(
      [&output, &stats](const EntityGuidComponent& guid, const LocalToWorld& local_to_world,
                        const SpriteRenderable& sprite) {
        if (!guid.guid.is_valid()) {
          return;
        }
        if (!sprite.texture.is_valid()) {
          ++stats.skipped_sprites_missing_asset;
          return;
        }
        output.sprites.push_back(RenderSprite{
            .entity = guid.guid,
            .texture = sprite.texture,
            .local_to_world = local_to_world.value,
            .tint = sprite.tint,
            .sorting_layer = sprite.sorting_layer,
            .sorting_order = sprite.sorting_order,
        });
      });

  std::ranges::sort(output.cameras,
                    [](const RenderCamera& a, const RenderCamera& b) {
                      return guid_less(a.entity, b.entity);
                    });
  std::ranges::sort(output.directional_lights,
                    [](const RenderDirectionalLight& a, const RenderDirectionalLight& b) {
                      return guid_less(a.entity, b.entity);
                    });
  std::ranges::sort(output.meshes,
                    [](const RenderMesh& a, const RenderMesh& b) {
                      return guid_less(a.entity, b.entity);
                    });
  std::ranges::sort(output.sprites, [](const RenderSprite& a, const RenderSprite& b) {
    if (a.sorting_layer != b.sorting_layer) {
      return a.sorting_layer < b.sorting_layer;
    }
    if (a.sorting_order != b.sorting_order) {
      return a.sorting_order < b.sorting_order;
    }
    return guid_less(a.entity, b.entity);
  });

  return output;
}

}  // namespace teng::engine
