#!/usr/bin/env bash
# Run the experimental checkout alongside an installed Sung without shared state.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
state="$root/.dev-state/qqmusic"
export XDG_CONFIG_HOME="$state/config"
export XDG_DATA_HOME="$state/data"
export XDG_CACHE_HOME="$state/cache"
mkdir -p -- "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
# Share the desktop bus for Caelestia MPRIS and StatusNotifierItem discovery.
# Separate activation and MPRIS names keep the installed player independent.
exec "$root/scripts/run.sh" --instance qqmusic "$@"
