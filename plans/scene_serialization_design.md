# Scene serialization (design note)

**Status:** framing only — implementation tracked under Phase 12 in [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md).

## Problem

Interim TOML (`schema_version = 1`, `SceneAssetLoader`) is **load-only**, **hand-maintained per component**, and drifts from ECS (`SpriteRenderable` exists in code but not in the loader). That blocks:

- Editor **save**
- **Single source of truth** for component shapes on disk
- **Cooked binary** scenes for shipped games

## Direction

1. **Serializable vs derived:** authored components round-trip; derived data (e.g. `LocalToWorld` from `Transform`) may be omitted or optional on disk with clear rules.
2. **Stable identities:** persist `EntityGuid`, `AssetId`, `SceneId` as today; disk layout may gain chunking or intern tables for binary without changing identity semantics.
3. **Canonical vs cooked:** human-mergeable authoring format (text or structured) vs compact binary for runtime; cook step may be CLI or build pipeline.
4. **Component registry:** one mechanism (reflection, tables, or codegen) drives **load/save**, future **inspector**, and **version migration**.
5. **Breaking changes:** schema bumps are expected—ship migration tools or dual-load windows alongside generator updates.

## Exit criteria (see Phase 12)

Editor or engine can save/load the same entity graph through shared serialization code; interim TOML v1 is migrated or wrapped, not the only truth.

## Related

- Interim format summary: [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) § Scene format v1 (interim).
- Asset registry/cooked manifests: [`asset_registry_implementation_plan.md`](asset_registry_implementation_plan.md).
