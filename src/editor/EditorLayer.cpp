#include "editor/EditorLayer.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float4.hpp>
#include <string>
#include <utility>

#include "core/Logger.hpp"
#include "engine/render/RenderFrameContext.hpp"
#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/render/RenderService.hpp"
#include "engine/scene/Scene.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "gfx/rhi/Texture.hpp"
#include "imgui.h"
#include "imgui_internal.h"

namespace teng::editor {

namespace {

[[nodiscard]] std::string scene_id_label(teng::engine::SceneId id) {
  return id.is_valid() ? std::to_string(id.value) : std::string{"none"};
}

[[nodiscard]] std::string entity_guid_label(
    const std::optional<teng::engine::EntityGuid>& selected) {
  if (!selected) {
    return "none";
  }
  return std::format("{:016x}", selected->value);
}

}  // namespace

EditorLayer::EditorLayer(engine::SceneId edit_scene_id,
                         std::optional<std::filesystem::path> scene_path)
    : edit_scene_id_(edit_scene_id), scene_path_(std::move(scene_path)) {}

void EditorLayer::on_attach(engine::EngineContext& ctx) {
  (void)session_.bind(ctx, edit_scene_id_, scene_path_);
  // Phase 10 scaffolding: this edit-mode boolean should retire into an explicit
  // scene role/tick policy model before richer editor previews or multi-scene ticking.
  ctx.set_scene_tick_enabled(false);
}

void EditorLayer::on_imgui(engine::EngineContext& ctx) {
  if (!ctx.imgui_enabled()) {
    return;
  }

  draw_dockspace();
  draw_viewport(ctx);
  draw_hierarchy(ctx);
  draw_inspector();
  draw_stats(ctx);
}

void EditorLayer::on_render_scene(engine::EngineContext& ctx) {
  if (session_.mode() != EditorMode::Edit || !session_.bound()) {
    return;
  }

  ctx.renderer().begin_scene_presentation(engine::RenderPresentation{
      .scene_extent = viewport_pixel_size_,
      .color_target = viewport_target_.handle(),
  });

  viewport_camera_.update(ctx.input(), ctx.time().delta_seconds, viewport_accepts_input_);
  const engine::SceneRenderView view = viewport_camera_.make_render_view(viewport_pixel_size_);
  if (!view.valid) {
    return;
  }
  ctx.renderer().enqueue_active_scene(view);
}

namespace {

void setup_default_dock_layout(const ImGuiID dockspace_id, const ImVec2& dockspace_size) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, dockspace_size);

  ImGuiID dock_main_id = dockspace_id;
  const ImGuiID dock_left_id =
      ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.22f, nullptr, &dock_main_id);
  const ImGuiID dock_right_id =
      ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.26f, nullptr, &dock_main_id);
  const ImGuiID dock_bottom_id =
      ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.26f, nullptr, &dock_main_id);

  ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
  ImGui::DockBuilderDockWindow("Hierarchy", dock_left_id);
  ImGui::DockBuilderDockWindow("Inspector", dock_right_id);
  ImGui::DockBuilderDockWindow("Stats", dock_bottom_id);
  ImGui::DockBuilderFinish(dockspace_id);
}

}  // namespace

void EditorLayer::draw_dockspace() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);

  const ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
  ImGui::Begin("Teng Editor Dockspace", nullptr, window_flags);
  ImGui::PopStyleVar(3);

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save", nullptr, false, session_.can_save())) {
        (void)session_.save();
      }
      ImGui::MenuItem("Reload", nullptr, false, false);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Play")) {
      ImGui::MenuItem("Play", nullptr, false, false);
      ImGui::MenuItem("Stop", nullptr, false, false);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }

  const ImGuiID dockspace_id = ImGui::GetID("TengEditorDockspace");
  if (!dock_layout_initialized_) {
    dock_layout_initialized_ = true;
    setup_default_dock_layout(dockspace_id, viewport->WorkSize);
  }
  ImGui::DockSpace(dockspace_id, ImVec2{0.0f, 0.0f});
  ImGui::End();
}

void EditorLayer::draw_viewport(engine::EngineContext& ctx) {
  ImGui::Begin("Viewport");
  const ImGuiIO& io = ImGui::GetIO();
  viewport_accepts_input_ =
      (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered()) &&
      !io.WantTextInput;

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  viewport_pixel_size_ = clamp_editor_viewport_extent(glm::uvec2{
      static_cast<unsigned>(std::max(0.f, std::floor(avail.x * io.DisplayFramebufferScale.x))),
      static_cast<unsigned>(std::max(0.f, std::floor(avail.y * io.DisplayFramebufferScale.y))),
  });
  LINFO("viewport_pixel_size_: {} {}", viewport_pixel_size_.x, viewport_pixel_size_.y);

  const gfx::rhi::Texture* const swapchain_tex =
      ctx.device().get_tex(ctx.swapchain().get_current_texture());
  viewport_target_.ensure_size(ctx.device(), swapchain_tex->desc().format, viewport_pixel_size_);

  if (viewport_target_.valid()) {
    ImGui::Image(gfx::MakeImGuiTexRefTextureHandle(viewport_target_.handle()), avail);
  } else {
    ImGui::TextUnformatted("Viewport");
  }
  ImGui::End();
}

void EditorLayer::draw_hierarchy(engine::EngineContext& ctx) {
  ImGui::Begin("Hierarchy");
  if (session_.bound()) {
    const engine::Scene& edit_scene = session_.document_controller().document().scene();
    ImGui::Text("Edit scene: %s", edit_scene.name().c_str());
  } else {
    const engine::Scene* active_scene = ctx.scenes().active_scene();
    if (active_scene) {
      ImGui::Text("Active scene: %s", active_scene->name().c_str());
    } else {
      ImGui::TextUnformatted("No edit document");
    }
  }
  ImGui::End();
}

void EditorLayer::draw_inspector() {
  ImGui::Begin("Inspector");
  ImGui::TextUnformatted("No selection");
  ImGui::End();
}

void EditorLayer::draw_stats(engine::EngineContext& ctx) {
  ImGui::Begin("Stats");
  const engine::Scene* active_scene = ctx.scenes().active_scene();
  ImGui::Text("Frame: %llu", static_cast<unsigned long long>(ctx.time().frame_index));
  ImGui::Text("Mode: %s", to_string(session_.mode()));
  if (active_scene) {
    ImGui::Text("Active scene id: %s", scene_id_label(active_scene->id()).c_str());
    ImGui::Text("Active scene name: %s", active_scene->name().c_str());
  } else {
    ImGui::TextUnformatted("Active scene id: none");
  }

  if (session_.bound()) {
    const EditorDocumentController& document = session_.document_controller();
    ImGui::Separator();
    ImGui::Text("Edit scene id: %s", scene_id_label(document.edit_scene_id()).c_str());
    ImGui::Text("Path: %s", document.path() ? document.path()->string().c_str() : "(unsaved)");
    ImGui::Text("Dirty: %s", document.dirty() ? "yes" : "no");
    ImGui::Text("Revision: %llu", static_cast<unsigned long long>(document.revision()));
    ImGui::Text("Saved revision: %llu", static_cast<unsigned long long>(document.saved_revision()));
  } else {
    ImGui::Separator();
    ImGui::TextUnformatted("Edit document: unbound");
  }
  ImGui::Text("Selected: %s", entity_guid_label(session_.selection().selected()).c_str());
  ImGui::TextWrapped("Status: %s", session_.last_status().c_str());
  ImGui::End();
}

}  // namespace teng::editor
