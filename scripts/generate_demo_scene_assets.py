#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import math
import random
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RESOURCE_DIR = REPO_ROOT / "resources"
DEMO_SEED = 10_000_000


@dataclass(frozen=True)
class Camera:
    pos: tuple[float, float, float]
    pitch: float = 0.0
    yaw: float = 270.0
    move_speed: float = 1.0


@dataclass(frozen=True)
class ModelBatch:
    source_path: str
    transforms: tuple[tuple[tuple[float, ...], tuple[float, ...], tuple[float, ...]], ...]


@dataclass(frozen=True)
class ScenePreset:
    name: str
    camera: Camera
    models: tuple[ModelBatch, ...]


def fnv1a64(data: bytes) -> str:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{value:016x}"


def asset_id_for_source(source_path: str) -> str:
    digest = hashlib.sha256(f"metalrender-demo-model:{source_path}".encode("utf-8")).hexdigest()
    return f"{digest[0:8]}-{digest[8:12]}-{digest[12:16]}-{digest[16:20]}-{digest[20:32]}"


def slug(text: str) -> str:
    out = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    return out or "scene"


def translation_matrix(x: float, y: float, z: float, scale: float = 1.0) -> tuple[float, ...]:
    return (
        scale,
        0.0,
        0.0,
        0.0,
        0.0,
        scale,
        0.0,
        0.0,
        0.0,
        0.0,
        scale,
        0.0,
        x,
        y,
        z,
        1.0,
    )


def quat_from_axis_angle(axis: tuple[float, float, float], angle: float) -> tuple[float, float, float, float]:
    length = math.sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2])
    if length == 0.0:
        return (1.0, 0.0, 0.0, 0.0)
    s = math.sin(angle * 0.5) / length
    return (math.cos(angle * 0.5), axis[0] * s, axis[1] * s, axis[2] * s)


def matrix_from_trs(
    translation: tuple[float, float, float],
    rotation: tuple[float, float, float, float],
    scale: tuple[float, float, float],
) -> tuple[float, ...]:
    w, x, y, z = rotation
    xx = x * x
    yy = y * y
    zz = z * z
    xy = x * y
    xz = x * z
    yz = y * z
    wx = w * x
    wy = w * y
    wz = w * z

    # Column-major, matching glm::mat4 serialization in SceneAssetLoader.
    return (
        (1.0 - 2.0 * (yy + zz)) * scale[0],
        (2.0 * (xy + wz)) * scale[0],
        (2.0 * (xz - wy)) * scale[0],
        0.0,
        (2.0 * (xy - wz)) * scale[1],
        (1.0 - 2.0 * (xx + zz)) * scale[1],
        (2.0 * (yz + wx)) * scale[1],
        0.0,
        (2.0 * (xz + wy)) * scale[2],
        (2.0 * (yz - wx)) * scale[2],
        (1.0 - 2.0 * (xx + yy)) * scale[2],
        0.0,
        translation[0],
        translation[1],
        translation[2],
        1.0,
    )


def transform_from_matrix(matrix: tuple[float, ...]) -> tuple[tuple[float, ...], tuple[float, ...], tuple[float, ...]]:
    sx = math.sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2])
    sy = math.sqrt(matrix[4] * matrix[4] + matrix[5] * matrix[5] + matrix[6] * matrix[6])
    sz = math.sqrt(matrix[8] * matrix[8] + matrix[9] * matrix[9] + matrix[10] * matrix[10])
    return ((matrix[12], matrix[13], matrix[14]), (1.0, 0.0, 0.0, 0.0), (sx, sy, sz))


def model_batch(source_path: str, matrices: Iterable[tuple[float, ...]]) -> ModelBatch:
    return ModelBatch(source_path=source_path, transforms=tuple(transform_from_matrix(m) for m in matrices))


def cube_grid(size: int, spacing: float, scale: float) -> tuple[float, ...]:
    return tuple(
        translation_matrix(float(x) * spacing, 0.0, float(z) * spacing, scale)
        for z in range(-size, size + 1)
        for x in range(-size, size + 1)
    )


def random_cube_transforms(count: int) -> tuple[tuple[float, ...], ...]:
    rng = random.Random(DEMO_SEED)
    transforms = []
    for _ in range(count):
        radius = 150.0
        pos = (rng.uniform(-radius, radius), rng.uniform(-radius, radius), rng.uniform(-radius, radius))
        axis = (rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0))
        quat = quat_from_axis_angle(axis, rng.uniform(0.0, math.tau))
        transforms.append(matrix_from_trs(pos, quat, (5.0, 5.0, 5.0)))
    return tuple(transforms)


def presets() -> tuple[ScenePreset, ...]:
    cube = "models/Cube/glTF/Cube.gltf"
    sponza = "models/gltf/Models/Sponza/glTF_ktx2/Sponza.gltf"
    chess = "models/gltf/Models/ABeautifulGame/glTF_ktx2/ABeautifulGame.gltf"
    suzanne = "models/gltf/Models/Suzanne/glTF_ktx2/Suzanne.gltf"
    return (
        ScenePreset("demo cube", Camera((0.0, 0.0, 3.0)), (model_batch(cube, (translation_matrix(0.0, 0.0, 0.0),)),)),
        ScenePreset("demo cube grid", Camera((10.0, 8.0, 18.0), pitch=-25.0, yaw=225.0, move_speed=4.0), (model_batch(cube, cube_grid(4, 2.0, 1.0)),)),
        ScenePreset("demo random cubes", Camera((0.0, 0.0, 40.0), move_speed=10.0), (model_batch(cube, random_cube_transforms(128)),)),
        ScenePreset("demo suzanne", Camera((0.0, 0.0, 3.0)), (model_batch(suzanne, (translation_matrix(0.0, 0.0, 0.0),)),)),
        ScenePreset("demo sponza", Camera((-6.0, 2.5, 0.0), yaw=0.0, move_speed=2.0), (model_batch(sponza, (translation_matrix(0.0, 0.0, 0.0),)),)),
        ScenePreset("demo chessboard", Camera((0.4, 0.4, 0.4), pitch=-30.0, yaw=-130.0, move_speed=0.25), (model_batch(chess, (translation_matrix(0.0, 0.0, 0.0),)),)),
    )


