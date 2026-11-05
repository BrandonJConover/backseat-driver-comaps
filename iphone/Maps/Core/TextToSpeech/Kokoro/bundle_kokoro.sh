#!/bin/bash
# Build phase: fills Core/TextToSpeech/Kokoro/CoreMLKokoro with everything FluidAudio's KokoroAne
# loads, so Backseat Driver ships its Kokoro voice inside the app and never downloads a model.
#
# Files come from one pinned FluidAudio revision (coreml_kokoro_manifest.tsv, each checked by
# sha256), fetched at build time and skipped once present. When kokoro_weights.tsv (repo root)
# names a fine-tune, its Kokoro*.mlmodelc files come from the local build-deps folder instead
# (model files never go on GitHub, so GitHub Actions can't build that; build on the Mac).
# At launch KokoroBundle copies the folder into FluidAudio's cache (KokoroBundle.swift).
set -euo pipefail

HERE="${SRCROOT}/Core/TextToSpeech/Kokoro"
MANIFEST="$HERE/coreml_kokoro_manifest.tsv"
WEIGHTS="${SRCROOT}/../../kokoro_weights.tsv"
DEST="$HERE/CoreMLKokoro"
REVISION=$(sed -n 's/^# FluidInference\/kokoro-82m-coreml @ \([0-9a-f]*\).*/\1/p' "$MANIFEST")
[ -n "$REVISION" ] || { echo "error: no pinned revision in $MANIFEST"; exit 1; }

WEIGHTS_ID=$(awk -F'\t' '$1 == "id" { print $2; exit }' "$WEIGHTS" 2>/dev/null || true)
WEIGHTS_ID=${WEIGHTS_ID:-stock}
DEPS="${BOOKLYRE_BUILD_DEPS_DIR:-$HOME/Library/Caches/BookLyre/build-deps}/$WEIGHTS_ID"
pinned_sha() { awk -F'\t' -v p="$1" '$1 == p { print $3; exit }' "$WEIGHTS"; }
sha_of() { shasum -a 256 "$1" | awk '{print $1}'; }

fetched=0
while IFS=$'\t' read -r remote local bytes sha; do
    case "$remote" in ''|'#'*) continue ;; esac
    target="$DEST/$local"
    source=""
    if [ "$WEIGHTS_ID" != "stock" ]; then
        tuned=$(pinned_sha "CoreMLKokoro/$local")
        if [ -n "$tuned" ]; then sha="$tuned"; source="$DEPS/CoreMLKokoro/$local"; fi
    fi
    if [ -f "$target" ] && [ "$(sha_of "$target")" = "$sha" ]; then continue; fi
    mkdir -p "$(dirname "$target")"
    tmp="$target.download"
    if [ -n "$source" ]; then
        [ -f "$source" ] || { echo "error: Kokoro weights $WEIGHTS_ID not found: $source (copy project files kokoro-finetune/ship/$WEIGHTS_ID/ there)"; exit 1; }
        cp "$source" "$tmp"
    else
        curl -fL --retry 3 --silent --show-error \
            "https://huggingface.co/FluidInference/kokoro-82m-coreml/resolve/${REVISION}/${remote}" -o "$tmp"
        case "$remote" in
            voices/*.json)  # Kokoro v1.0 JSON pack ("1".."510" rows of 256) -> flat little-endian fp32
                /usr/bin/python3 -c 'import json, struct, sys
d = json.load(open(sys.argv[1]))
open(sys.argv[2], "wb").write(b"".join(struct.pack("<256f", *d[str(r)]) for r in range(1, 511)))' "$tmp" "$tmp.bin"
                mv "$tmp.bin" "$tmp" ;;
        esac
    fi
    actual=$(sha_of "$tmp")
    if [ "$actual" != "$sha" ]; then
        rm -f "$tmp"; echo "error: checksum mismatch for $local (expected $sha, got $actual)"; exit 1
    fi
    mv "$tmp" "$target"
    fetched=$((fetched + 1))
done < "$MANIFEST"

# KokoroBundle replaces the installed models when this changes (an update with new weights).
printf '%s\n' "$WEIGHTS_ID" > "$DEST/WEIGHTS_ID"
echo "Kokoro Core ML files ready in CoreMLKokoro ($fetched fetched, weights $WEIGHTS_ID)."
