#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export SUNG_QQMUSIC_ROOT="$root"
if [[ ! -x "$root/runtime/bin/python" ]]; then
    printf 'Missing Python runtime. Run %s/scripts/setup.sh first.\n' "$root" >&2
    exit 1
fi
exec "$root/runtime/bin/python" "$root/helper/qqmusic_login.py" "$@"
