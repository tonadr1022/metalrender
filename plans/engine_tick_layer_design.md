# Engine Tick And Layer Design

Status: Phase 1 runtime shell implemented. The remaining sections describe the
target direction and the next migration slices.

This note refines the phase-one runtime direction from
`plans/engine_runtime_migration_plan.md`: make `Engine::tick()` the primary
runtime primitive, introduce an ordered layer stack, keep current vktest behavior
working through explicit migration scaffolding, and preserve both Vulkan and
Metal through platform/RHI boundaries.

## Scope

In scope:

- Ownership boundaries for `Engine`, layers, window/platform services, RHI device,
  swapchain, renderer services, scenes, debug UI, and compatibility code.
- Initial layer order and per-frame tick flow.
- How `apps/vktest/TestApp.*` moves from runtime owner to temporary host.
- Backend selection for Vulkan and Metal without adding engine-level
  backend-specific assumptions.
- Phases, risks, validation, and open questions.

Out of scope:

- Implementing Flecs scene storage.
- Rewriting the meshlet renderer.
- Replacing `ResourceManager`.
- Final editor UI architecture.
- New CMake targets beyond what the phases identify.

## Relevant Current Code

Before Phase 1, `apps/vktest/TestApp.cpp` owned most process/runtime concerns:

- Discovers `resources`, sets current working directory, creates `resources/local`,
  and loads/saves CVars.
- Creates a platform window through `create_platform_window()`.
- Hard-codes `gfx::rhi::GfxAPI::Vulkan` when calling `gfx::rhi::create_device`.
- Initializes the RHI device, creates the swapchain, creates `gfx::TestRenderer`,
  initializes global `ResourceManager`, installs input callbacks, and runs the
  main loop.
- Owns the ImGui frame lifecycle and top-level debug shortcuts such as scene
  cycling, demo preset selection, and ImGui toggling.

After Phase 1, `src/engine/Engine.*` owns resource path setup, CVar lifecycle,
window/device/swapchain creation, backend selection, frame timing, queued input
dispatch, ImGui frame begin/end, `Engine::tick()`, and `Engine::run()`.
`apps/vktest/TestApp.*` is now a thin host that builds `EngineConfig`, installs
`CompatibilityVktestLayer`, and calls `Engine::run()`.

`apps/vktest/TestRenderer.cpp` currently combines renderer services with debug
scene orchestration:

- Owns `ShaderManager`, `RenderGraph`, `ImGuiRenderer`, upload/copy managers,
  `ModelGPUMgr`, static instance/draw batches, material storage, samplers, and
  frame-in-flight state.
- Creates and switches `ITestScene` debug scenes.
- For each frame, updates time, forwards to the active scene, handles dirty
  pipelines and pending uploads, bakes/executes the render graph, acquires the
  swapchain image, submits the frame, and advances frame index.
- Exposes debug UI and forwards input to the current debug scene.

`src/Window.*` provides a platform-neutral `Window` interface backed by GLFW.
`create_platform_window()` returns `AppleWindow` on Apple platforms and `Window`
elsewhere. The Apple implementation sets Cocoa/Metal-friendly window state and
provides `set_layer_for_window(GLFWwindow*, CA::MetalLayer*)` for Metal
swapchain integration.

`src/gfx/rhi/Device.*` already defines `gfx::rhi::GfxAPI { Vulkan, Metal }` and
`create_device(GfxAPI)`. Build availability is controlled by `VULKAN_BACKEND`
and `METAL_BACKEND`. Vulkan is enabled by default; Metal is enabled on Apple by
top-level CMake and adds Apple window, Metal device, Metal shader runtime, and
Metal shader compilation targets.

`apps/vktest/main.cpp` is intentionally thin: parse `--quit-after-frames`, build
`TestApp`, and call `run()`. That makes it a good migration host while ownership
moves downward into `engine::Engine`.

## Target Ownership Boundaries

### Executable

The executable should own only process-level choices:

- Parse command-line flags.
- Build `EngineConfig`.
- Select app mode, such as vktest compatibility, editor, or headless smoke run.
- Install requested layers.
- Call `Engine::run()` for standalone mode, or manually call `Engine::tick()` in
  editor/test harnesses.

