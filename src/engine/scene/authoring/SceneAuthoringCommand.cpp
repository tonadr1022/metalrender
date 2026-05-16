#include "engine/scene/authoring/SceneAuthoringCommand.hpp"

#include "engine/scene/authoring/SceneDocument.hpp"

namespace teng::engine::scene::authoring {

void mark_committed(SceneDocument& document, SceneAuthoringMutation) {
  document.mark_committed_for_authoring();
}

void mark_saved(SceneDocument& document) { document.mark_saved_for_authoring(); }

const char* mutation_name(SceneAuthoringMutation mutation) {
  switch (mutation) {
    case SceneAuthoringMutation::EntityCreate:
      return "entity_create";
    case SceneAuthoringMutation::EntityRename:
      return "entity_rename";
    case SceneAuthoringMutation::EntityDestroy:
      return "entity_destroy";
    case SceneAuthoringMutation::ComponentAdd:
      return "component_add";
    case SceneAuthoringMutation::ComponentRemove:
      return "component_remove";
    case SceneAuthoringMutation::ComponentSet:
      return "component_set";
    case SceneAuthoringMutation::ComponentFieldEdit:
      return "component_field_edit";
    case SceneAuthoringMutation::Save:
      return "save";
  }
  return "unknown";
}

}  // namespace teng::engine::scene::authoring
