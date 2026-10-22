#!/usr/bin/env bash
set -e

for file in ./shaders/*.comp; do
    [ -f "$file" ] || continue

    name=$(basename "$file" .comp)
    glslang -V -S comp -o "./shaders/$name.spv" "$file"
done
