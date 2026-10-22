#!/usr/bin/env bash
set -e

mkdir -p ./spv

for file in ./shaders/*.comp; do
    [ -f "$file" ] || continue

    name=$(basename "$file" .comp)
    glslang -V -S comp -o "./spv/$name.spv" "$file"
done
