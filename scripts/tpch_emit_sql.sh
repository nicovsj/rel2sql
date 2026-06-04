#!/usr/bin/env bash
# Translate TPC-H Rel queries to SQL under benchmarks/TPCH/out/ for diff against benchmarks/TPCH/sql/.
#
# Usage:
#   scripts/tpch_emit_sql.sh              # all queries 1-22 (full + solo)
#   scripts/tpch_emit_sql.sh 11 17 21     # subset
#   MODE=full scripts/tpch_emit_sql.sh 11 # only full (with common defs), not solo
#
# Env:
#   OUT_ROOT  default: benchmarks/TPCH/out
#   BIN       default: ./bazel-bin/rel2sql_bin
#   MODE          comma-separated: full,solo (default both)
#   TIMEOUT_SEC   per-query wall-clock limit (default 60; 0 = no limit)
#   SKIP_QUERIES  comma-separated query numbers to skip (e.g. 16 or 2,16)
#   UNOPTIMIZED   set to 1 to pass -u (skip sql::ast::Optimizer; default is optimized)
set -euo pipefail

cd "$(dirname "$0")/.."

ROOT="$(pwd)"
OUT_ROOT="${OUT_ROOT:-$ROOT/benchmarks/TPCH/out}"
REF_SQL="$ROOT/benchmarks/TPCH/sql"
EDB="$ROOT/benchmarks/TPCH/rel/tpch_edb.edb"
BIN="${BIN:-$ROOT/bazel-bin/rel2sql_bin}"
MODE="${MODE:-full,solo}"
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"
SKIP_QUERIES="${SKIP_QUERIES:-}"
UNOPTIMIZED="${UNOPTIMIZED:-0}"

mkdir -p "$OUT_ROOT/sql" "$OUT_ROOT/rel" "$OUT_ROOT/err"

if [[ $# -gt 0 ]]; then
  QUERIES=("$@")
else
  QUERIES=($(seq 1 22))
fi

should_skip_query() {
  local q="$1"
  [[ -n "$SKIP_QUERIES" ]] || return 1
  local IFS=,
  for s in $SKIP_QUERIES; do
    s="${s//[[:space:]]/}"
    [[ -n "$s" && "$s" == "$q" ]] && return 0
  done
  return 1
}

FILTERED=()
for q in "${QUERIES[@]}"; do
  if should_skip_query "$q"; then
    echo "[SKIP] Q$q (SKIP_QUERIES)" >&2
  else
    FILTERED+=("$q")
  fi
done
QUERIES=("${FILTERED[@]}")

if [[ ! -x "$BIN" ]]; then
  echo "Building rel2sql_bin..." >&2
  bazel build --config=default //:rel2sql_bin
fi

if [[ ! -f "$EDB" ]]; then
  echo "[WARN] EDB file missing: $EDB (run: python3 scripts/gen_tpch_edb.py)" >&2
fi

mode_enabled() {
  [[ ",$MODE," == *",$1,"* ]]
}

run_one() {
  local q="$1"
  local mode="$2" # full | solo
  local suffix=""
  local rel="$OUT_ROOT/rel/q${q}.rel"
  local sqlf="$OUT_ROOT/sql/q${q}.sql"
  local errf="$OUT_ROOT/err/q${q}.err"

  if [[ "$mode" == "solo" ]]; then
    suffix="_solo"
    rel="$OUT_ROOT/rel/q${q}_solo.rel"
    sqlf="$OUT_ROOT/sql/q${q}_solo.sql"
    errf="$OUT_ROOT/err/q${q}_solo.err"
    python3 scripts/tpch_rewrite.py "$q" --no-defs >"$rel"
  else
    python3 scripts/tpch_rewrite.py "$q" >"$rel"
  fi

  local -a cmd=("$BIN")
  [[ "$UNOPTIMIZED" == "1" ]] && cmd+=(-u)
  if [[ -f "$EDB" ]]; then
    cmd+=(-e "$EDB")
  fi
  cmd+=(-f "$rel")

  local ref="$REF_SQL/q${q}.sql"
  local ref_note=""
  if [[ -f "$ref" ]]; then
    ref_note=" (ref: $ref)"
  fi

  local rc=0
  if [[ "$TIMEOUT_SEC" != "0" ]]; then
    if perl -e 'alarm shift; exec @ARGV' "$TIMEOUT_SEC" "${cmd[@]}" >"$sqlf" 2>"$errf"; then
      :
    else
      rc=$?
    fi
  elif ! "${cmd[@]}" >"$sqlf" 2>"$errf"; then
    rc=$?
  fi

  if [[ $rc -eq 0 ]]; then
    echo "  [OK] Q$q $mode -> $sqlf${ref_note}"
    rm -f "$errf"
    return 0
  fi
  rm -f "$sqlf"
  if [[ $rc -eq 142 ]]; then
    echo "  [TIMEOUT] Q$q $mode (>${TIMEOUT_SEC}s)${ref_note}" >&2
    echo "Translation timed out after ${TIMEOUT_SEC}s." >"$errf"
  else
    echo "  [FAIL] Q$q $mode (see $errf)${ref_note}"
    head -6 "$errf" | sed 's/^/    /'
  fi
  return 1
}

failures=0
for q in "${QUERIES[@]}"; do
  echo "==================== Q$q ===================="
  if mode_enabled full; then
    run_one "$q" full || ((failures++)) || true
  fi
  if mode_enabled solo; then
    run_one "$q" solo || ((failures++)) || true
  fi
done

echo
echo "Output: $OUT_ROOT/sql/"
echo "Reference SQL: $REF_SQL/"
if [[ ${#QUERIES[@]} -eq 1 ]]; then
  q="${QUERIES[0]}"
  if [[ -f "$REF_SQL/q${q}.sql" && -f "$OUT_ROOT/sql/q${q}.sql" ]]; then
    echo "Diff: diff -u $REF_SQL/q${q}.sql $OUT_ROOT/sql/q${q}.sql"
  fi
fi

exit $((failures > 0 ? 1 : 0))
