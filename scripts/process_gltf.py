#!/usr/bin/env python3

import sys
import shutil
import subprocess
from pathlib import Path
import json
from concurrent.futures import ThreadPoolExecutor, as_completed
import os


def convert_image(src_img: Path) -> bool:
    """Convert a single image to KTX2. Returns True if conversion succeeded, False otherwise."""
    if src_img.suffix == ".ktx2":
        print(f"skipping {src_img.name} (already ktx2)")
        return True

    base = src_img.stem
    dst_img = src_img.parent / f"{base}.ktx2"
    tmp_img = dst_img.with_suffix(".ktx2.tmp")

    if not src_img.is_file():
        print(f"[warn] missing image: {src_img}")
        return True

    dst_img.parent.mkdir(parents=True, exist_ok=True)

    try:
        subprocess.run(
            [
                "toktx",
                "--t2",
                "--encode",
                "astc",
                "--astc_blk_d",
                "4x4",
                "--genmipmap",
                str(tmp_img),
                str(src_img),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        tmp_img.rename(dst_img)
        print(f"converted {src_img.name} -> {dst_img.name}")
        src_img.unlink()
        return True
    except subprocess.CalledProcessError:
        print(f"[error] toktx failed for {src_img}")
        if tmp_img.exists():
            tmp_img.unlink()
        return False


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <gltf_directory>")
        sys.exit(1)

    src_dir = Path(sys.argv[1]).resolve()
    if not src_dir.is_dir():
        print(f"{src_dir} is not a valid directory")
        sys.exit(1)

    dst_dir = src_dir.parent / f"{src_dir.name}_ktx2"
    shutil.copytree(src_dir, dst_dir, dirs_exist_ok=True)

    gltf_files = list(dst_dir.glob("*.gltf"))
    if not gltf_files:
        print("no .gltf file found")
        sys.exit(1)
    gltf_file = gltf_files[0]
    print(f"processing {gltf_file}")

    with open(gltf_file, "r") as f:
        gltf = json.load(f)

    uris = [img.get("uri") for img in gltf.get("images", []) if "uri" in img]

    max_workers = min(32, os.cpu_count() or 4)

    conversion_failed = False
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(convert_image, dst_dir / uri): uri for uri in uris}
        for future in as_completed(futures):
            if not future.result():
                conversion_failed = True

    if conversion_failed:
        print("[error] some conversions failed, not updating GLTF")
        sys.exit(1)

    for img in gltf.get("images", []):
        if "uri" in img:
            img["uri"] = str(Path(img["uri"]).with_suffix(".ktx2"))
            img["mimeType"] = "image/ktx2"

    with open(gltf_file, "w") as f:
        json.dump(gltf, f, indent=2)

    print(f"done. output in {dst_dir}")


if __name__ == "__main__":
    main()
