set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -d godot-cpp ]]; then
    echo "godot-cpp/ missing. Run:"
    echo "  git clone --recursive https://github.com/godotengine/godot-cpp.git"
    exit 1
fi

if [[ -z "${SYPHON_SDK:-}" && -d third_party/Syphon.framework ]]; then
    export SYPHON_SDK="$(pwd)/third_party"
fi

if [[ -z "${SYPHON_SDK:-}" ]]; then
    echo "WARNING: SYPHON_SDK unset and third_party/Syphon.framework not found."
    echo "         Syphon backend will compile as a stub. See README.md#Installing-Syphon."
fi

JOBS="$(sysctl -n hw.ncpu)"

echo "=== macOS (native) ==="
scons -j"$JOBS" platform=macos target=template_debug   arch=universal "$@"
scons -j"$JOBS" platform=macos target=template_release arch=universal "$@"

echo
echo "=== Windows (mingw cross) ==="
if ! command -v x86_64-w64-mingw32-gcc >/dev/null; then
    echo "SKIP: x86_64-w64-mingw32-gcc not on PATH. brew install mingw-w64."
elif [[ -z "${SPOUT_SDK:-}" ]]; then
    echo "SKIP: SPOUT_SDK not set. Spout backend would fail to link."
else
    scons -j"$JOBS" platform=windows use_mingw=yes target=template_debug   arch=x86_64 "$@"
    scons -j"$JOBS" platform=windows use_mingw=yes target=template_release arch=x86_64 "$@"
fi

echo
echo "=== Linux (Docker cross) ==="
if ! command -v docker >/dev/null; then
    echo "SKIP: docker not on PATH."
else
    docker run --rm -v "$(pwd)":/src -w /src archlinux:latest bash -c '
        pacman -Syu --noconfirm scons python libpipewire vulkan-headers vulkan-icd-loader base-devel git &&
        JOBS="$(nproc)" &&
        scons -j"$JOBS" platform=linux target=template_debug &&
        scons -j"$JOBS" platform=linux target=template_release
    '
fi

echo
echo "Built:"
ls -1 demo/addons/spectator_stream_exporter/bin/ | sed 's/^/  /'
