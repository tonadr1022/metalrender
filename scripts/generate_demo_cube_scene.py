#!/usr/bin/env python3

from __future__ import annotations

from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCENE_PATH = REPO_ROOT / "resources" / "scenes" / "demo_cube.tscene.toml"
CUBE_ASSET_ID = "24d0fbf7-833e-cb90-cd2e-e7963f89d58e"


def main() -> int:
    SCENE_PATH.parent.mkdir(parents=True, exist_ok=True)
    SCENE_PATH.write_text(
        f"""schema_version = 1
name = "demo cube"

[[entities]]
guid = 1001
name = "camera"

[entities.transform]
translation = [0.0, 0.0, 3.0]
rotation = [1.0, 0.0, 0.0, 0.0]
scale = [1.0, 1.0, 1.0]

[entities.local_to_world]
matrix = [
  1.0, 0.0, 0.0, 0.0,
  0.0, 1.0, 0.0, 0.0,
  0.0, 0.0, 1.0, 0.0,
  0.0, 0.0, 3.0, 1.0,
]

[entities.camera]
fov_y = 1.04719755
z_near = 0.1
z_far = 10000.0
primary = true

[[entities]]
guid = 1002
name = "directional light"

[entities.transform]
translation = [0.0, 0.0, 0.0]
rotation = [1.0, 0.0, 0.0, 0.0]
scale = [1.0, 1.0, 1.0]

[entities.local_to_world]
matrix = [
  1.0, 0.0, 0.0, 0.0,
  0.0, 1.0, 0.0, 0.0,
  0.0, 0.0, 1.0, 0.0,
  0.0, 0.0, 0.0, 1.0,
]

[entities.directional_light]
direction = [0.35, 1.0, 0.4]
color = [1.0, 1.0, 1.0]
intensity = 1.0

[[entities]]
guid = 1003
name = "cube"

[entities.transform]
translation = [0.0, 0.0, 0.0]
rotation = [1.0, 0.0, 0.0, 0.0]
scale = [1.0, 1.0, 1.0]

[entities.local_to_world]
matrix = [
  1.0, 0.0, 0.0, 0.0,
  0.0, 1.0, 0.0, 0.0,
  0.0, 0.0, 1.0, 0.0,
  0.0, 0.0, 0.0, 1.0,
]

[entities.mesh_renderable]
model = "{CUBE_ASSET_ID}"
"""
    )
    print(f"wrote {SCENE_PATH.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
