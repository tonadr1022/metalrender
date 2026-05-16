#pragma once

namespace teng::engine {

struct RenderFrameContext;
struct RenderScene;
struct SceneRenderView;

class IRenderer {
 public:
  virtual ~IRenderer() = default;
  virtual void on_resize(RenderFrameContext&) {}
  virtual void render(RenderFrameContext& frame, const RenderScene& scene,
                      const SceneRenderView& view) = 0;
  virtual void on_imgui(RenderFrameContext&) {}
};

}  // namespace teng::engine
