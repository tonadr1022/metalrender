#include "DebugSceneAuthoringLayer.hpp"

#include <format>
#include <string>

#include "engine/scene/Scene.hpp"
#include "imgui.h"

namespace teng::engine {

void DebugSceneAuthoringLayer::refresh_document(EngineContext& ctx) {
  Scene* active_scene = ctx.scenes().active_scene();
  if (!active_scene) {
    document_.reset();
    documented_scene_ = {};
    return;
  }
  if (document_ && documented_scene_ == active_scene->id()) {
    return;
  }
  documented_scene_ = active_scene->id();
  document_ =
      std::make_unique<scene::authoring::SceneDocument>(*active_scene, ctx.scene_serialization());
}

void DebugSceneAuthoringLayer::on_imgui(EngineContext& ctx) {
  refresh_document(ctx);
  if (!document_ || !ImGui::Begin("Scene Authoring Debug")) {
    if (document_) {
      ImGui::End();
    }
    return;
  }

  ImGui::Text("revision: %llu", static_cast<unsigned long long>(document_->revision()));
  ImGui::Text("dirty: %s", document_->dirty() ? "yes" : "no");
  if (ImGui::Button("Create Entity")) {
    const std::string name = std::format("authored entity {}", document_->revision() + 1);
    Result<EntityGuid> created = document_->create_entity(name);
    if (created) {
      last_created_entity_ = *created;
    }
  }
  if (last_created_entity_) {
    ImGui::Text("last entity: %016llx",
                static_cast<unsigned long long>(last_created_entity_.value));
  }
  ImGui::End();
}

}  // namespace teng::engine
