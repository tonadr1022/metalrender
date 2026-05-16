#pragma once

#include <memory>

#include "engine/Engine.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/authoring/SceneDocument.hpp"

namespace teng::engine {

class DebugSceneAuthoringLayer final : public Layer {
 public:
  void on_imgui(EngineContext& ctx) override;

 private:
  void refresh_document(EngineContext& ctx);

  SceneId documented_scene_;
  std::unique_ptr<scene::authoring::SceneDocument> document_;
  EntityGuid last_created_entity_;
};

}  // namespace teng::engine
