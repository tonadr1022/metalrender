#!/usr/bin/env bash
# Thanks GPT
set -euo pipefail

if [ $# -ne 1 ]; then
	echo "usage: $0 <gltf_directory>"
	exit 1
fi

SRC_DIR="$(cd "$1" && pwd)"
BASENAME="$(basename "$SRC_DIR")"
DST_DIR="$(dirname "$SRC_DIR")/${BASENAME}_ktx2"

mkdir -p "$DST_DIR"
cp -R "$SRC_DIR"/. "$DST_DIR"/

GLTF_FILE="$(ls "$DST_DIR"/*.gltf 2>/dev/null | head -n 1)"
if [ -z "$GLTF_FILE" ]; then
	echo "no .gltf file found"
	exit 1
fi

echo "processing $GLTF_FILE"

# Create temp file for URIs
URIS_FILE=$(mktemp)
trap "rm -f $URIS_FILE" EXIT

jq -r '.images[]?.uri // empty' "$GLTF_FILE" >"$URIS_FILE"

CONVERSION_FAILED=0

while IFS= read -r URI; do
	if [[ "$URI" == *.ktx2 ]]; then
		echo "skipping $URI (already ktx2)"
		continue
	fi

	SRC_IMG="$DST_DIR/$URI"
	BASE="${URI%.*}"
	DST_IMG="$DST_DIR/$BASE.ktx2"
	TMP_IMG="$DST_IMG.tmp"

	if [ ! -f "$SRC_IMG" ]; then
		echo "[warn] missing image: $SRC_IMG"
		continue
	fi

	echo "converting $URI -> $BASE.ktx2"
	mkdir -p "$(dirname "$DST_IMG")"

	# Capture both stdout and stderr, but check exit code
	if toktx --t2 --encode astc --astc_blk_d 4x4 --genmipmap "$TMP_IMG" "$SRC_IMG" >/dev/null 2>&1; then
		mv "$TMP_IMG" "$DST_IMG"
		rm "$SRC_IMG"
	else
		echo "[error] toktx failed for $SRC_IMG"
		rm -f "$TMP_IMG"
		CONVERSION_FAILED=1
	fi
done <"$URIS_FILE"

if [ $CONVERSION_FAILED -eq 1 ]; then
	echo "[error] some conversions failed, not updating GLTF"
	exit 1
fi

# Update GLTF file
jq '
.images |= map(
  .uri |= sub("\\.[^.]+$"; ".ktx2") |
  .mimeType = "image/ktx2"
)
' "$GLTF_FILE" >"$GLTF_FILE.tmp"

mv "$GLTF_FILE.tmp" "$GLTF_FILE"

echo "done. output in $DST_DIR"
