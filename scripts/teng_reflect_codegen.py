#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Optional


@dataclass(frozen=True)
class EnumValue:
    enumerator_expr: str
    key: str
    stable_value: int


@dataclass(frozen=True)
class Field:
    member: str
    kind: str
    default_expr: str
    script_exposure: str
    json_key: Optional[str] = None
    asset_kind: Optional[str] = None
    enum_key: Optional[str] = None
    enum_values: tuple[EnumValue, ...] = ()


@dataclass(frozen=True)
class Component:
    cpp_type: str
    component_key: str
    module_id: str
    module_version: int
    schema_version: int
    storage: str
    visibility: str
    add_on_create: bool
    fields: tuple[Field, ...]


@dataclass(frozen=True)
class OutputOptions:
    basename: str
    namespace: str
    function_prefix: str
    header_includes: tuple[str, ...]
    typed_thunks: bool


class CodegenError(RuntimeError):
    pass


def _read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except Exception as exc:
        raise CodegenError(f"failed to read {path}: {exc}") from exc


def _strip_cpp_comments(text: str) -> str:
    # Remove // comments but keep string literals intact (good enough for fixtures).
    out: list[str] = []
    i = 0
    in_str = False
    escape = False
    while i < len(text):
        c = text[i]
        if in_str:
            out.append(c)
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == '"':
                in_str = False
            i += 1
            continue

        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue

        if c == "/" and i + 1 < len(text) and text[i + 1] == "/":
            # skip to end of line
            i = text.find("\n", i)
            if i == -1:
                break
            out.append("\n")
            i += 1
            continue

        out.append(c)
        i += 1
    return "".join(out)


def _split_top_level_commas(arg_text: str) -> list[str]:
    items: list[str] = []
    depth = 0
    in_str = False
    escape = False
    start = 0
    for i, c in enumerate(arg_text):
        if in_str:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == '"':
                in_str = False
            continue
        if c == '"':
            in_str = True
            continue
        if c == "(":
            depth += 1
            continue
        if c == ")":
            depth -= 1
            continue
        if c == "," and depth == 0:
            items.append(arg_text[start:i].strip())
            start = i + 1
    tail = arg_text[start:].strip()
    if tail:
        items.append(tail)
    return items


def _parse_string_literal(token: str) -> str:
    token = token.strip()
    if len(token) < 2 or not (token.startswith('"') and token.endswith('"')):
        raise CodegenError(f"expected string literal, got: {token}")
    # This is fixtures-only; decode simple escape sequences via json.
    try:
        return json.loads(token)
    except Exception as exc:
        raise CodegenError(f"invalid string literal {token}: {exc}") from exc


def _parse_bool_literal(token: str) -> bool:
    token = token.strip()
    if token == "true":
        return True
    if token == "false":
        return False
    raise CodegenError(f"expected bool literal, got: {token}")


def _parse_int(token: str) -> int:
    token = token.strip()
    try:
        return int(token, 0)
    except Exception as exc:
        raise CodegenError(f"expected integer literal, got: {token}") from exc


_INVOCATION_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def _find_invocations(text: str, *, start: int = 0) -> Iterable[tuple[str, str, int, int]]:
    """
    Yields (name, arg_text, begin_index, end_index_exclusive) for macro-like invocations.
    Uses balanced-paren scanning; ignores nesting correctness inside strings.
    """
    i = start
    while True:
        m = _INVOCATION_RE.search(text, i)
        if not m:
            return
        name = m.group(1)
        open_paren = m.end(0) - 1
        depth = 0
        in_str = False
        escape = False
        j = open_paren
        while j < len(text):
            c = text[j]
            if in_str:
                if escape:
                    escape = False
                elif c == "\\":
                    escape = True
                elif c == '"':
                    in_str = False
                j += 1
                continue
            if c == '"':
                in_str = True
                j += 1
                continue
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    arg_text = text[open_paren + 1 : j]
                    yield (name, arg_text, m.start(1), j + 1)
                    i = j + 1
                    break
            j += 1
        else:
            raise CodegenError(f"unterminated invocation: {name}(")


