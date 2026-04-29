#!/usr/bin/env python3

from __future__ import annotations

import hashlib
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
RESOURCE_DIR = REPO_ROOT / "resources"

DEMO_MODEL_PATHS = [
    "Models/Sponza/glTF_ktx2/Sponza.gltf",
    "Models/ABeautifulGame/glTF_ktx2/ABeautifulGame.gltf",
    "Models/Suzanne/glTF_ktx2/Suzanne.gltf",
    "Models/Cube/glTF_ktx2/Cube.gltf",
]

EXTRA_EXISTING_MODEL_PATHS = [
    "models/Cube/glTF/Cube.gltf",
]


def fnv1a64(data: bytes) -> str:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{value:016x}"


def asset_id_for_source(source_path: str) -> str:
    digest = hashlib.sha256(f"metalrender-demo-model:{source_path}".encode("utf-8")).hexdigest()
    return f"{digest[0:8]}-{digest[8:12]}-{digest[12:16]}-{digest[16:20]}-{digest[20:32]}"


def resolved_demo_source_path(path: str) -> str:
    if path.startswith("Models"):
        return str(Path("models") / "gltf" / path)
    return path


def write_sidecar(source_path: str) -> bool:
    source = RESOURCE_DIR / source_path
    if not source.is_file():
        return False

    sidecar = source.with_name(source.name + ".tasset.toml")
    display_name = source.stem
    content_hash = fnv1a64(source.read_bytes())
    sidecar.write_text(
        "\n".join(
            [
                "schema_version = 1",
                f'id = "{asset_id_for_source(source_path)}"',
                'type = "model"',
                f'source_path = "{source_path}"',
                f'display_name = "{display_name}"',
                'importer = "gltf"',
                "importer_version = 1",
                f'source_content_hash = "{content_hash}"',
                'imported_artifact_hash = ""',
                'status = "available"',
                "dependencies = []",
                'labels = [ "demo" ]',
                "",
            ]
        )
    )
    return True


def main() -> int:
    source_paths = [resolved_demo_source_path(path) for path in DEMO_MODEL_PATHS]
    source_paths.extend(EXTRA_EXISTING_MODEL_PATHS)
    written = [path for path in sorted(set(source_paths)) if write_sidecar(path)]
    for path in written:
        print(f"registered {path}")
    skipped = sorted(set(source_paths) - set(written))
    for path in skipped:
        print(f"skipped missing {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
