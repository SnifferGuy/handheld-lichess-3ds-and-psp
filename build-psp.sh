#!/usr/bin/env bash
set -euo pipefail
project="$(cd -- "$(dirname -- "$0")" && pwd)"
command -v psp-config >/dev/null || {
    echo "PSPDEV is missing. Follow https://pspdev.github.io/installation/windows.html" >&2
    exit 1
}
stage="$(mktemp -d)"
trap 'rm -f -- "$stage"/*; rmdir -- "$stage"' EXIT
cp -- "$project"/*.cpp "$project"/*.hpp "$project/Makefile.psp" "$stage/"
cp -- "$project/assets/icon-psp.png" "$stage/icon-psp.png"
mkdir -p "$project/dist"
make -C "$stage" -f Makefile.psp EBOOT.PBP 2>&1 | tee "$stage/build.log"
if grep -qi 'could not fixup imports' "$stage/build.log"; then
    echo "PSP firmware import fixup failed" >&2
    exit 1
fi
cp -- "$stage/EBOOT.PBP" "$project/dist/EBOOT.PBP"
{
    psp-g++ --version
    if [[ -r "${PSPDEV:-}/build.txt" ]]; then cat -- "$PSPDEV/build.txt"; fi
} > "$project/dist/psp-toolchain.txt"
printf 'Built %s\n' "$project/dist/EBOOT.PBP"
