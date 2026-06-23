#!/usr/bin/env bash
# Check whether the Rel engine server is running and responding to ping.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOCKET="${REL2SQL_REL_ENGINE_SOCKET:-$ROOT/.rel_engine.sock}"
PIDFILE="${REL2SQL_REL_ENGINE_PID:-$ROOT/.rel_engine.pid}"

if [[ ! -S "$SOCKET" ]]; then
  echo "rel engine server: not running (no socket at $SOCKET)"
  exit 1
fi

if [[ -f "$PIDFILE" ]]; then
  pid="$(cat "$PIDFILE")"
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "rel engine server: stale pid file (pid $pid not running)"
    rm -f "$PIDFILE" "$SOCKET"
    exit 1
  fi
  echo -n "rel engine server: pid $pid, "
else
  echo -n "rel engine server: socket present, "
fi

python3 - "$SOCKET" <<'PY'
import socket, sys
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(5.0)
sock.connect(sys.argv[1])
sock.sendall(b'{"op":"ping"}\n')
print("ping ok:", sock.recv(4096).decode().strip())
PY
