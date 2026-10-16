#!/usr/bin/env bash
# Build (and optionally run) the FluidTouch desktop simulator on macOS / Linux.
#
#   ./sim/build.sh            # configure + build
#   ./sim/build.sh --run      # build, then launch
#   ./sim/build.sh --clean    # wipe sim/build first
#   ./sim/build.sh --release  # optimised build
#
# Arguments after -- are passed to the simulator, e.g.
#   ./sim/build.sh --run -- --data ~/ft-test
#
# macOS:  brew install sdl2 cmake ninja   (plus Xcode Command Line Tools)
# Linux:  apt install clang cmake ninja-build libsdl2-dev

set -euo pipefail

SIM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SIM_DIR/build"
RUN=0 CLEAN=0 BUILD_TYPE=Debug
SIM_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --run)     RUN=1 ;;
        --clean)   CLEAN=1 ;;
        --release) BUILD_TYPE=Release ;;
        --)        shift; SIM_ARGS=("$@"); break ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1 (try --help)" >&2; exit 1 ;;
    esac
    shift
done

for tool in cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: '$tool' not found." >&2
        if [[ "$(uname)" == Darwin ]]; then
            echo "Install the dependencies with: brew install sdl2 cmake ninja" >&2
        else
            echo "Install the dependencies with: sudo apt install clang cmake ninja-build libsdl2-dev" >&2
        fi
        exit 1
    fi
done

if [[ $CLEAN -eq 1 ]]; then
    rm -rf "$BUILD_DIR"
fi

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    CMAKE_ARGS=(-S "$SIM_DIR" -B "$BUILD_DIR" -G Ninja "-DCMAKE_BUILD_TYPE=$BUILD_TYPE")
    # Prefer clang (Apple Clang on macOS); fall back to whatever CMake finds
    if command -v clang++ >/dev/null 2>&1; then
        CMAKE_ARGS+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
    fi
    cmake "${CMAKE_ARGS[@]}"
fi

cmake --build "$BUILD_DIR"

if [[ $RUN -eq 1 ]]; then
    exec "$BUILD_DIR/fluidtouch_sim" ${SIM_ARGS[@]+"${SIM_ARGS[@]}"}
fi
