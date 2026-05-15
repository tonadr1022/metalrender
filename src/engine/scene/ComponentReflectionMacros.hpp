#pragma once

// Component reflection authoring annotations.
//
// Clang drops unknown vendor attributes from the semantic AST, so v1 reflection
// uses clang::annotate as the preserved carrier. The codegen tool interprets the
// string payloads; runtime compilation treats them as inert declaration metadata.

#define TENG_COMPONENT(...) [[clang::annotate("teng.component:" #__VA_ARGS__)]]
#define TENG_FIELD(...) [[clang::annotate("teng.field:" #__VA_ARGS__)]]
#define TENG_ENUM_VALUE(...) [[clang::annotate("teng.enum_value:" #__VA_ARGS__)]]
// Single numeric literal for both reflection stable value and C++ enumerator initializer.
#define TENG_ENUM_N(v) TENG_ENUM_VALUE(value = v) = (v)
