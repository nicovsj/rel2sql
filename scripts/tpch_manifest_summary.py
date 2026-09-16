#!/usr/bin/env python3
"""Print benchmarks/TPCH/pipeline/manifest.json as a human-readable table.

Example:
  python3 scripts/tpch_manifest_summary.py
  python3 scripts/tpch_manifest_summary.py --notes
  task tpch:manifest-summary
  task tpch:manifest-summary -- --notes
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import textwrap

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = ROOT / "benchmarks/TPCH/pipeline/manifest.json"

# Stages in left-to-right pipeline order; each is a (json_field, header) pair.
STAGE_COLUMNS = [
    ("rewrite", "rewrite"),
    ("translate", "translate"),
    ("execute_empty", "execute"),
    ("compare_local", "compare"),
]

STATUS_GLYPH = {"ok": "ok", "verified": "ok", "fail": "FAIL", "known_bug": "BUG", "skip": "-"}


def status_cell(value: str | None) -> str:
    if not value:
        return "-"
    return STATUS_GLYPH.get(value, value)


def first_note(entry: dict, width: int) -> str:
    """The most specific note available for a query: compare > execute > translate."""
    for note_field in ("compare_local_note", "execute_empty_note", "translate_note"):
        note = entry.get(note_field)
        if note:
            note = re.sub(r"\s+", " ", note).strip()
            return textwrap.shorten(note, width=width, placeholder="...")
    return ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("manifest", nargs="?", type=pathlib.Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--notes", action="store_true", help="Print each query's own note block below the table")
    ap.add_argument("--note-width", type=int, default=60, help="Truncation width for the table's inline note column")
    args = ap.parse_args()

    data = json.loads(args.manifest.read_text())
    queries = data.get("queries", {})
    rows = sorted(queries.items(), key=lambda kv: int(kv[0]))

    col_widths = {field: max(len(header), *(len(status_cell(v.get(field))) for _, v in rows)) for field, header in STAGE_COLUMNS}
    q_width = max(len("Q#"), max(len(f"Q{n}") for n, _ in rows))
    note_width = args.note_width

    header = (
        f"{'Q#':<{q_width}}  " + "  ".join(f"{h:<{col_widths[f]}}" for f, h in STAGE_COLUMNS) + f"  {'note':<{note_width}}"
    )
    print(header)
    print("-" * len(header))

    counts: dict[str, int] = {}
    for n, entry in rows:
        cells = "  ".join(f"{status_cell(entry.get(f)):<{col_widths[f]}}" for f, _ in STAGE_COLUMNS)
        note = first_note(entry, note_width)
        print(f"{'Q' + n:<{q_width}}  {cells}  {note}")
        counts[entry.get("compare_local", "unknown")] = counts.get(entry.get("compare_local", "unknown"), 0) + 1

    print("-" * len(header))
    total = len(rows)
    summary_parts = [f"{v} {k}" for k, v in sorted(counts.items(), key=lambda kv: -kv[1])]
    print(f"{total} queries: " + ", ".join(summary_parts))

    if args.notes:
        print()
        for n, entry in rows:
            for note_field, label in (
                ("translate_note", "translate"),
                ("execute_empty_note", "execute_empty"),
                ("compare_local_note", "compare_local"),
            ):
                note = entry.get(note_field)
                if note:
                    print(f"Q{n} ({label}): {note}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