def parse_headers(headers: list[Path]) -> list[Component]:
    components: list[Component] = []
    seen_component_keys: set[str] = set()

    for header in headers:
        raw = _read_text(header)
        text = _strip_cpp_comments(raw)
        invocations = list(_find_invocations(text))

        idx = 0
        while idx < len(invocations):
            name, arg_text, _, _ = invocations[idx]
            if name != "TENG_REFLECT_COMPONENT_BEGIN":
                idx += 1
                continue

            begin_inv = invocations[idx]
            begin_args = _split_top_level_commas(begin_inv[1])
            if len(begin_args) != 2:
                raise CodegenError(
                    f"{header}: TENG_REFLECT_COMPONENT_BEGIN expects 2 args, got {len(begin_args)}"
                )
            cpp_type = begin_args[0].strip()
            component_key = _parse_string_literal(begin_args[1])

            module_id: Optional[str] = None
            module_version: Optional[int] = None
            schema_version: Optional[int] = None
            storage: Optional[str] = None
            visibility: Optional[str] = None
            add_on_create: Optional[bool] = None
            fields: list[Field] = []
            seen_field_keys: set[str] = set()

            idx += 1
            while idx < len(invocations):
                name, arg_text, _, _ = invocations[idx]
                if name == "TENG_REFLECT_COMPONENT_END":
                    break

                if name == "TENG_REFLECT_MODULE":
                    args = _split_top_level_commas(arg_text)
                    if len(args) != 2:
                        raise CodegenError(f"{header}: TENG_REFLECT_MODULE expects 2 args")
                    module_id = _parse_string_literal(args[0])
                    module_version = _parse_int(args[1])
                elif name == "TENG_REFLECT_SCHEMA_VERSION":
                    args = _split_top_level_commas(arg_text)
                    if len(args) != 1:
                        raise CodegenError(
                            f"{header}: TENG_REFLECT_SCHEMA_VERSION expects 1 arg"
                        )
                    schema_version = _parse_int(args[0])
                elif name == "TENG_REFLECT_STORAGE":
                    args = _split_top_level_commas(arg_text)
                    if len(args) != 1:
                        raise CodegenError(f"{header}: TENG_REFLECT_STORAGE expects 1 arg")
                    storage = args[0].strip()
                elif name == "TENG_REFLECT_VISIBILITY":
                    args = _split_top_level_commas(arg_text)
                    if len(args) != 1:
                        raise CodegenError(
                            f"{header}: TENG_REFLECT_VISIBILITY expects 1 arg"
                        )
                    visibility = args[0].strip()
                elif name == "TENG_REFLECT_ADD_ON_CREATE":
                    args = _split_top_level_commas(arg_text)
                    if len(args) != 1:
                        raise CodegenError(
                            f"{header}: TENG_REFLECT_ADD_ON_CREATE expects 1 arg"
                        )
                    add_on_create = _parse_bool_literal(args[0])
                elif name == "TENG_REFLECT_FIELD":
                    args = _split_top_level_commas(arg_text)
                    if len(args) != 4:
                        raise CodegenError(f"{header}: TENG_REFLECT_FIELD expects 4 args")
                    member = args[0].strip()
                    kind = args[1].strip()
                    default_expr = args[2].strip()
                    exposure = args[3].strip()
                    json_key = member
                    if json_key in seen_field_keys:
                        raise CodegenError(
                            f"{header}: duplicate field key {json_key} in {component_key}"
                        )
                    seen_field_keys.add(json_key)
                    fields.append(
                        Field(
                            member=member,
                            kind=kind,
                            default_expr=default_expr,
                            script_exposure=exposure,
                            json_key=json_key,
                        )
                    )
                elif name == "TENG_REFLECT_ASSET_FIELD":
                    args = _split_top_level_commas(arg_text)
                    if len(args) != 5:
                        raise CodegenError(
                            f"{header}: TENG_REFLECT_ASSET_FIELD expects 5 args"
                        )
                    member = args[0].strip()
                    json_key = _parse_string_literal(args[1])
                    asset_kind = _parse_string_literal(args[2])
                    default_expr = args[3].strip()
                    exposure = args[4].strip()
                    if json_key in seen_field_keys:
                        raise CodegenError(
                            f"{header}: duplicate field key {json_key} in {component_key}"
                        )
                    seen_field_keys.add(json_key)
                    fields.append(
                        Field(
                            member=member,
                            kind="AssetId",
                            default_expr=default_expr,
                            script_exposure=exposure,
                            json_key=json_key,
                            asset_kind=asset_kind,
                        )
                    )
                elif name == "TENG_REFLECT_ENUM_FIELD":
                    args = _split_top_level_commas(arg_text)
                    if len(args) < 6:
                        raise CodegenError(
                            f"{header}: TENG_REFLECT_ENUM_FIELD expects at least 6 args"
                        )
                    member = args[0].strip()
                    json_key = _parse_string_literal(args[1])
                    enum_key = _parse_string_literal(args[2])
                    default_expr = args[3].strip()
                    exposure = args[4].strip()
                    values_tokens = args[5:]

                    enum_values: list[EnumValue] = []
                    seen_enum_keys: set[str] = set()
                    seen_enum_values: set[int] = set()
                    for token in values_tokens:
                        token = token.strip()
                        if not token.startswith("TENG_ENUM_VALUE"):
                            raise CodegenError(
                                f"{header}: enum values must be TENG_ENUM_VALUE(...), got {token}"
                            )
                        # Parse inner args of TENG_ENUM_VALUE(...)
                        inner_start = token.find("(")
                        inner_end = token.rfind(")")
                        if inner_start == -1 or inner_end == -1 or inner_end <= inner_start:
                            raise CodegenError(
                                f"{header}: malformed TENG_ENUM_VALUE invocation: {token}"
                            )
                        inner = token[inner_start + 1 : inner_end]
                        inner_args = _split_top_level_commas(inner)
                        if len(inner_args) != 3:
                            raise CodegenError(
                                f"{header}: TENG_ENUM_VALUE expects 3 args, got {len(inner_args)}"
                            )
                        enumerator_expr = inner_args[0].strip()
                        key = _parse_string_literal(inner_args[1])
                        stable_value = _parse_int(inner_args[2])
                        if key in seen_enum_keys:
                            raise CodegenError(
                                f"{header}: duplicate enum key {key} in {enum_key}"
                            )
                        if stable_value in seen_enum_values:
                            raise CodegenError(
                                f"{header}: duplicate enum stable value {stable_value} in {enum_key}"
                            )
                        seen_enum_keys.add(key)
                        seen_enum_values.add(stable_value)
                        enum_values.append(
                            EnumValue(
                                enumerator_expr=enumerator_expr,
                                key=key,
                                stable_value=stable_value,
                            )
                        )

                    if not enum_values:
                        raise CodegenError(f"{header}: enum field {json_key} has no values")

                    # Validate default key is among values (best-effort parse for DefaultEnum(..., \"key\"))
                    default_match = re.search(r'DefaultEnum\s*\(\s*[^,]+,\s*(".*?")\s*\)', default_expr)
                    if not default_match:
                        raise CodegenError(
                            f"{header}: enum field {json_key} default must be DefaultEnum(..., \"key\")"
                        )
                    default_key = _parse_string_literal(default_match.group(1))
                    if default_key not in seen_enum_keys:
                        raise CodegenError(
                            f"{header}: enum field {json_key} default key {default_key} not declared"
                        )

                    if json_key in seen_field_keys:
                        raise CodegenError(
                            f"{header}: duplicate field key {json_key} in {component_key}"
                        )
                    seen_field_keys.add(json_key)
                    fields.append(
                        Field(
                            member=member,
                            kind="Enum",
                            default_expr=default_expr,
                            script_exposure=exposure,
                            json_key=json_key,
                            enum_key=enum_key,
                            enum_values=tuple(enum_values),
                        )
                    )

                idx += 1

            if idx >= len(invocations) or invocations[idx][0] != "TENG_REFLECT_COMPONENT_END":
                raise CodegenError(f"{header}: missing TENG_REFLECT_COMPONENT_END()")

            missing: list[str] = []
            if module_id is None or module_version is None:
                missing.append("TENG_REFLECT_MODULE")
            if schema_version is None:
                missing.append("TENG_REFLECT_SCHEMA_VERSION")
            if storage is None:
                missing.append("TENG_REFLECT_STORAGE")
            if visibility is None:
                missing.append("TENG_REFLECT_VISIBILITY")
            if add_on_create is None:
                missing.append("TENG_REFLECT_ADD_ON_CREATE")
            if missing:
                raise CodegenError(
                    f"{header}: component {component_key} missing required metadata: {', '.join(missing)}"
                )

            if component_key in seen_component_keys:
                raise CodegenError(f"{header}: duplicate component key {component_key}")
            seen_component_keys.add(component_key)

            components.append(
                Component(
                    cpp_type=cpp_type,
                    component_key=component_key,
                    module_id=module_id,
                    module_version=module_version,
                    schema_version=schema_version,
                    storage=storage,
                    visibility=visibility,
                    add_on_create=add_on_create,
                    fields=tuple(fields),
                )
            )

            idx += 1  # consume END

    return components


