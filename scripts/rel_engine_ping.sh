#!/usr/bin/env bash
# Ping the Rel engine server (quick connectivity check).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOCKET="${REL2SQL_REL_ENGINE_SOCKET:-$ROOT/.rel_engine.sock}"

if [[ ! -S "$SOCKET" ]]; then
  echo "error: no socket at $SOCKET (is task rel-engine:start running?)" >&2
  exit 1
fi

python3 - "$SOCKET" <<'PY'
import json, socket, sys
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(10.0)
sock.connect(sys.argv[1])
sock.sendall(b'{"op":"ping"}\n')
data = sock.recv(4096).decode()
print("ping response:", data.strip())
resp = json.loads(data.splitlines()[0])
sys.exit(0 if resp.get("ok") else 1)
PY