The executable should not own RHI objects, render graph services, scene lifetime,
or frame lifecycle.

### Engine

`Engine` should own long-lived runtime services:

- Resource/local-resource paths and CVar load/save.
- Platform window creation and shutdown.
- Backend selection, RHI device creation, and swapchain creation.
- Time/frame accounting.
- Input collection and callback dispatch into engine events.
- Ordered `LayerStack`.
- Shared `EngineContext` references to platform, RHI, swapchain, resource,
  scene, render, and debug services.
- Coordinated startup and shutdown.

`Engine` should not know about `ITestScene`, `TestDebugScene`, meshlet demo
presets, or `gfx::TestRenderer` except through an explicitly named compatibility
layer during migration.

### Layers

Layers should be the unit of runtime composition. A layer may read services from
`EngineContext`, register callbacks/systems, and perform work during tick phases,
but it should not outlive the engine-owned services it references.

Initial layer categories:

- `RuntimeLayer`: advances active scene simulation and engine systems.
- `RenderLayer`: performs scene-to-render extraction and renderer submission.
- `ImGuiDebugLayer`: manages ImGui frame participation and generic debug panels.
- `EditorLayer`: future editor tools over the same runtime.
- `CompatibilityVktestLayer`: temporary adapter for `TestRenderer` and current
  debug scenes.
- `ScriptingLayer`: future script system scheduling, ordered after input and
  before render extraction.

### Render Services

The long-term render service should own renderer-wide frame services currently
inside `TestRenderer`: shader manager, render graph, upload/copy managers, ImGui
renderer integration, GPU resource residency, and active `IRenderer`.

Gameplay/data scenes should not receive `RenderGraph`. Render graph construction
belongs to renderer implementations after the engine has produced a render
snapshot from scene data.

### Platform And RHI

Engine-level code should depend on:

- `Window` and `create_platform_window()`.
- `gfx::rhi::Device`, `Swapchain`, and `gfx::rhi::create_device(GfxAPI)`.
- RHI capability queries and shader target queries.

Engine-level code should not include Vulkan or Metal headers, call backend
concrete types, or special-case Metal layer setup directly. Backend-specific
window/swapchain work should remain in platform/RHI files.

## Layer Order

The first runtime stack should be deterministic and simple:

1. Platform/input pump.
2. Engine pre-frame service update.
3. Runtime/scripting layers.
4. Editor layer, when enabled.
5. Render layer.
6. ImGui/debug layer participation.
7. Engine post-frame and present completion.

The exact ImGui split needs care because current code calls `ImGui::NewFrame()`
before app/debug UI and `ImGui::Render()` before renderer submission. The layer
model should preserve that lifecycle while avoiding hidden ImGui work in random
layers:

- Engine or an ImGui service starts the ImGui frame if ImGui is enabled.
- Layers that expose UI contribute during an `on_imgui` phase.
- Engine or the ImGui service finalizes ImGui draw data before render submission.
- Render layer passes the draw data to the renderer service.

Compatibility mode can initially keep `TestRenderer::render(imgui_enabled)` as a
single render-layer operation, but that should be labeled as temporary.

## Engine::tick() Flow

`Engine::tick()` should perform exactly one bounded frame and return whether the
engine should continue. `Engine::run()` should be only a loop over `tick()`.

Target flow:

1. Return `false` if shutdown has been requested or the window should close.
2. Poll platform events.
3. Compute `EngineTime` from the previous frame.
4. Begin a new frame in engine-owned services.
5. Dispatch queued input/window events to layers in order.
6. Begin ImGui frame if enabled.
7. Run update phases for runtime, scripting, editor, and compatibility layers.
8. Run `on_imgui` for layers that contribute debug/editor UI.
9. Finalize ImGui frame if enabled.
10. Render/present through the render layer or compatibility layer.
11. Advance frame counters and frame-in-flight state.
12. Run deferred cleanup and return `true` unless a quit condition was reached.

Smoke-test quitting is an `EngineConfig` run policy. `--quit-after-frames 30`
counts completed frames and exits cleanly after rendering the requested number
of frames.

