#pragma once

// Component reflection authoring DSL (no-op macro surface).
//
// These macros exist so reflection blocks in headers are valid C++.
// They must remain semantically inert at runtime; the repo-local generator
// interprets these tokens as the source of truth.

#define TENG_REFLECT_COMPONENT_BEGIN(TypeName, ComponentKeyStringLiteral)
#define TENG_REFLECT_COMPONENT_END()

#define TENG_REFLECT_MODULE(ModuleKeyStringLiteral, ModuleVersionInt)
#define TENG_REFLECT_SCHEMA_VERSION(SchemaVersionInt)
#define TENG_REFLECT_STORAGE(StorageIdent)
#define TENG_REFLECT_VISIBILITY(VisibilityIdent)
#define TENG_REFLECT_ADD_ON_CREATE(BoolLiteral)

#define TENG_REFLECT_FIELD(MemberName, KindIdent, DefaultExpr, ScriptExposureIdent)
#define TENG_REFLECT_ENUM_FIELD(MemberName, JsonKeyStringLiteral, EnumTypeKeyStringLiteral, \
                                DefaultExpr, ScriptExposureIdent, ...)                      \
  /* no-op */
#define TENG_REFLECT_ASSET_FIELD(MemberName, JsonKeyStringLiteral, AssetKindStringLiteral, \
                                 DefaultExpr, ScriptExposureIdent)                         \
  /* no-op */

#define TENG_ENUM_VALUE(EnumeratorExpr, AuthoredKeyStringLiteral, StableValueInt)

// Default helpers (no-op wrappers; generator defines semantics).
#define DefaultF32(x)
#define DefaultBool(x)
#define DefaultEnum(enumValue, key)
#define DefaultAssetId(str)
