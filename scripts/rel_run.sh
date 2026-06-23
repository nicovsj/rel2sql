#!/usr/bin/env bash
# Run arbitrary Rel via RAICode (prefers warm rel-engine server).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -f "$ROOT/third_party/raicode/Project.toml" ]]; then
  echo "error: third_party/raicode not initialized (git submodule update --init third_party/raicode)" >&2
  exit 1
fi

export JULIA_PROJECT="${JULIA_PROJECT:-$ROOT/third_party/raicode}"
export REL2SQL_REL_ENGINE_SOCKET="${REL2SQL_REL_ENGINE_SOCKET:-$ROOT/.rel_engine.sock}"

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

if [[ $# -eq 0 ]]; then
  echo "Usage: task rel-engine:run -- [options] <program.rl|->" >&2
  echo "       scripts/rel_run.sh --output myout query.rl" >&2
  echo "       echo 'def output {1}' | scripts/rel_run.sh -" >&2
  echo "Options: --output, --edb, --json, --raw (see tests/generator/run_rel_snippet.jl)" >&2
  exit 2
fi

if [[ -S "$REL2SQL_REL_ENGINE_SOCKET" ]]; then
  exec "$JULIA" "$ROOT/tests/generator/run_rel_snippet.jl" "$@"
fi

echo "note: rel engine server not running; using one-shot Julia (slow). Start with: task rel-engine:start" >&2
exec "$JULIA" "$ROOT/tests/generator/run_rel_snippet.jl" "$@"
