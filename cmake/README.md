# CMake Linkage Notes

`teng_runtime` is the default engine target for normal applications. It is an interface aggregate
over the static runtime component libraries, so shipped-style app targets link engine core, scene,
Flecs, platform, renderer, and gfx into the executable.

Flecs is private engine runtime state. Do not introduce a shared engine ABI that exports Flecs
headers or symbols by accident. Code outside the engine that compiles against Flecs-facing scene
headers must link `teng_runtime`, so it uses one static engine/Flecs runtime in the final
executable.

Tests and tools that need the full runtime should link `teng_runtime`. Minimal tools should keep
their own narrow libraries instead of pulling the runtime aggregate; `teng-shaderc` is the current
example and links only `teng_shader_compiler`.

Scene serialization uses `nlohmann_json::nlohmann_json`, pinned through `FetchContent` in
`third_party/CMakeLists.txt` at v3.11.3. Canonical scene files are JSON-only; project configuration
and asset sidecars may still use TOML.

The current component DAG is:

```text
teng_runtime -> teng_engine_runtime -> teng_render -> teng_gfx -> teng_platform -> teng_core
                                      -> teng_cvars -> teng_core
                                      -> teng_scene -> teng_assets -> teng_core

teng_scene_tool_lib -> teng_scene + teng_assets + teng_core
teng-scene-tool -> teng_scene_tool_lib
```

`teng_scene_tool_lib` is a GPU-free static library for scene validate/cook/dump; the `teng-scene-tool`
executable links it and is built by `scripts/agent_verify.sh`. The library must not gain platform,
renderer, gfx, Vulkan, Metal, ImGui, or CVar dependencies. `teng_runtime` is a pure interface
aggregate and does not compile an anchor source or add backend compile definitions directly; backend
flags live on concrete backend-using component targets. `AssetService` stays in
`teng_engine_runtime` for now because its public API owns model loader/runtime model types.
