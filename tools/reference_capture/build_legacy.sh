#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "usage: $0 LEGACY_AMRVIS_ROOT AMREX_ROOT BUILD_DIRECTORY [DIMENSION]" >&2
    exit 2
fi

legacy_root=$1
amrex_root=$2
build_root=$3
dimension=${4:-2}

if [[ "$dimension" != 2 && "$dimension" != 3 ]]; then
    echo "dimension must be 2 or 3" >&2
    exit 2
fi

if [[ -e "$build_root" ]]; then
    echo "build directory already exists: $build_root" >&2
    exit 1
fi

mkdir -p "$build_root/source"
git -C "$legacy_root" archive --format=tar HEAD -o "$build_root/amrvis-source.tar"
tar -xf "$build_root/amrvis-source.tar" -C "$build_root/source"

make -C "$build_root/source" -j4 \
    AMREX_HOME="$(readlink -f "$amrex_root")" \
    DIM="$dimension" \
    USE_MPI=FALSE \
    USE_VOLRENDER=FALSE \
    COMP=gnu \
    DEBUG=FALSE

sha256sum "$build_root/amrvis-source.tar" > "$build_root/amrvis-source.tar.sha256"
sha256sum "$build_root/source/amrvis${dimension}d.gnu.ex" \
    > "$build_root/amrvis${dimension}d.gnu.ex.sha256"
