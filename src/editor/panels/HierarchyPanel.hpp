#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/scene/SceneIds.hpp"

namespace teng::editor {

class EditorSession;

class HierarchyPanel {
 public:
  void draw(EditorSession& session);

 private:
  struct Row {
    teng::engine::EntityGuid guid;
    std::string label;
  };

  void rebuild_rows(EditorSession& session);
  void invalidate_cache();

  std::vector<Row> rows_;
  teng::engine::SceneId cached_scene_id_;
  uint64_t cached_revision_{};
  bool cache_valid_{};
};

}  // namespace teng::editor
