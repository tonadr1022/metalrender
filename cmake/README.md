# CMake Linkage Notes

`teng_runtime` is the default engine target for normal applications. It is built as a static
library so shipped-style app targets link engine core, scene, and Flecs into the executable.

Flecs is private engine runtime state. Do not introduce a shared engine ABI that exports Flecs
headers or symbols by accident. Code outside the engine that compiles against Flecs-facing scene
headers must link `teng_runtime`, so it uses one static engine/Flecs runtime in the final
executable.

Tests and tools that need the full runtime should link `teng_runtime`. Minimal tools should keep
their own narrow libraries instead of pulling the runtime aggregate; `teng-shaderc` is the current
example and links only `teng_shader_compiler`.

`teng_runtime` is the app-facing aggregation target over internal object-library components: core,
platform, gfx, and engine. Those internal component targets are a CMake organization boundary, not
public app link targets. Normal apps should link `teng_runtime`; scene smoke tests link it through
`teng_engine_smoke`.
