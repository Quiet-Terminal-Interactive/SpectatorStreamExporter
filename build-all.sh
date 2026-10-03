#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -d godot-cpp ]]; then
    echo "godot-cpp/ missing. Run:"
    echo "  git clone --recursive https://github.com/godotengine/godot-cpp.git"
    exit 1
fi

JOBS="$(nproc)"

echo "=== Linux (native) ==="
scons -j"$JOBS" platform=linux target=template_debug   "$@"
scons -j"$JOBS" platform=linux target=template_release "$@"

echo
echo "=== Windows (mingw-w64 cross) ==="
if ! command -v x86_64-w64-mingw32-gcc >/dev/null; then
    echo "SKIP: x86_64-w64-mingw32-gcc not on PATH. Install mingw-w64."
elif [[ -z "${SPOUT_SDK:-}" ]]; then
    echo "SKIP: SPOUT_SDK not set. Spout backend would fail to link."
else
    scons -j"$JOBS" platform=windows use_mingw=yes target=template_debug   arch=x86_64 "$@"
    scons -j"$JOBS" platform=windows use_mingw=yes target=template_release arch=x86_64 "$@"
fi

echo
echo "=== macOS (osxcross cross) ==="
if [[ -z "${SYPHON_SDK:-}" && -d third_party/Syphon.framework ]]; then
    export SYPHON_SDK="$(pwd)/third_party"
fi
if [[ -z "${OSXCROSS_ROOT:-}" || ! -d "${OSXCROSS_ROOT}/target/bin" ]]; then
    echo "SKIP: OSXCROSS_ROOT unset or ${OSXCROSS_ROOT:-<unset>}/target/bin missing."
elif [[ -z "${SYPHON_SDK:-}" ]]; then
    echo "SKIP: SYPHON_SDK unset and third_party/Syphon.framework not found."
else
    scons -j"$JOBS" platform=macos osxcross_sdk=darwin25.4 target=template_debug   arch=universal "$@"
    scons -j"$JOBS" platform=macos osxcross_sdk=darwin25.4 target=template_release arch=universal "$@"
fi

echo
echo "Built:"
ls -1 demo/addons/spectator_stream_exporter/bin/ | sed 's/^/  /'
