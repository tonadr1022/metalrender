# CMake Linkage Notes

`teng` is the default engine target for normal applications. It is built as a shared library so
existing app targets keep linking the same runtime boundary.

Flecs is private engine runtime state. Do not treat Flecs headers or symbols as part of the shared
`teng` ABI. Code outside `teng` that compiles against Flecs-facing engine headers must link the
static engine target, `teng_static`, so it uses one static engine/Flecs runtime in that final
executable.

Tests and tools that only use stable engine APIs should keep linking `teng` unless they need to
compile code that touches Flecs-facing scene APIs. Scene smoke tests are the current example: they
link through `teng_static` via `teng_engine_smoke`.

`teng` and `teng_static` are app-facing aggregation targets over internal object-library components:
core, platform, gfx, and engine. Those internal component targets are a CMake organization boundary,
not public app link targets. Normal apps should continue to link `teng`; Flecs-facing smoke tests
should continue to link through `teng_engine_smoke`.
