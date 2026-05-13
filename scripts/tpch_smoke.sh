#!/usr/bin/env bash
# Smoke-test: apply Section 1 rewrites and pipe each MVP query through rel2sql.
set -u

cd "$(dirname "$0")/.."

QUERIES=(11 17 18 19 21)
BIN=./bazel-bin/rel2sql_bin
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
  if "$BIN" -u -f "$rel" > "$sqlf" 2> "$errf"; then
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
