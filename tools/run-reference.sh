#!/usr/bin/env bash
# Run the unchanged TOCA 1.1a reference.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/run-reference.sh [--sound | --mute | --audio-null-sink] [--help]

Launch the unchanged TOCA 1.1a reference in a 640 × 480 Wine desktop.
  --sound  Enable Wine audio output for manual checking.
  --mute   Disable Wine audio output (default).
  --audio-null-sink  Use only winepulse with explicit PULSE_SERVER=unix:/path/to/socket.
  --help   Show this help without launching the game.
EOF
}

audio_status=muted
for argument in "$@"; do
    case "$argument" in
        --sound) audio_status='sound enabled' ;;
        --mute) audio_status=muted ;;
        --audio-null-sink) audio_status='private null sink' ;;
        --help) usage; exit 0 ;;
        *) echo "Unknown argument: $argument" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "$audio_status" == 'private null sink' ]]; then
    if [[ "${PULSE_SERVER:-}" != unix:/* || ! -S "${PULSE_SERVER#unix:}" ]]; then
        echo '--audio-null-sink requires an explicit absolute Unix PULSE_SERVER socket' >&2
        exit 2
    fi
fi

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
reference="$project_dir/references/toca-1.1a"
cache="${XDG_CACHE_HOME:-$HOME/.cache}"
runner="${TOCA_WINE_RUNNER:-$HOME/.local/share/lutris/runners/wine/ge-proton/files}"
graphics="$cache/toca-tools/dgVoodoo-2.63"
export WINEPREFIX="$cache/toca-1.1a/wine"
export WINEARCH=win32
export WINEDEBUG="${WINEDEBUG:--all}"
export DXVK_LOG_LEVEL=error
# Keep native graphics wrappers and disable Wine's desktop-menu integration.
export WINEDLLOVERRIDES='ddraw,d3dimm,d3d11,dxgi=n;winemenubuilder.exe=d'
if [[ "$audio_status" == muted ]]; then
    WINEDLLOVERRIDES+=';winepulse.drv,winealsa.drv,wineoss.drv=d'
elif [[ "$audio_status" == 'private null sink' ]]; then
    WINEDLLOVERRIDES+=';winepulse.drv=b;winealsa.drv,wineoss.drv=d'
fi

expected=8ce56d41
if [[ ! -f "$reference/tourcars.exe" ]] ||
   [[ "$(python3 "$project_dir/tools/checksum.py" "$reference/tourcars.exe")" != "$expected" ]]; then
    echo 'The unchanged TOCA 1.1a executable is missing or has a different checksum.' >&2
    exit 1
fi
for dependency in "$runner/bin/wine" "$runner/bin/wineserver" \
    "$runner/lib/wine/dxvk/i386-windows/d3d11.dll" \
    "$runner/lib/wine/dxvk/i386-windows/dxgi.dll" \
    "$runner/lib/vkd3d/i386-windows/libvkd3d-1.dll" \
    "$runner/lib/vkd3d/i386-windows/libvkd3d-shader-1.dll" \
    "$graphics/DDraw.dll" "$graphics/D3DImm.dll"; do
    if [[ ! -f "$dependency" ]]; then
        echo "Missing dependency: $dependency (see docs/porting.md)." >&2
        exit 1
    fi
done

mkdir -p "$WINEPREFIX"
exec 9>"$cache/toca-1.1a/run.lock"
flock -n 9 || { echo 'The reference is already running.' >&2; exit 1; }
trap '"$runner/bin/wineserver" -k >/dev/null 2>&1 || true' EXIT
if [[ ! -f "$WINEPREFIX/system.reg" ]]; then
    "$runner/bin/wine" wineboot -u >/dev/null 2>&1
fi
install -m 644 "$runner/lib/vkd3d/i386-windows/"{libvkd3d-1,libvkd3d-shader-1}.dll \
    "$WINEPREFIX/drive_c/windows/system32/"
# Windows 98 mode is required for the game's VGA port access.
"$runner/bin/wine" reg add 'HKCU\Software\Wine' /v Version /d win98 /f >/dev/null
"$runner/bin/wine" reg add 'HKLM\Software\Wine\Drives' /v d: /d cdrom /f >/dev/null
ln -sfn "$reference" "$WINEPREFIX/dosdevices/d:"
"$runner/bin/wineserver" -k
"$runner/bin/wineserver" -w

runtime="$WINEPREFIX/drive_c/TOCA"
mkdir -p "$runtime"
for file in tourcars.exe mss32.dll; do
    install -m 644 "$reference/$file" "$runtime/$file"
done
for directory in data fe speech; do
    ln -sfn "$reference/$directory" "$runtime/$directory"
done
install -m 644 "$graphics/DDraw.dll" "$graphics/D3DImm.dll" "$runtime/"
install -m 644 "$runner/lib/wine/dxvk/i386-windows/"{d3d11,dxgi}.dll "$runtime/"
install -m 644 "$project_dir/tools/dgVoodoo.conf" "$runtime/"
cd "$runtime"
echo "TOCA 1.1a — 640 × 480, $audio_status"
"$runner/bin/wine" explorer /desktop=TOCA,640x480 \
    'C:\TOCA\tourcars.exe' 'C:\TOCA' 'D:\' 0
"$runner/bin/wineserver" -w
