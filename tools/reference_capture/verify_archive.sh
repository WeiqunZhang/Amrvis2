#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 CORPUS_ARCHIVE" >&2
    exit 2
fi

archive=$1
if [[ ! -f "$archive" ]]; then
    echo "corpus archive does not exist: $archive" >&2
    exit 1
fi

restore_root=$(mktemp -d -p /tmp amrvis2-corpus-restore.XXXXXX)
trap 'rm -rf -- "$restore_root"' EXIT
tar -xJf "$archive" -C "$restore_root"

manifest=$(find "$restore_root" -mindepth 2 -maxdepth 2 \
    -type f -name corpus-manifest.sha256 -print -quit)
if [[ -z "$manifest" ]]; then
    echo "archive has no top-level corpus manifest" >&2
    exit 1
fi

(
    cd "$(dirname "$manifest")"
    sha256sum --check corpus-manifest.sha256
)
