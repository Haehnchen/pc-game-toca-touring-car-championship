#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ghidra="${GHIDRA_HOME:-${XDG_CACHE_HOME:-$HOME/.cache}/toca-tools/ghidra_12.1.4_PUBLIC}"
destination="$project_dir/references/ghidra"
exe="$project_dir/references/toca-1.1a/tourcars.exe"
expected=8ce56d41
action="${1:-open}"
case "$action" in
    import|open|decompile) ;;
    *) echo 'Usage: tools/ghidra.sh [import|open|decompile]' >&2; exit 1 ;;
esac
if [[ ! -x "$ghidra/support/analyzeHeadless" ]]; then
    echo 'Ghidra is missing. Set GHIDRA_HOME; see docs/porting.md.' >&2; exit 1
fi
if [[ ! -f "$exe" ]] || [[ "$(python3 "$project_dir/tools/checksum.py" "$exe")" != "$expected" ]]; then
    echo 'The unchanged TOCA 1.1a executable is missing or has a different checksum.' >&2; exit 1
fi
mkdir -p "$destination"
if [[ ! -f "$destination/TOCA-1.1a.gpr" ]]; then
    "$ghidra/support/analyzeHeadless" "$destination" TOCA-1.1a \
        -import "$exe" -processor x86:LE:32:default -cspec windows \
        -log "$destination/import.log"
fi
case "$action" in
    open) exec "$ghidra/ghidraRun" "$destination/TOCA-1.1a.gpr" ;;
    decompile)
        mkdir -p "$project_dir/references/decompiled"
        exec "$ghidra/support/analyzeHeadless" "$destination" TOCA-1.1a \
            -process tourcars.exe -noanalysis -readOnly \
            -scriptPath "$project_dir/tools" \
            -postScript ExportReference.java "$project_dir/references/decompiled/tourcars.c" \
            -log "$destination/decompile.log"
        ;;
esac
