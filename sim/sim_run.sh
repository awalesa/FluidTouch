#!/usr/bin/env bash
# Start the fake FluidNC server and the simulator together; the server stops
# when the simulator closes. Works from any directory.
#   ./sim/sim_run.sh                 fake on port 81, then the simulator
#   ./sim/sim_run.sh --build         build the simulator first (./sim/build.sh)
#   PORT=8181 ./sim/sim_run.sh       another port (the machine in the
#                                    simulator must use the same one)
# Extra arguments after -- go to fake_fluidnc.py, e.g. -- --steps-per-mm 80
set -euo pipefail

SIM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${PORT:-81}"

FAKE_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build) "$SIM_DIR/build.sh"; shift ;;
        --) shift; FAKE_ARGS=("$@"); break ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ ! -x "$SIM_DIR/build/fluidtouch_sim" ]]; then
    echo "Simulator not built yet, building..."
    "$SIM_DIR/build.sh"
fi

FAKE_PID=""
if lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    # Something (probably a fake started earlier) already listens: use it
    echo "Port $PORT is already in use, not starting another fake FluidNC"
else
    python3 "$SIM_DIR/tools/fake_fluidnc.py" --port "$PORT" ${FAKE_ARGS[@]+"${FAKE_ARGS[@]}"} &
    FAKE_PID=$!
    # Stop the fake when the simulator exits, however that happens
    trap '[[ -n "$FAKE_PID" ]] && kill "$FAKE_PID" 2>/dev/null || true' EXIT
    sleep 1
    if ! kill -0 "$FAKE_PID" 2>/dev/null; then
        echo "Fake FluidNC did not start (see the error above)" >&2
        exit 1
    fi
fi

"$SIM_DIR/build/fluidtouch_sim"