def write_sidecar(resource_dir: Path, source_path: str) -> bool:
    rel = Path(source_path)
    if rel.is_absolute() or ".." in rel.parts:
        print(f"warning: skipping unsafe model path (must be relative, no '..'): {source_path}")
        return False
    source = resource_dir / rel
    if not source.is_file():
        print(f"warning: skipping missing model for {source_path}: {source}")
        return False
    if not source.resolve().is_relative_to(resource_dir):
        print(f"warning: skipping symlinked model outside resource dir: {source_path}")
        return False

    sidecar = source.with_name(source.name + ".tasset.toml")
    sidecar.write_text(
        "\n".join(
            [
                "schema_version = 1",
                f'id = "{asset_id_for_source(source_path)}"',
                'type = "model"',
                f'source_path = "{source_path}"',
                f'display_name = "{source.stem}"',
                'importer = "gltf"',
                "importer_version = 1",
                f'source_content_hash = "{fnv1a64(source.read_bytes())}"',
                'imported_artifact_hash = ""',
                'status = "available"',
                "dependencies = []",
                'labels = [ "demo" ]',
                "",
            ]
        )
    )
    return True


def fmt_float(value: float) -> str:
    if value == 0.0:
        value = 0.0
    text = f"{value:.8g}"
    if "." not in text and "e" not in text:
        text += ".0"
    return text


def array(values: Iterable[float]) -> str:
    return "[" + ", ".join(fmt_float(v) for v in values) + "]"


def write_entity(out: list[str], guid: int, name: str, transform, local_to_world: tuple[float, ...]) -> None:
    translation, rotation, scale = transform
    out.extend(
        [
            "[[entities]]",
            f"guid = {guid}",
            f'name = "{name}"',
            "",
            "[entities.transform]",
            f"translation = {array(translation)}",
            f"rotation = {array(rotation)}",
            f"scale = {array(scale)}",
            "",
            "[entities.local_to_world]",
            "matrix = [",
        ]
    )
    for i in range(0, 16, 4):
        out.append("  " + ", ".join(fmt_float(v) for v in local_to_world[i : i + 4]) + ",")
    out.extend(["]", ""])


def camera_matrix(camera: Camera) -> tuple[float, ...]:
    return translation_matrix(camera.pos[0], camera.pos[1], camera.pos[2])


def write_scene(resource_dir: Path, scene_dir: Path, index: int, preset: ScenePreset) -> Path | None:
    available_batches = []
    for batch in preset.models:
        if not write_sidecar(resource_dir, batch.source_path):
            print(f"warning: skipping missing model for {preset.name}: {batch.source_path}")
            continue
        available_batches.append(batch)

    if not available_batches:
        print(f"warning: skipping scene with no available models: {preset.name}")
        return None

    scene_dir.mkdir(parents=True, exist_ok=True)
    path = scene_dir / f"demo_{index:02d}_{slug(preset.name.removeprefix('demo '))}.tscene.toml"
    lines = ["schema_version = 1", f'name = "{preset.name}"', ""]

    camera_guid = 10_000 + index * 100_000 + 1
    light_guid = camera_guid + 1
    cam_matrix = camera_matrix(preset.camera)
    write_entity(lines, camera_guid, "camera", transform_from_matrix(cam_matrix), cam_matrix)
    lines.extend(
        [
            "[entities.camera]",
            "fov_y = 1.04719755",
            "z_near = 0.1",
            "z_far = 10000.0",
            "primary = true",
            "",
        ]
    )

    light_matrix = translation_matrix(0.0, 0.0, 0.0)
    write_entity(lines, light_guid, "directional light", transform_from_matrix(light_matrix), light_matrix)
    lines.extend(
        [
            "[entities.directional_light]",
            "direction = [0.35, 1.0, 0.4]",
            "color = [1.0, 1.0, 1.0]",
            "intensity = 1.0",
            "",
        ]
    )

    mesh_index = 0
    for batch in available_batches:
        asset_id = asset_id_for_source(batch.source_path)
        for transform in batch.transforms:
            translation, rotation, scale = transform
            local_to_world = matrix_from_trs(translation, rotation, scale)
            write_entity(lines, camera_guid + 1000 + mesh_index, f"mesh {mesh_index}", transform, local_to_world)
            lines.extend(["[entities.mesh_renderable]", f'model = "{asset_id}"', ""])
            mesh_index += 1

    path.write_text("\n".join(lines))
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--resource-dir", type=Path, default=DEFAULT_RESOURCE_DIR)
    parser.add_argument("--scene-dir", type=Path)
    args = parser.parse_args()

    resource_dir = args.resource_dir.resolve()
    scene_dir = args.scene_dir.resolve() if args.scene_dir else resource_dir / "scenes"
    scene_dir.mkdir(parents=True, exist_ok=True)
    for old_scene in scene_dir.glob("demo_*.tscene.toml"):
        old_scene.unlink()

    written = []
    for i, preset in enumerate(presets()):
        path = write_scene(resource_dir, scene_dir, i, preset)
        if path:
            written.append(path)
            if i == 0:
                (scene_dir / "demo_cube.tscene.toml").write_text(path.read_text())
            print(f"wrote {path}")
    print(f"wrote {len(written)} scene asset(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
