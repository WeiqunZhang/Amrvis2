#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 6 ]]; then
    echo "usage: $0 OUTPUT_ARCHIVE CAPTURE_DIRECTORY PLOT2D_A PLOT2D_B PLOT3D MULTIFAB_PREFIX" >&2
    exit 2
fi

output_archive=$1
capture_directory=$2
plot2d_a=$3
plot2d_b=$4
plot3d=$5
multifab_prefix=$6

if [[ -e "$output_archive" ]]; then
    echo "output archive already exists: $output_archive" >&2
    exit 1
fi
for required in \
    "$capture_directory/legacy-plt00001-phi-boxes-range-0-1.png" \
    "$capture_directory/legacy-standalone-fab.png" \
    "$capture_directory/legacy-standalone-multifab-nonzero-origin.png" \
    "$capture_directory/legacy-3d-orthogonal-q-range-0-1.png" \
    "$capture_directory/plt00001.zslice.0.Level_1.fab" \
    "$capture_directory/Palette" \
    "$capture_directory/amrvis.defaults" \
    "$plot2d_a/Header" \
    "$plot2d_b/Header" \
    "$plot3d/Header" \
    "${multifab_prefix}_H"; do
    if [[ ! -f "$required" ]]; then
        echo "required corpus input is missing: $required" >&2
        exit 1
    fi
done

script_directory=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repository_root=$(cd "$script_directory/../.." && pwd)
staging_parent=$(mktemp -d -p /tmp amrvis2-corpus.XXXXXX)
trap 'rm -rf -- "$staging_parent"' EXIT

artifact_name=amrvis2-compatibility-corpus-0.1.0-incomplete
artifact_root="$staging_parent/$artifact_name"
mkdir -p \
    "$artifact_root/datasets/plotfile-sequence" \
    "$artifact_root/datasets/plotfile-3d" \
    "$artifact_root/datasets/standalone-multifab" \
    "$artifact_root/legacy-outputs" \
    "$artifact_root/metadata"

cp -a "$plot2d_a" "$artifact_root/datasets/plotfile-sequence/"
cp -a "$plot2d_b" "$artifact_root/datasets/plotfile-sequence/"
cp -a "$plot3d" "$artifact_root/datasets/plotfile-3d/"
cp "${multifab_prefix}_H" "$artifact_root/datasets/standalone-multifab/inmf_H"
multifab_directory=$(dirname "$multifab_prefix")
multifab_base=$(basename "$multifab_prefix")
find "$multifab_directory" -maxdepth 1 -type f -name "${multifab_base}_D_*" \
    -exec cp {} "$artifact_root/datasets/standalone-multifab/" \;

cp "$capture_directory/legacy-plt00001-phi-boxes-range-0-1.png" \
    "$capture_directory/legacy-standalone-fab.png" \
    "$capture_directory/legacy-standalone-multifab-nonzero-origin.png" \
    "$capture_directory/legacy-3d-orthogonal-q-range-0-1.png" \
    "$capture_directory/plt00001.zslice.0.Level_1.fab" \
    "$capture_directory/Palette" \
    "$capture_directory/amrvis.defaults" \
    "$artifact_root/legacy-outputs/"

cp "$repository_root/reference/README.md" \
    "$repository_root/reference/manifest.toml" \
    "$repository_root/reference/compatibility-evidence.md" \
    "$artifact_root/metadata/"
cp -a "$repository_root/reference/environment" "$artifact_root/metadata/"
cp -a "$repository_root/reference/workflows" "$artifact_root/metadata/"

(
    cd "$artifact_root"
    find . -type f ! -name corpus-manifest.sha256 -print0 \
        | sort -z \
        | xargs -0 sha256sum > corpus-manifest.sha256
)

mkdir -p "$(dirname "$output_archive")"
tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -C "$staging_parent" -cJf "$output_archive" "$artifact_name"
sha256sum "$output_archive"
