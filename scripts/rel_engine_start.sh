#!/usr/bin/env bash
# Start the persistent RAICode Rel engine server.
# Foreground by default (logs in terminal). Use --background for automated tasks.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOCKET="${REL2SQL_REL_ENGINE_SOCKET:-$ROOT/.rel_engine.sock}"
PIDFILE="${REL2SQL_REL_ENGINE_PID:-$ROOT/.rel_engine.pid}"
LOGFILE="${REL2SQL_REL_ENGINE_LOG:-$ROOT/.rel_engine.log}"

# Typical RAICode warm-up (first start compiles/precompiles packages).
WARMUP_HINT_FIRST="3-8 minutes"
WARMUP_HINT_CACHED="30-60 seconds"
READY_TIMEOUT_SEC=900

usage() {
  echo "Usage: $(basename "$0") [--background|-b]" >&2
  echo "  default: run in foreground (logs in terminal; Ctrl+C to stop)" >&2
  echo "  --background: detach after warm-up (for corpus:build and similar)" >&2
}

BACKGROUND=false
while [[ $# -gt 0 ]]; do
  case "$1" in
    --background|-b) BACKGROUND=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -f "$PIDFILE" ]]; then
  pid="$(cat "$PIDFILE")"
  if kill -0 "$pid" 2>/dev/null; then
    echo "rel engine server already running (pid $pid, socket $SOCKET)"
    exit 0
  fi
  rm -f "$PIDFILE"
fi

if [[ ! -f "$ROOT/third_party/raicode/Project.toml" ]]; then
  echo "error: third_party/raicode not initialized" >&2
  exit 1
fi

export JULIA_PROJECT="${JULIA_PROJECT:-$ROOT/third_party/raicode}"
export REL2SQL_REL_ENGINE_SOCKET="$SOCKET"
export REL2SQL_REL_ENGINE_PID="$PIDFILE"
if [[ -z "${REL2SQL_JULIA:-}" ]]; then
  if command -v juliaup >/dev/null 2>&1; then
    REL2SQL_JULIA="$(juliaup which 1.10 2>/dev/null || true)"
  fi
  if [[ -z "${REL2SQL_JULIA:-}" ]]; then
    for candidate in "$HOME"/.julia/juliaup/julia-1.10*/Julia-1.10.app/Contents/Resources/julia/bin/julia; do
      if [[ -x "$candidate" ]]; then
        REL2SQL_JULIA="$candidate"
        break
      fi
    done
  fi
fi
JULIA="${REL2SQL_JULIA:-julia}"

ping_server() {
  [[ -S "$SOCKET" ]] || return 1
  python3 - "$SOCKET" <<'PY'
import socket, sys
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(2.0)
sock.connect(sys.argv[1])
sock.sendall(b'{"op":"ping"}\n')
data = sock.recv(4096)
sys.exit(0 if b'"ok"' in data else 1)
PY
}

wait_for_ready_with_timer() {
  local start=$SECONDS
  while (( SECONDS - start < READY_TIMEOUT_SEC )); do
    if ping_server; then
      printf '\r  waiting for socket... ready in %ds                    \n' "$((SECONDS - start))"
      return 0
    fi
    printf '\r  waiting for socket... %ds elapsed (expect %s first start, %s cached)' \
      "$((SECONDS - start))" "$WARMUP_HINT_FIRST" "$WARMUP_HINT_CACHED"
    sleep 1
  done
  printf '\n'
  return 1
}

rm -f "$SOCKET" "$PIDFILE"

if $BACKGROUND; then
  echo "rel engine server: starting in background"
  echo "  socket: $SOCKET"
  echo "  log:    $LOGFILE"
  echo "  warm-up: typically $WARMUP_HINT_FIRST on first start, $WARMUP_HINT_CACHED when cached"
  nohup "$JULIA" "$ROOT/tests/generator/run_rel_engine_server.jl" "$SOCKET" >>"$LOGFILE" 2>&1 &
  if wait_for_ready_with_timer; then
    pid="unknown"
    [[ -f "$PIDFILE" ]] && pid="$(cat "$PIDFILE")"
    echo "rel engine server: ready (pid $pid, socket $SOCKET)"
    echo "  tail log: tail -f $LOGFILE"
    exit 0
  fi
  echo "error: rel engine server failed to become ready (see $LOGFILE)" >&2
  exit 1
fi

echo "rel engine server: starting in foreground"
echo "  socket: $SOCKET"
echo "  log:    $LOGFILE (also mirrored here)"
echo "  warm-up: typically $WARMUP_HINT_FIRST on first start, $WARMUP_HINT_CACHED when cached"
echo "  stop:    Ctrl+C in this terminal, or task rel-engine:stop in another"
echo ""

cleanup() {
  rm -f "$PIDFILE"
}
trap cleanup EXIT INT TERM

"$JULIA" "$ROOT/tests/generator/run_rel_engine_server.jl" "$SOCKET" 2>&1 | tee "$LOGFILE"
