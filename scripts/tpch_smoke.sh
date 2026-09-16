#!/usr/bin/env bash
# MVP TPC-H emit: Q11, Q17, Q18, Q19, Q21 in full (with tpch_common_defs.rel) and solo (--no-defs).
# Solo may E100 on queries that need common defs (e.g. l_revenue); see benchmarks/TPCH/out/err/.
set -u

cd "$(dirname "$0")/.."
export MODE=full,solo
exec scripts/tpch_emit_sql.sh 11 17 18 19 21