def _manifest_from_components(components: list[Component], *, module_name: str, headers: list[str]) -> dict[str, Any]:
    return {
        "tool": "teng_reflect_codegen.py",
        "module_name": module_name,
        "headers": headers,
        "components": [
            {
                "cpp_type": c.cpp_type,
                "component_key": c.component_key,
                "module": {"id": c.module_id, "version": c.module_version},
                "schema_version": c.schema_version,
                "storage": c.storage,
                "visibility": c.visibility,
                "add_on_create": c.add_on_create,
                "fields": [
                    {
                        "member": f.member,
                        "json_key": f.json_key,
                        "kind": f.kind,
                        "default": f.default_expr,
                        "script_exposure": f.script_exposure,
                        "asset_kind": f.asset_kind,
                        "enum_key": f.enum_key,
                        "enum_values": [
                            {
                                "enumerator": ev.enumerator_expr,
                                "key": ev.key,
                                "stable_value": ev.stable_value,
                            }
                            for ev in f.enum_values
                        ],
                    }
                    for f in c.fields
                ],
            }
            for c in components
        ],
    }


def _write_manifest(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _c_ident(s: str) -> str:
    # Only for debug strings; we keep this very conservative.
    return re.sub(r"[^A-Za-z0-9_]", "_", s)


def _cpp_string_literal(s: str) -> str:
    return json.dumps(s)


def _cpp_storage(storage: str) -> str:
    return f"scene::ComponentStoragePolicy::{storage}"


def _cpp_visibility(visibility: str) -> str:
    return f"scene::ComponentSchemaVisibility::{visibility}"


def _cpp_field_kind(kind: str) -> str:
    return f"scene::ComponentFieldKind::{kind}"


def _cpp_script_exposure(exposure: str) -> str:
    mapping = {
        "ScriptNone": "scene::ScriptExposure::None",
        "ScriptRead": "scene::ScriptExposure::Read",
        "ScriptReadWrite": "scene::ScriptExposure::ReadWrite",
    }
    if exposure not in mapping:
        raise CodegenError(f"unknown script exposure token: {exposure}")
    return mapping[exposure]


def _extract_call_arg(expr: str, name: str) -> str:
    prefix = f"{name}("
    if not expr.startswith(prefix) or not expr.endswith(")"):
        raise CodegenError(f"expected {name}(...) default expression, got: {expr}")
    return expr[len(prefix) : -1].strip()


def _cpp_default_value(field: Field) -> str:
    expr = field.default_expr.strip()
    if field.kind == "Bool":
        value = _extract_call_arg(expr, "DefaultBool") if expr.startswith("DefaultBool(") else expr
        return f"scene::ComponentFieldDefaultValue{{{value}}}"
    if field.kind == "F32":
        value = _extract_call_arg(expr, "DefaultF32") if expr.startswith("DefaultF32(") else expr
        return f"scene::ComponentFieldDefaultValue{{{value}}}"
    if field.kind == "I32":
        return f"scene::ComponentFieldDefaultValue{{{expr}}}"
    if field.kind == "U32":
        return f"scene::ComponentFieldDefaultValue{{{expr}}}"
    if field.kind == "AssetId":
        value = _extract_call_arg(expr, "DefaultAssetId")
        return (
            "scene::ComponentFieldDefaultValue{"
            f"scene::ComponentDefaultAssetId{{.value = {_cpp_string_literal(_parse_string_literal(value))}}}"
            "}"
        )
    if field.kind == "Enum":
        match = re.search(r'DefaultEnum\s*\(\s*[^,]+,\s*(".*?")\s*\)', expr)
        if not match:
            raise CodegenError(f"enum default must be DefaultEnum(..., \"key\"), got: {expr}")
        return (
            "scene::ComponentFieldDefaultValue{"
            f"scene::ComponentDefaultEnum{{.key = {_cpp_string_literal(_parse_string_literal(match.group(1)))}}}"
            "}"
        )
    raise CodegenError(f"fixture generator does not know how to emit default for kind {field.kind}")


def _cpp_field_record(field: Field) -> str:
    parts = [
        f".key = {_cpp_string_literal(field.json_key or field.member)}",
        f".member_name = {_cpp_string_literal(field.member)}",
        f".kind = {_cpp_field_kind(field.kind)}",
        ".authored_required = true",
        f".default_value = {_cpp_default_value(field)}",
    ]
    if field.asset_kind is not None:
        parts.append(
            ".asset = scene::ComponentAssetFieldMetadata{"
            f".expected_kind = {_cpp_string_literal(field.asset_kind)}"
            "}"
        )
    if field.enum_key is not None:
        enum_values = ", ".join(
            "scene::ComponentEnumValueRegistration{"
            f".key = {_cpp_string_literal(ev.key)}, .value = {ev.stable_value}"
            "}"
            for ev in field.enum_values
        )
        parts.append(
            ".enumeration = scene::ComponentEnumRegistration{"
            f".enum_key = {_cpp_string_literal(field.enum_key)}, .values = {{{enum_values}}}"
            "}"
        )
    parts.append(f".script_exposure = {_cpp_script_exposure(field.script_exposure)}")
    return "scene::ReflectedFieldRecord{" + ", ".join(parts) + "}"


def _enum_type(field: Field) -> str:
    if not field.enum_values:
        raise CodegenError(f"enum field {field.member} has no values")
    first = field.enum_values[0].enumerator_expr
    if "::" not in first:
        raise CodegenError(f"enum value must be scoped, got: {first}")
    enum_type = first.rsplit("::", 1)[0]
    for value in field.enum_values:
        if not value.enumerator_expr.startswith(f"{enum_type}::"):
            raise CodegenError(
                f"enum field {field.member} mixes enum types: {first}, {value.enumerator_expr}"
            )
    return enum_type


def _typed_field_json_expr(field: Field, component_var: str) -> str:
    member_expr = f"{component_var}.{field.member}"
    if field.kind in {"Bool", "F32", "I32", "U32", "String"}:
        return member_expr
    if field.kind == "AssetId":
        return f"{member_expr}.to_string()"
    if field.kind == "Enum":
        return f"std::string(to_key_{_c_ident(field.enum_key or field.member)}({member_expr}))"
    raise CodegenError(f"typed generator does not know how to serialize kind {field.kind}")


def _typed_field_load_expr(field: Field) -> str:
    key = field.json_key or field.member
    payload_get = f'payload[{_cpp_string_literal(key)}]'
    if field.kind == "Bool":
        return f"{payload_get}.get<bool>()"
    if field.kind == "F32":
        return f"{payload_get}.get<float>()"
    if field.kind == "I32":
        return f"{payload_get}.get<int32_t>()"
    if field.kind == "U32":
        return f"{payload_get}.get<uint32_t>()"
    if field.kind == "String":
        return f"{payload_get}.get<std::string>()"
    if field.kind == "AssetId":
        return f"AssetId::parse({payload_get}.get<std::string>()).value()"
    if field.kind == "Enum":
        return f"from_key_{_c_ident(field.enum_key or field.member)}({payload_get}.get<std::string>())"
    raise CodegenError(f"typed generator does not know how to deserialize kind {field.kind}")


def _write_enum_helpers(cpp_lines: list[str], components: list[Component]) -> None:
    seen_enum_helpers: set[str] = set()
    for component in components:
        for field in component.fields:
            if field.kind != "Enum":
                continue
            helper_ident = _c_ident(field.enum_key or field.member)
            if helper_ident in seen_enum_helpers:
                continue
            seen_enum_helpers.add(helper_ident)
            enum_type = _enum_type(field)
            cpp_lines.append(
                f"[[nodiscard]] std::string_view to_key_{helper_ident}({enum_type} value) {{"
            )
            cpp_lines.append("  switch (value) {")
            for enum_value in field.enum_values:
                cpp_lines.append(f"    case {enum_value.enumerator_expr}:")
                cpp_lines.append(f"      return {_cpp_string_literal(enum_value.key)};")
            cpp_lines.append("  }")
            cpp_lines.append('  ALWAYS_ASSERT(false, "unknown reflected enum value");')
            cpp_lines.append('  return "";')
            cpp_lines.append("}")
            cpp_lines.append("")
            cpp_lines.append(
                f"[[nodiscard]] {enum_type} from_key_{helper_ident}(std::string_view key) {{"
            )
            for enum_value in field.enum_values:
                cpp_lines.append(f"  if (key == {_cpp_string_literal(enum_value.key)}) {{")
                cpp_lines.append(f"    return {enum_value.enumerator_expr};")
                cpp_lines.append("  }")
            cpp_lines.append('  ALWAYS_ASSERT(false, "unknown reflected enum key {}", key);')
            cpp_lines.append(f"  return {field.enum_values[0].enumerator_expr};")
            cpp_lines.append("}")
            cpp_lines.append("")
            cpp_lines.append(
                f"[[maybe_unused]] [[nodiscard]] int64_t to_stable_value_{helper_ident}("
                f"{enum_type} value) {{"
            )
            cpp_lines.append("  switch (value) {")
            for enum_value in field.enum_values:
                cpp_lines.append(f"    case {enum_value.enumerator_expr}:")
                cpp_lines.append(f"      return {enum_value.stable_value};")
            cpp_lines.append("  }")
            cpp_lines.append('  ALWAYS_ASSERT(false, "unknown reflected enum value");')
            cpp_lines.append("  return 0;")
            cpp_lines.append("}")
            cpp_lines.append("")


def _write_typed_thunks(cpp_lines: list[str], components: list[Component]) -> None:
    _write_enum_helpers(cpp_lines, components)
    for component in components:
        ident = _c_ident(component.component_key)
        cpp_lines.append(f"void register_flecs_{ident}(flecs::world& world) {{")
        cpp_lines.append(f"  world.component<{component.cpp_type}>();")
        cpp_lines.append("}")
        cpp_lines.append("")
        if component.add_on_create:
            cpp_lines.append(f"void apply_on_create_{ident}(flecs::entity entity) {{")
            cpp_lines.append(f"  entity.set<{component.cpp_type}>({component.cpp_type}{{}});")
            cpp_lines.append("}")
            cpp_lines.append("")
        cpp_lines.append(f"bool has_component_{ident}(flecs::entity entity) {{")
        cpp_lines.append(f"  return entity.has<{component.cpp_type}>();")
        cpp_lines.append("}")
        cpp_lines.append("")
        if component.storage == "Authored":
            cpp_lines.append(f"nlohmann::json serialize_component_{ident}(flecs::entity entity) {{")
            cpp_lines.append(f"  const auto& component = entity.get<{component.cpp_type}>();")
            cpp_lines.append("  return nlohmann::json{")
            for field in component.fields:
                key = field.json_key or field.member
                cpp_lines.append(
                    f"      {{{_cpp_string_literal(key)}, {_typed_field_json_expr(field, 'component')}}},"
                )
            cpp_lines.append("  };")
            cpp_lines.append("}")
            cpp_lines.append("")
            cpp_lines.append(
                f"void deserialize_component_{ident}(flecs::entity entity, "
                "const nlohmann::json& payload) {"
            )
            cpp_lines.append(f"  entity.set<{component.cpp_type}>({component.cpp_type}{{")
            for field in component.fields:
                cpp_lines.append(f"      .{field.member} = {_typed_field_load_expr(field)},")
            cpp_lines.append("  });")
            cpp_lines.append("}")
            cpp_lines.append("")


def _write_fixture_thunks(cpp_lines: list[str]) -> None:
    cpp_lines.append("void register_fixture_flecs_component(flecs::world&) {}")
    cpp_lines.append("[[maybe_unused]] void apply_fixture_on_create(flecs::entity) {}")
    cpp_lines.append("bool has_fixture_component(flecs::entity entity) { return entity.is_valid(); }")
    cpp_lines.append("nlohmann::json serialize_fixture_component(flecs::entity) {")
    cpp_lines.append("  return nlohmann::json::object();")
    cpp_lines.append("}")
    cpp_lines.append("void deserialize_fixture_component(flecs::entity, const nlohmann::json&) {}")
    cpp_lines.append("")


def _write_generated_cpp(
    out_dir: Path, *, components: list[Component], module_name: str, options: OutputOptions
) -> None:
    hpp = out_dir / f"{options.basename}.hpp"
    cpp = out_dir / f"{options.basename}.cpp"

    banner = f"{options.basename} (module={module_name})"
    comp_count = len(components)
    field_count = sum(len(c.fields) for c in components)

    # Flatten a small string dump for sanity checks / debugging.
    dump_lines: list[str] = []
    for c in components:
        dump_lines.append(f"component {c.component_key} type={c.cpp_type} storage={c.storage} visibility={c.visibility}")
        for f in c.fields:
            line = f"  field {f.json_key} member={f.member} kind={f.kind} script={f.script_exposure}"
            if f.kind == "AssetId":
                line += f" asset_kind={f.asset_kind}"
            if f.kind == "Enum":
                line += f" enum_key={f.enum_key} values={len(f.enum_values)}"
            dump_lines.append(line)

    hpp_text = f"""#pragma once

#include <cstddef>
#include <string_view>

#include "engine/scene/ComponentRuntimeReflection.hpp"

namespace {options.namespace} {{

inline constexpr std::string_view k_banner = {json.dumps(banner)};
inline constexpr std::size_t k_component_count = {comp_count};
inline constexpr std::size_t k_field_count = {field_count};

// Debug-only dump of parsed reflection; stable format is not guaranteed.
extern const char* const k_dump_lines[];
extern const std::size_t k_dump_line_count;

void register_{options.function_prefix}_reflected_components(engine::scene::ComponentRegistryBuilder& builder);
void register_{options.function_prefix}_reflected_flecs(
    const engine::scene::ComponentRegistry& registry,
    engine::FlecsComponentContextBuilder& builder);
void register_{options.function_prefix}_reflected_serialization(engine::SceneSerializationContextBuilder& builder);

}}  // namespace {options.namespace}
"""

    cpp_lines = []
    cpp_lines.append(f'#include "{options.basename}.hpp"')
    cpp_lines.append("")
    cpp_lines.append("#include <array>")
    cpp_lines.append("#include <cstdint>")
    cpp_lines.append("#include <string>")
    cpp_lines.append("#include <string_view>")
    cpp_lines.append("")
    cpp_lines.append("#include <nlohmann/json.hpp>")
    if options.typed_thunks:
        cpp_lines.append("")
        cpp_lines.append('#include "core/EAssert.hpp"')
    for include in options.header_includes:
        cpp_lines.append(f'#include "{include}"')
    cpp_lines.append("")
    cpp_lines.append(f"namespace {options.namespace} {{")
    cpp_lines.append("")
    cpp_lines.append("namespace {")
    cpp_lines.append("")
    cpp_lines.append("namespace scene = teng::engine::scene;")
    cpp_lines.append("")
    for c in components:
        array_name = f"k_{_c_ident(c.component_key)}_fields"
        cpp_lines.append(
            f"const std::array<scene::ReflectedFieldRecord, {len(c.fields)}> {array_name} = {{{{"
        )
        for f in c.fields:
            cpp_lines.append(f"  {_cpp_field_record(f)},")
        cpp_lines.append("}};")
        cpp_lines.append("")
    cpp_lines.append(
        f"const std::array<scene::ReflectedComponentRecord, {len(components)}> k_components = {{{{"
    )
    for c in components:
        array_name = f"k_{_c_ident(c.component_key)}_fields"
        cpp_lines.append("  scene::ReflectedComponentRecord{")
        cpp_lines.append(f"      .component_key = {_cpp_string_literal(c.component_key)},")
        cpp_lines.append(f"      .module_id = {_cpp_string_literal(c.module_id)},")
        cpp_lines.append(f"      .module_version = {c.module_version},")
        cpp_lines.append(f"      .schema_version = {c.schema_version},")
        cpp_lines.append(f"      .storage = {_cpp_storage(c.storage)},")
        cpp_lines.append(f"      .visibility = {_cpp_visibility(c.visibility)},")
        cpp_lines.append(f"      .add_on_create = {'true' if c.add_on_create else 'false'},")
        cpp_lines.append(f"      .fields = std::span<const scene::ReflectedFieldRecord>{{{array_name}}},")
        cpp_lines.append("  },")
    cpp_lines.append("}};")
    cpp_lines.append("")
    if options.typed_thunks:
        _write_typed_thunks(cpp_lines, components)
    else:
        _write_fixture_thunks(cpp_lines)

    flecs_components = [c for c in components if c.storage != "EditorOnly"]
    cpp_lines.append(
        f"const std::array<teng::engine::ReflectedFlecsThunks, {len(flecs_components)}> "
        "k_flecs_thunks = {{"
    )
    for c in flecs_components:
        apply_fn = (
            f"apply_on_create_{_c_ident(c.component_key)}"
            if options.typed_thunks and c.add_on_create
            else ("apply_fixture_on_create" if c.add_on_create else "nullptr")
        )
        register_fn = (
            f"register_flecs_{_c_ident(c.component_key)}"
            if options.typed_thunks
            else "register_fixture_flecs_component"
        )
        cpp_lines.append("  teng::engine::ReflectedFlecsThunks{")
        cpp_lines.append(f"      .component_key = {_cpp_string_literal(c.component_key)},")
        cpp_lines.append(f"      .register_flecs_fn = {register_fn},")
        cpp_lines.append(f"      .apply_on_create_fn = {apply_fn},")
        cpp_lines.append("  },")
    cpp_lines.append("}};")
    cpp_lines.append("")
    serialized_components = [c for c in components if c.storage == "Authored"]
    cpp_lines.append(
        f"const std::array<teng::engine::ReflectedSerializationThunks, {len(serialized_components)}> "
        "k_serialization_thunks = {{"
    )
    for c in serialized_components:
        ident = _c_ident(c.component_key)
        has_fn = f"has_component_{ident}" if options.typed_thunks else "has_fixture_component"
        serialize_fn = (
            f"serialize_component_{ident}" if options.typed_thunks else "serialize_fixture_component"
        )
        deserialize_fn = (
            f"deserialize_component_{ident}"
            if options.typed_thunks
            else "deserialize_fixture_component"
        )
        cpp_lines.append("  teng::engine::ReflectedSerializationThunks{")
        cpp_lines.append(f"      .component_key = {_cpp_string_literal(c.component_key)},")
        cpp_lines.append(f"      .has_component_fn = {has_fn},")
        cpp_lines.append(f"      .serialize_fn = {serialize_fn},")
        cpp_lines.append(f"      .deserialize_fn = {deserialize_fn},")
        cpp_lines.append("  },")
    cpp_lines.append("}};")
    cpp_lines.append("")
    cpp_lines.append("}  // namespace")
    cpp_lines.append("")
    cpp_lines.append(f"const std::size_t k_dump_line_count = {len(dump_lines)};")
    cpp_lines.append("const char* const k_dump_lines[] = {")
    for line in dump_lines:
        cpp_lines.append(f"  {json.dumps(line)},")
    cpp_lines.append("};")
    cpp_lines.append("")
    cpp_lines.append(
        f"void register_{options.function_prefix}_reflected_components("
        "engine::scene::ComponentRegistryBuilder& builder) {"
    )
    cpp_lines.append("  engine::register_reflected_components(builder, k_components);")
    cpp_lines.append("}")
    cpp_lines.append("")
    cpp_lines.append(f"void register_{options.function_prefix}_reflected_flecs(")
    cpp_lines.append("    const engine::scene::ComponentRegistry& registry,")
    cpp_lines.append("    engine::FlecsComponentContextBuilder& builder) {")
    cpp_lines.append("  engine::register_reflected_flecs(registry, builder, k_flecs_thunks);")
    cpp_lines.append("}")
    cpp_lines.append("")
    cpp_lines.append(
        f"void register_{options.function_prefix}_reflected_serialization("
        "engine::SceneSerializationContextBuilder& builder) {"
    )
    cpp_lines.append("  engine::register_reflected_serialization(builder, k_serialization_thunks);")
    cpp_lines.append("}")
    cpp_lines.append("")
    cpp_lines.append(f"}}  // namespace {options.namespace}")
    cpp_lines.append("")

    out_dir.mkdir(parents=True, exist_ok=True)
    hpp.write_text(hpp_text, encoding="utf-8")
    cpp.write_text("\n".join(cpp_lines), encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Header reflection/codegen fixture generator.")
    parser.add_argument("--out-dir", required=True, help="Output directory.")
    parser.add_argument("--module-name", required=True, help="Generated module name label.")
    parser.add_argument(
        "--output-basename",
        default="fixtures_reflect.generated",
        help="Generated header/source basename without .hpp/.cpp.",
    )
    parser.add_argument(
        "--namespace",
        default="teng::reflect_fixture_generated",
        help="C++ namespace for generated registration functions.",
    )
    parser.add_argument(
        "--function-prefix",
        default="fixture",
        help="Prefix in register_<prefix>_reflected_* functions.",
    )
    parser.add_argument(
        "--include",
        dest="header_includes",
        action="append",
        default=[],
        help="Header include to add to the generated .cpp. May be passed multiple times.",
    )
    parser.add_argument(
        "--typed-thunks",
        action="store_true",
        help="Generate typed Flecs and JSON thunks instead of fixture dummy thunks.",
    )
    parser.add_argument(
        "--headers",
        nargs="+",
        required=True,
        help="Ordered list of headers to scan.",
    )
    parser.add_argument(
        "--manifest",
        default=None,
        help="Optional override manifest path (defaults to <out-dir>/fixtures_reflect.manifest.json).",
    )
    args = parser.parse_args(argv)

    out_dir = Path(args.out_dir)
    headers = [Path(h) for h in args.headers]
    module_name = str(args.module_name)
    options = OutputOptions(
        basename=str(args.output_basename),
        namespace=str(args.namespace),
        function_prefix=str(args.function_prefix),
        header_includes=tuple(str(include) for include in args.header_includes),
        typed_thunks=bool(args.typed_thunks),
    )

    try:
        components = parse_headers(headers)
        manifest = _manifest_from_components(
            components, module_name=module_name, headers=[os.fspath(h) for h in headers]
        )

        manifest_path = (
            Path(args.manifest)
            if args.manifest is not None
            else (
                out_dir
                / (
                    "fixtures_reflect.manifest.json"
                    if options.basename == "fixtures_reflect.generated"
                    else f"{options.basename}.manifest.json"
                )
            )
        )
        _write_manifest(manifest_path, manifest)
        _write_generated_cpp(out_dir, components=components, module_name=module_name, options=options)
        return 0
    except CodegenError as exc:
        print(f"teng_reflect_codegen.py: error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
