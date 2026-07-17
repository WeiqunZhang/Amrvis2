#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 LEGACY_AMRVIS_ROOT AMREX_ROOT" >&2
    exit 2
fi

legacy_root=$1
amrex_root=$2

printf 'capture_time_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf 'host=%s\n' "$(hostname)"
printf 'kernel=%s\n' "$(uname -srmo)"
if [[ -r /etc/os-release ]]; then
    . /etc/os-release
    printf 'os=%s\n' "${PRETTY_NAME:-unknown}"
fi
printf 'legacy_root=%s\n' "$(readlink -f "$legacy_root")"
printf 'legacy_commit=%s\n' "$(git -C "$legacy_root" rev-parse HEAD)"
printf 'legacy_tracked_clean=%s\n' "$(git -C "$legacy_root" diff --quiet && echo true || echo false)"
printf 'amrex_root=%s\n' "$(readlink -f "$amrex_root")"
printf 'amrex_commit=%s\n' "$(git -C "$amrex_root" rev-parse HEAD)"
printf 'amrex_tracked_clean=%s\n' "$(git -C "$amrex_root" diff --quiet && echo true || echo false)"
printf 'compiler=%s\n' "$(g++ --version | head -n 1)"
printf 'cmake=%s\n' "$(cmake --version | head -n 1)"
printf 'qt=%s\n' "$(qmake6 -query QT_VERSION 2>/dev/null || echo unavailable)"
printf 'display=%s\n' "${DISPLAY:-unset}"
printf 'legacy_build_options=DIM=2 USE_MPI=FALSE USE_VOLRENDER=FALSE COMP=gnu DEBUG=FALSE\n'

for package in libmotif-dev libx11-dev libxt-dev libxext-dev libxpm-dev; do
    version=$(dpkg-query -W -f='${Version}' "$package" 2>/dev/null || echo missing)
    printf 'package.%s=%s\n' "$package" "$version"
done

