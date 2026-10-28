#!/usr/bin/env bash
# Translate benchmarks/TPCH/rel/wide/<q>.rel into benchmarks/TPCH/out/wide-sql/q<q>.sql.
#
# The wide queries read one relation per TPC-H table (w_lineitem, w_orders, ...; see
# benchmarks/TPCH/rel/wide/tpch_wide_edb.edb). Each output file starts with the views of
# benchmarks/TPCH/rel/wide/wide_views.sql, which expose the database's tpch_* tables under
# the column names the translation uses (A1, A2, ...), so it runs on the same databases as
# the reference SQL. Same layout as benchmarks/TPCH/out/sql, for tpch_explain_bench.py
# --gen-sql-dir.
#
# With NOKEYS=1, the translation gets the arities of the relations only, not their keys, and the
# output goes to benchmarks/TPCH/out/wide-nokeys-sql: the "No keys" column of the paper's table.
#
# Usage:
#   scripts/tpch_wide_emit_sql.sh          # every .rel in wide/
#   scripts/tpch_wide_emit_sql.sh 6 19     # just these
#   NOKEYS=1 scripts/tpch_wide_emit_sql.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIDE_DIR="$ROOT/benchmarks/TPCH/rel/wide"
OUT_REL="$ROOT/benchmarks/TPCH/out/wide-rel"
OUT_SQL="$ROOT/benchmarks/TPCH/out/wide-sql"
EDB="$WIDE_DIR/tpch_wide_edb.edb"
VIEWS="$WIDE_DIR/wide_views.sql"
if [[ "${NOKEYS:-0}" == 1 ]]; then
  OUT_SQL="$ROOT/benchmarks/TPCH/out/wide-nokeys-sql"
  mkdir -p "$OUT_SQL"
  EDB="$OUT_SQL/tpch_wide_edb_nokeys.edb"
  sed -E 's/ key( [0-9]+)+$//' "$WIDE_DIR/tpch_wide_edb.edb" >"$EDB"
fi
BIN="${BIN:-$ROOT/bazel-bin/rel2sql_bin}"
TIMEOUT_SEC="${TIMEOUT_SEC:-120}"

mkdir -p "$OUT_REL" "$OUT_SQL"

if [[ $# -gt 0 ]]; then
  queries=("$@")
else
  queries=()
  for f in "$WIDE_DIR"/*.rel; do
    [[ -e "$f" ]] || continue
    queries+=("$(basename "$f" .rel)")
  done
  IFS=$'\n' queries=($(sort -n <<<"${queries[*]}")); unset IFS
fi

failures=0
for q in "${queries[@]}"; do
  rel="$OUT_REL/q${q}.rel"
  sqlf="$OUT_SQL/q${q}.sql"
  if ! python3 "$ROOT/scripts/tpch_rewrite.py" "$q" --no-defs --queries-dir "$WIDE_DIR" >"$rel" 2>"$rel.err"; then
    echo "  [FAIL] Q$q rewrite -- $(head -c 160 "$rel.err")"
    ((failures++)); continue
  fi
  if perl -e 'alarm shift; exec @ARGV' "$TIMEOUT_SEC" "$BIN" -e "$EDB" -f "$rel" >"$sqlf.body" 2>"$sqlf.err"; then
    cat "$VIEWS" "$sqlf.body" >"$sqlf"
    rm -f "$sqlf.body"
    n=$(grep -oE '(FROM|,) +[a-z_][a-z0-9_]* AS T[0-9]+' "$sqlf" | wc -l | tr -d ' ')
    echo "  [OK]   Q$q -> $sqlf (sources=$n)"
  else
    echo "  [FAIL] Q$q translate -- $(head -c 160 "$sqlf.err")"
    rm -f "$sqlf" "$sqlf.body"
    ((failures++))
  fi
done

echo
echo "Output: $OUT_SQL"
[[ $failures -eq 0 ]] || { echo "$failures failure(s)"; exit 1; }
