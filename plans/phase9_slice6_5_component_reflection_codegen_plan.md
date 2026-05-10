# Phase 9 Slice 6.5: Clang component reflection codegen

**Status:** implemented with Clang Tooling.

**Parent plan:** [`component_schema_authoring_implementation_plan.md`](component_schema_authoring_implementation_plan.md).  
**Scene byte contract:** [`scene_serialization_design.md`](scene_serialization_design.md).  
**Next dependent slice:** [`phase9_slice7_cooked_scene_v2_plan.md`](phase9_slice7_cooked_scene_v2_plan.md).

## Purpose

Component metadata, Flecs registration, JSON serialization, editor/script exposure flags, enum authored
keys, and future cooked field encoding now come from annotated C++ component declarations instead of
parallel handwritten tables or a Python macro parser.

## Implemented Direction

The authoring surface is declaration-local and AST-backed:

```cpp
struct TENG_COMPONENT(key = "teng.core.camera",
                      module = "teng.core",
                      schema_version = 1,
                      storage = "Authored",
                      visibility = "Editable") Camera {
  TENG_FIELD(script = "ReadWrite")
  float fov_y{1.04719755f};
};
```

`TENG_COMPONENT`, `TENG_FIELD`, and `TENG_ENUM_VALUE` expand to `clang::annotate` payloads because
unknown `[[teng::...]]` attributes are not preserved in Clang's semantic AST. `teng-component-codegen`
uses Clang Tooling to inspect real declarations, infer field kinds from C++ types, and emit build-dir
generated C++ glue.

## Generated Runtime Surface

- Tool target: `teng-component-codegen`.
- Generator source: `tools/component_codegen/ComponentCodegen.cpp`.
- Annotation carrier: `src/engine/scene/ComponentReflectionMacros.hpp`.
- Generated files stay under `${CMAKE_BINARY_DIR}/generated/...`.
- Generated delegates preserve the existing public entrypoints:
  - `register_core_components(ComponentRegistryBuilder&)`
  - `register_flecs_core_components(FlecsComponentContextBuilder&)`
  - `register_builtin_component_serialization(SceneSerializationContextBuilder&)`

## Current Scope

- Core components in `SceneComponents.hpp` and `EngineInputSnapshot` in `Input.hpp` are generated.
- The external test extension component is generated and still proves non-core extension.
- Negative codegen tests cover duplicate component keys, duplicate field keys, duplicate enum keys,
  and missing required metadata.
- `EntityGuidComponent` and `Name` remain special identity/document metadata and are intentionally not
  reflected.

## Follow-up Work

- Add richer source-location diagnostics from the Clang tool.
- Add generic typed field get/set thunks for editor/script consumers.
- Decide later whether exact `[[teng::component(... )]]` spelling is worth a custom Clang attribute
  plugin. The current runtime/generated surface should not need to change if that happens.
