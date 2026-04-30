#include "engine/ImGuiOverlayLayer.hpp"

#include <GLFW/glfw3.h>

#include <filesystem>

#include "UI.hpp"
#include "Window.hpp"
#include "engine/render/RenderService.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"

namespace teng::engine {

void ImGuiOverlayLayer::on_attach(EngineContext& ctx) { init_imgui(ctx); }

void ImGuiOverlayLayer::on_detach(EngineContext& ctx) {
  if (frame_started_) {
    ImGui::EndFrame();
    frame_started_ = false;
  }
  ctx.renderer().shutdown_imgui_renderer();
  shutdown_imgui();
}

void ImGuiOverlayLayer::on_update(EngineContext& ctx, const EngineTime&) {
  if (!ctx.imgui_enabled()) {
    return;
  }
  if (!imgui_initialized_) {
    init_imgui(ctx);
  }
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  frame_started_ = true;
}

void ImGuiOverlayLayer::on_imgui(EngineContext& ctx) {
  if (frame_started_) {
    ctx.renderer().on_imgui();
  }
  if (frame_started_) {
    ImGui::Render();
  }
}

void ImGuiOverlayLayer::on_render(EngineContext& ctx) {
  ctx.renderer().set_imgui_ui_active(ctx.imgui_enabled() && frame_started_);
  if (frame_started_) {
    ctx.renderer().enqueue_imgui_overlay_pass();
  }
}

void ImGuiOverlayLayer::on_end_frame(EngineContext&) {
  if (frame_started_) {
    ImGui::EndFrame();
    frame_started_ = false;
  }
}

void ImGuiOverlayLayer::on_key_event(EngineContext& ctx, int key, int action, int mods) {
  if (action == GLFW_PRESS && key == GLFW_KEY_G && (mods & GLFW_MOD_ALT) != 0) {
    ctx.toggle_imgui_enabled();
  }
}

void ImGuiOverlayLayer::init_imgui(EngineContext& ctx) {
  if (imgui_initialized_) {
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  const std::filesystem::path font_dir = ctx.resource_dir() / "fonts";
  if (std::filesystem::exists(font_dir)) {
    for (const auto& entry : std::filesystem::directory_iterator(font_dir)) {
      if (entry.path().extension() == ".ttf") {
        auto* font = io.Fonts->AddFontFromFileTTF(entry.path().string().c_str(), 16, nullptr,
                                                  io.Fonts->GetGlyphRangesDefault());
        add_font(entry.path().stem().string(), font);
      }
    }
  }

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendRendererName = "imgui_impl_memes";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  ImGui_ImplGlfw_InitForOther(ctx.window().get_handle(), true);
  imgui_initialized_ = true;
}

void ImGuiOverlayLayer::shutdown_imgui() {
  if (!imgui_initialized_) {
    return;
  }
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  imgui_initialized_ = false;
}

}  // namespace teng::engine
