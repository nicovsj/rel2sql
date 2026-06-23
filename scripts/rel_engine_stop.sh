#!/usr/bin/env bash
# Stop the persistent RAICode Rel engine server.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOCKET="${REL2SQL_REL_ENGINE_SOCKET:-$ROOT/.rel_engine.sock}"
PIDFILE="${REL2SQL_REL_ENGINE_PID:-$ROOT/.rel_engine.pid}"

if [[ -S "$SOCKET" ]]; then
  python3 - "$SOCKET" <<'PY' || true
import socket, sys
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(5.0)
try:
    sock.connect(sys.argv[1])
    sock.sendall(b'{"op":"shutdown"}\n')
    sock.recv(4096)
except OSError:
    pass
PY
fi

if [[ -f "$PIDFILE" ]]; then
  pid="$(cat "$PIDFILE")"
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    for _ in $(seq 1 30); do
      kill -0 "$pid" 2>/dev/null || break
      sleep 1
    done
    kill -9 "$pid" 2>/dev/null || true
  fi
  rm -f "$PIDFILE"
fi

rm -f "$SOCKET"
echo "rel engine server stopped"