## TestApp As Migration Scaffolding

`TestApp` is migration scaffolding, not engine architecture.

Temporary role:

- Keep the existing `vktest` executable and smoke-test behavior stable.
- Continue hosting current debug scenes while the engine skeleton appears.
- Provide a safe place to adapt command-line flags into `EngineConfig`.
- Install a temporary compatibility layer that delegates to `gfx::TestRenderer`.

Migration path:

1. Done: resource path setup, CVar lifecycle, window/device/swapchain creation,
   frame timing, and quit-after-frame handling live in `Engine`.
2. Done: `TestApp::run()` delegates to `Engine::run()`.
3. Done: key/cursor callback forwarding flows through engine input events into
   `CompatibilityVktestLayer`.
4. Done for Phase 1: ImGui frame begin/end lives in `Engine`; the vktest debug
   panel lives in the compatibility layer.
5. Move renderer-owned services from `TestRenderer` into `RenderService` in later
   phases.

Retirement criteria:

- `apps/vktest/main.cpp` can create `engine::Engine` directly.
- `TestApp` no longer owns `Window`, `Device`, `Swapchain`, `ResourceManager`, or
  `gfx::TestRenderer`.
- vktest-specific scene switching and demo presets live in a compatibility/debug
  layer or in data-driven demo scene loaders.
- Removing `TestApp` does not affect engine runtime, editor, or renderer service
  ownership.

## TestRenderer As Migration Scaffolding

`gfx::TestRenderer` is migration scaffolding.

Temporary role:

- Preserve current meshlet/debug-scene rendering while `Engine::tick()` and layers
  are introduced.
- Serve as the source for extracting `RenderService` responsibilities.
- Keep `ResourceManager::init()` viable until asset and GPU residency boundaries
  are redesigned.

Retirement criteria:

- `RenderService` owns shader/render graph/upload/model GPU services.
- `IRenderer` implementations consume `RenderScene` snapshots instead of
  `ITestScene` owning render graph pass wiring.
- Debug scene switching no longer sits in the renderer service.
- `ResourceManager` singleton usage is wrapped or removed from runtime paths.

## Backend Selection

Current state:

- CMake builds Vulkan by default through `BUILD_VULKAN`.
- CMake builds Metal on Apple through `BUILD_METAL`.
- `gfx::rhi::create_device(GfxAPI)` already returns a Vulkan or Metal device when
  the corresponding backend is compiled.
- `EngineConfig` now exposes platform default, Vulkan, and Metal choices.
- `vktest` currently requests Vulkan explicitly to preserve local behavior.

Implemented direction:

- Backend choice is centralized in `EngineConfig`.
- Platform default resolves centrally:
  - Apple: prefer Metal when `METAL_BACKEND` is available, optionally allow
    Vulkan when built and requested.
  - Non-Apple: prefer Vulkan when `VULKAN_BACKEND` is available.
- Fail early with a clear log message if the requested backend was not compiled.
- Keep swapchain creation backend-neutral through `Device::create_swapchain` and
  `SwapchainDesc{ .window = window.get(), ... }`.
- Keep shader target selection in renderer/shader services through
  `Device::get_supported_shader_targets()`.

Backend selection should not imply renderer selection. The same engine config
may choose Vulkan or Metal as the RHI backend while a separate renderer config
selects meshlet, 2D, or future renderer implementations.

## Phased Implementation Sequence

### Phase 0: Guardrail Design

Deliverables:

- Land this design note.
- Keep `plans/engine_runtime_migration_plan.md` as the broad migration plan.
- Do not change runtime code in this phase.

Validation:

- Planning review only.

### Phase 1: Engine Shell And Config

Status: complete.

Delivered:

- Add `EngineConfig`, `EngineTime`, `EngineContext`, `Engine`, `Layer`, and
  `LayerStack`.
- Move backend selection, window/device/swapchain ownership, CVar lifecycle,
  quit-after-frame handling, and resource path setup into `Engine`.
- Implement `Engine::tick()` as the primary primitive and `Engine::run()` as a
  helper loop.
- Keep `TestApp` as a thin compatibility host.

Validation:

