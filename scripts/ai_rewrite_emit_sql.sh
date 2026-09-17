#!/usr/bin/env bash
# Translate benchmarks/TPCH/rel/ai-rewrites/<q>.rel into benchmarks/TPCH/out/ai-sql/q<q>.sql.
#
# Same layout as benchmarks/TPCH/out/sql, so the result can be fed to
# tpch_explain_bench.py --gen-sql-dir for a like-for-like EXPLAIN ANALYZE against
# the queries in benchmarks/TPCH/rel/queries.
#
# Usage:
#   scripts/ai_rewrite_emit_sql.sh          # every .rel in ai-rewrites
#   scripts/ai_rewrite_emit_sql.sh 6 19     # just these
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AI_DIR="$ROOT/benchmarks/TPCH/rel/ai-rewrites"
OUT_REL="$ROOT/benchmarks/TPCH/out/ai-rel"
OUT_SQL="$ROOT/benchmarks/TPCH/out/ai-sql"
EDB="$ROOT/benchmarks/TPCH/rel/tpch_edb.edb"
BIN="${BIN:-$ROOT/bazel-bin/rel2sql_bin}"
TIMEOUT_SEC="${TIMEOUT_SEC:-120}"

mkdir -p "$OUT_REL" "$OUT_SQL"

if [[ $# -gt 0 ]]; then
  queries=("$@")
else
  queries=()
  for f in "$AI_DIR"/*.rel; do
    [[ -e "$f" ]] || continue
    queries+=("$(basename "$f" .rel)")
  done
  IFS=$'\n' queries=($(sort -n <<<"${queries[*]}")); unset IFS
fi

failures=0
for q in "${queries[@]}"; do
  rel="$OUT_REL/q${q}.rel"
  sqlf="$OUT_SQL/q${q}.sql"
  if ! python3 "$ROOT/scripts/tpch_rewrite.py" "$q" --queries-dir "$AI_DIR" >"$rel" 2>"$rel.err"; then
    echo "  [FAIL] Q$q rewrite -- $(head -c 160 "$rel.err")"
    ((failures++)); continue
  fi
  if perl -e 'alarm shift; exec @ARGV' "$TIMEOUT_SEC" "$BIN" -e "$EDB" -f "$rel" >"$sqlf" 2>"$sqlf.err"; then
    n=$(grep -oE '(FROM|,) +[a-z_][a-z0-9_]* AS T[0-9]+' "$sqlf" | wc -l | tr -d ' ')
    echo "  [OK]   Q$q -> $sqlf (sources=$n)"
  else
    echo "  [FAIL] Q$q translate -- $(head -c 160 "$sqlf.err")"
    rm -f "$sqlf"
    ((failures++))
  fi
done

echo
echo "Output: $OUT_SQL"
[[ $failures -eq 0 ]] || { echo "$failures failure(s)"; exit 1; }
