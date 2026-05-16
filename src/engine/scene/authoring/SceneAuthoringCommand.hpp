#pragma once

#include <string_view>

namespace teng::engine::scene::authoring {

class SceneDocument;

enum class SceneAuthoringMutation {
  EntityCreate,
  EntityRename,
  EntityDestroy,
  ComponentAdd,
  ComponentRemove,
  ComponentSet,
  ComponentFieldEdit,
  Save,
};

void mark_committed(SceneDocument& document, SceneAuthoringMutation mutation);
void mark_saved(SceneDocument& document);

[[nodiscard]] const char* mutation_name(SceneAuthoringMutation mutation);

}  // namespace teng::engine::scene::authoring