- `./scripts/agent_verify.sh`
- `./build/Debug/bin/vktest --quit-after-frames 30`

### Phase 2: Compatibility Layer Cleanup

Deliverables:

- Keep the current `CompatibilityVktestLayer` behavior stable while deciding
  whether it should remain inside `TestApp.cpp` or move to a named app file.
- Add focused smoke coverage or a small app-side harness proving manual
  `Engine::tick()` usage.
- Preserve Tab scene cycling, Ctrl+0..9 demo presets, Alt+G ImGui toggle,
  render graph dump UI, and meshlet startup preset behavior.

Validation:

- Existing smoke test.
- Interactive vktest sanity check for scene cycling and ImGui toggle.

### Phase 3: ImGui And Debug Layer Split

Deliverables:

- Centralize ImGui frame begin/end in engine or an ImGui service.
- Move generic app debug UI out of `TestApp`.
- Keep scene-specific debug UI behind the compatibility layer until debug scenes
  migrate.

Validation:

- Smoke test with ImGui enabled.
- Interactive check that render graph dump UI and scene overlays still appear.

### Phase 4: RenderService Extraction

Deliverables:

- Move renderer-wide services out of `TestRenderer` into `RenderService` where
  practical.
- Establish the future `RenderScene -> IRenderer -> RenderGraph/RHI` boundary.
- Keep `TestRenderer` or an adapter alive only for unmigrated debug scenes.

Validation:

- Smoke test.
- Render graph dump mode still works.
- Shader hot-reload/dirty pipeline path still works where supported.

### Phase 5: Data Scene Integration

Deliverables:

- Introduce Flecs-backed scenes and stable IDs as described in the main runtime
  migration plan.
- Move demo scene data toward ECS components and render extraction.
- Retire `ITestScene` from runtime scene architecture.

Validation:

- Smoke test.
- Minimal ECS scene can tick and render.
- Existing meshlet preset still renders through either migrated data or explicit
  compatibility path.

## Risks And Tradeoffs

- Moving ownership too aggressively could break vktest before replacement
  services exist. Keep early phases small and retain compatibility bridges.
- ImGui lifecycle is currently interleaved between `TestApp` and `TestRenderer`.
  Centralizing it requires preserving exact begin/render/end ordering.
- `TestRenderer` owns many services that are both renderer infrastructure and
  debug-scene support. Extract only stable service boundaries first.
- Backend selection may expose gaps in Metal parity. Engine code should surface
  unsupported backend choices clearly without encoding Vulkan assumptions.
- Layer order can become ambiguous if render, editor, and debug UI all mutate
  scene state. Define update phases early and keep render extraction after scene
  mutation.
- Global `ResourceManager` makes asset ownership fuzzy. Treat it as a temporary
  facade until asset IDs and renderer GPU residency are separated.

## Validation Strategy

Phase 1 implementation validation:

- Run `./scripts/agent_verify.sh` from repo root.
- Run `./build/Debug/bin/vktest --quit-after-frames 30`.
- Confirm `src/engine` does not include Vulkan or Metal concrete backend
  headers.

Validation for future code phases:

- Run `./scripts/agent_verify.sh` from repo root.
- Run `./build/Debug/bin/vktest --quit-after-frames 30`.
- For backend-selection work, validate all compiled backend combinations that the
  local platform supports:
  - Vulkan-only build.
  - Metal build on Apple.
  - Vulkan+Metal build on Apple if supported by the environment.
- For HLSL or shared shader include changes, run `teng-shaderc --all` through
  `agent_verify.sh`.

## Open Questions

- Should backend selection become a `vktest` command-line flag before Metal
  parity work, or stay as an `EngineConfig` field for now?
- Should ImGui frame ownership remain directly in `Engine`, or move into an
  engine-owned `ImGuiService` before editor work starts?
- Should `CompatibilityVktestLayer` stay in `TestApp.cpp`, move to a named
  app-side source file, or call a smaller adapter as `TestRenderer` extraction
  proceeds?
- When should `ResourceManager::init()` leave compatibility code and become an
  engine asset/resource service boundary?
- What is the first Metal validation target: successful device/window/swapchain
  startup, or full parity with the current meshlet vktest path?
