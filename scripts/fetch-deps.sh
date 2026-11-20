#!/bin/sh
set -eu
root=${1:-deps}
mkdir -p "$root"
fetch() {
    name=$1 repo=$2 tag=$3
    if [ -f "$root/$name/.project-tag" ] && [ "$(cat "$root/$name/.project-tag")" = "$tag" ]; then
        return
    fi
    if [ -e "$root/$name" ]; then
        echo "Move the existing $root/$name directory before fetching $tag." >&2
        exit 1
    fi
    archive=$(mktemp)
    staging=$(mktemp -d "$root/.download-XXXXXX")
    trap 'rm -f "$archive"; rm -rf "$staging"' EXIT HUP INT TERM
    curl -fLsS --retry 2 "https://codeload.github.com/$repo/tar.gz/refs/tags/$tag" -o "$archive"
    tar -xzf "$archive" --strip-components=1 -C "$staging"
    printf '%s\n' "$tag" > "$staging/.project-tag"
    mv "$staging" "$root/$name"
    rm -f "$archive"
    trap - EXIT HUP INT TERM
}
fetch rtos FreeRTOS/FreeRTOS-Kernel V11.1.0
fetch dsp ARM-software/CMSIS-DSP v1.16.2
fetch cmsis ARM-software/CMSIS_5 5.9.0
fetch f7 STMicroelectronics/cmsis-device-f7 v1.2.10
