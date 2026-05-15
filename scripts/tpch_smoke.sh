#!/usr/bin/env bash
# Smoke-test: apply Section 1 rewrites and pipe each MVP query through rel2sql.
# Uses benchmarks/TPCH/rel/tpch_edb.edb when present (run: python3 scripts/gen_tpch_edb.py).
set -u

cd "$(dirname "$0")/.."

QUERIES=(11 17 18 19 21)
BIN=./bazel-bin/rel2sql_bin
EDB="$(pwd)/benchmarks/TPCH/rel/tpch_edb.edb"
OUT=/tmp/tpch_smoke_out
mkdir -p "$OUT"

run_one() {
  local q="$1"
  local mode="$2"     # "full" (with common defs) or "solo"
  local label="$3"
  local rel="$OUT/q${q}_${mode}.rel"
  local sqlf="$OUT/q${q}_${mode}.sql"
  local errf="$OUT/q${q}_${mode}.err"
  if [[ "$mode" == "solo" ]]; then
    python3 scripts/tpch_rewrite.py "$q" --no-defs > "$rel"
  else
    python3 scripts/tpch_rewrite.py "$q" > "$rel"
  fi
  echo "-- $label"
  if [[ ! -f "$EDB" ]]; then
    echo "  [WARN] EDB file missing: $EDB (run: python3 scripts/gen_tpch_edb.py)" >&2
  fi
  local -a cmd
  if [[ -f "$EDB" ]]; then
    cmd=("$BIN" -u -e "$EDB" -f "$rel")
  else
    cmd=("$BIN" -u -f "$rel")
  fi
  if "${cmd[@]}" > "$sqlf" 2> "$errf"; then
    echo "  [OK] $sqlf ($(wc -l < "$sqlf") lines)"
  else
    head -6 "$errf" | sed 's/^/    /'
  fi
}

for q in "${QUERIES[@]}"; do
  echo
  echo "==================== Q$q ===================="
  run_one "$q" "full" "with tpch_common_defs.rel"
  run_one "$q" "solo" "query-only (no common defs)"
done
