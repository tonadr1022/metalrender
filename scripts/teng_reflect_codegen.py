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


def _write_generated_cpp(out_dir: Path, *, components: list[Component], module_name: str) -> None:
    hpp = out_dir / "fixtures_reflect.generated.hpp"
    cpp = out_dir / "fixtures_reflect.generated.cpp"

    banner = f"fixtures_reflect (module={module_name})"
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

namespace teng::reflect_fixture_generated {{

inline constexpr std::string_view k_banner = {json.dumps(banner)};
inline constexpr std::size_t k_component_count = {comp_count};
inline constexpr std::size_t k_field_count = {field_count};

// Debug-only dump of parsed reflection; stable format is not guaranteed.
extern const char* const k_dump_lines[];
extern const std::size_t k_dump_line_count;

}}  // namespace teng::reflect_fixture_generated
"""

    cpp_lines = []
    cpp_lines.append('#include "fixtures_reflect.generated.hpp"')
    cpp_lines.append("")
    cpp_lines.append("namespace teng::reflect_fixture_generated {")
    cpp_lines.append("")
    cpp_lines.append(f"const std::size_t k_dump_line_count = {len(dump_lines)};")
    cpp_lines.append("const char* const k_dump_lines[] = {")
    for line in dump_lines:
        cpp_lines.append(f"  {json.dumps(line)},")
    cpp_lines.append("};")
    cpp_lines.append("")
    cpp_lines.append("}  // namespace teng::reflect_fixture_generated")
    cpp_lines.append("")

    out_dir.mkdir(parents=True, exist_ok=True)
    hpp.write_text(hpp_text, encoding="utf-8")
    cpp.write_text("\n".join(cpp_lines), encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Header reflection/codegen fixture generator.")
    parser.add_argument("--out-dir", required=True, help="Output directory.")
    parser.add_argument("--module-name", required=True, help="Generated module name label.")
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

    try:
        components = parse_headers(headers)
        manifest = _manifest_from_components(
            components, module_name=module_name, headers=[os.fspath(h) for h in headers]
        )

        manifest_path = (
            Path(args.manifest)
            if args.manifest is not None
            else (out_dir / "fixtures_reflect.manifest.json")
        )
        _write_manifest(manifest_path, manifest)
        _write_generated_cpp(out_dir, components=components, module_name=module_name)
        return 0
    except CodegenError as exc:
        print(f"teng_reflect_codegen.py: error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

