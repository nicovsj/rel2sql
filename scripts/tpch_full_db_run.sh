#!/usr/bin/env bash
# Rewrite, translate, and optionally compare TPC-H query results in DuckDB.
#
# Usage:
#   TPCH_DUCKDB_PATH=/path/to/tpch.duckdb scripts/tpch_full_db_run.sh 18
#   scripts/tpch_full_db_run.sh --compare 18
#   scripts/tpch_full_db_run.sh --all
#
# Without TPCH_DUCKDB_PATH, runs pipeline stages only (no result compare).
set -euo pipefail

cd "$(dirname "$0")/.."

ROOT="$(pwd)"
RUNNER="${RUNNER:-$ROOT/bazel-bin/benchmarks/TPCH/tpch_runner}"
COMPARE=0
EXTRA=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --compare)
      COMPARE=1
      shift
      ;;
    --all)
      EXTRA+=(--all)
      shift
      ;;
    -h|--help)
      echo "Usage: $0 [--compare] [--all] [QUERY...]"
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

if [[ ! -x "$RUNNER" ]]; then
  echo "Building tpch_runner..." >&2
  bazel build --config=default //benchmarks/TPCH:tpch_runner
fi

export REL2SQL_REPO_ROOT="$ROOT"
export REL2SQL_BIN="${REL2SQL_BIN:-$ROOT/bazel-bin/rel2sql_bin}"

if [[ $COMPARE -eq 1 ]]; then
  if [[ -z "${TPCH_DUCKDB_PATH:-}" ]]; then
    echo "TPCH_DUCKDB_PATH must point to a DuckDB file with TPC-H data (both reference and rel2sql schemas)." >&2
    echo "See benchmarks/TPCH/pipeline/README.md" >&2
    exit 1
  fi
  EXTRA+=(--compare)
fi

if [[ $# -eq 0 && " ${EXTRA[*]} " != *" --all "* ]]; then
  set -- 18
fi

for q in "$@"; do
  echo "==================== Q$q ===================="
  "$RUNNER" "${EXTRA[@]}" --query "$q"
done
