# Getting the TPC-H Rel queries through rel2sql — plan and gap analysis

**Scope:** `benchmarks/TPCH/rel/queries/{1..22}.rel`, run against the current rel2sql grammar/translator
(source read directly from the repo: `RelParser.g4`, `RelLexer.g4`, `src/preprocessing/safety_inferrer.{h,cc}`,
`src/preprocessing/builtin_resolver.{h,cc}`, `src/sql/translator.cc`, `src/preprocessing/arity_visitor.cc`,
`scripts/tpch_rewrite.py`, `benchmarks/TPCH/pipeline/manifest.json`, and every current `.err` file).

**Method note / honesty check.** Everything below is derived from reading the translator source and the
existing test suite as ground truth (particularly which idioms already have a `translate: ok` test or
manifest entry). I was **not able to actually run `rel2sql_bin`** in this session — building it is a full
Bazel C++ build that doesn't fit in the short-lived remote shell calls available here. Every fix below is
a source-grounded prediction, not an empirically re-verified one. Before trusting this plan, run the
pipeline (`task tpch:pipeline-test` or equivalent) on the rewritten queries and treat any mismatch as a
correction to this document, not to the queries. I've flagged the one place (Q1/Q4/Q8/Q15/Q20/Q22's
single-level `sort[family]` idiom) where I could not find a currently-passing test that proves the pattern
works, as opposed to the double-nested `reverse_sort` idiom which **is** proven by Q18/Q21's passing status.

**Bottom line.** Of the 22 queries, 5 currently translate (9, 11, 12, 17, 18) and 1 more executes but is
skipped for a different reason (16, optimizer hang). Of the remaining 16 failures, all but one component
(Q16's hang) trace back to nine distinct, well-understood causes, and **every one of them has a
semantics-preserving query-level rewrite** — none requires new SQL-generation logic in rel2sql. The
one item that's a genuine rel2sql gap (`<++`/`++>` override) turns out not to block Q13 either, because
Q13's specific use of `<++` reduces to the same "make total over a domain with a default" pattern as Q8,
which is expressible without the operator. So Part 2 is shorter than you might expect — I did not want to
manufacture gaps to pad it out.

---

## Part 1 — Rewrite plan

### 1.0 Current status (manifest.json, before any changes)

| # | Status | Failure |
|---|---|---|
| 1, 3, 4, 6, 7, 10, 14, 15, 20 | `translate: fail` | `E104` — comparison term couldn't be built |
| 2 | `translate: fail` | `E1` syntax error on `__rest1...` |
| 5 | `translate: fail` | `E100` — `reverse_sort` "not defined" |
| 8 | `translate: fail` | `E100` — `default_value` "not defined" |
| 13 | `translate: fail` | `E1` syntax error on `<++` |
| 19, 21 | `translate: fail` | `E102` — comparison variable "not bound (safety check failed)" |
| 22 | `translate: fail` | arity error on `dec[0]` |
| 9, 11, 12, 17, 18 | `translate: ok` | — |
| 16 | `translate: skip` | optimizer (`MergeWith`) hangs on a long OR chain |

### 1.1 Pattern A — materialize bare references to 0-ary helper defs before comparing them

**Root cause (confirmed from `src/sql/translator.cc`).** `RelComparison` translation calls
`BuildSqlTermFromLinearRelTerm` on each side. That function only produces SQL for a numeric/string
literal, a parenthesis/op-term, or a `RelIDTerm` that is *already in `term_sources`* — i.e. a variable
actually bound by an enclosing `exists`/comprehension/atom. It has no case for a bare identifier that
resolves to a **0-ary top-level def** (`lower[]`, `upper[]`, `target_date[]`, `lower_shipdate[]`, …),
because such an identifier is a zero-argument relation application, not a bound variable, and nothing
lowers it into `term_sources` before the comparison is built. `TermRewriter` — the pass that normally
rescues expressions-as-terms by lifting them into `exists z. app(z) and …` — only fires on
`RelExprAsTerm` nodes (applications *with* arguments); a bare 0-ary name doesn't take that shape, so it's
never lifted. Result: any `<`, `<=`, `>`, `>=` comparison with a bare 0-ary-def operand fails to build,
whether or not it's part of a chained comparison. This single root cause accounts for **all nine** of the
E104 failures (1, 3, 4, 6, 7, 10, 14, 15, 20) — I checked every one and they all share exactly this shape.

**Fix.** Bind the 0-ary def to a fresh existential variable via an explicit equality before comparing.
An equality against an application *does* get lifted correctly (this exact idiom already has a passing
test — `TpchQ9FullExistsOptimized` in `tests/test_translation.cc`), so this sidesteps the gap entirely.

```rel
// before (Q6)
lower_shipdate <= l_shipdate[o, num] < upper_shipdate
and lower_discount <= l_discount[o, num] <= upper_discount

// after
exists((lo_sd, hi_sd, lo_disc, hi_disc) |
    lo_sd = lower_shipdate and hi_sd = upper_shipdate and
    lo_disc = lower_discount and hi_disc = upper_discount and
    lo_sd <= l_shipdate[o, num] and l_shipdate[o, num] < hi_sd and
    lo_disc <= l_discount[o, num] and l_discount[o, num] <= hi_disc
)
```

The same shape applies to Q1 (`upper_shipdate`), Q3 (`target_date`, used twice — once outside the `sum[]`
and once inside; bind once in the outer scope, the inner comprehension can see the same bound variable),
Q4 (`lower`/`upper`), Q7 (`lower`/`upper`), Q10 (`lower`/`upper`), Q14 (`lower`/`upper`), Q15
(`lower`/`upper`), Q20 (`lower`/`upper`).

**Automation.** This is mechanical enough to add as a new rewrite stage in `scripts/tpch_rewrite.py`
(a "Section 1k" pass): find every 0-ary `def NAME[]: ...` referenced bare inside a comparison, and
auto-wrap the containing formula in the `exists` binding. I'd still hand-verify each query once rather
than trust the regex blindly, given how irregular the surrounding formula shapes are.

### 1.2 Pattern B — residual risk: single-level `sort[family]`

Not a distinct failure today (masked by Pattern A's E104 in the same queries), but worth flagging before
you assume Q1/Q4/Q8/Q15/Q20/Q22 are fully fixed by Pattern A alone. All six use `sort[unsorted_result]` or
`sort[unsorted_result[k]]` where `unsorted_result` is a **curried family** (`unsorted_result[flag,status]:
...`) rather than the double-nested `reverse_sort[reverse_sort[R[a]]]` idiom used by Q3/Q16/Q18/Q21. The
double-nested idiom **is** proven working (Q18 and Q21 both reach `translate: ok`/`execute_empty: ok`
today). The single-level family form is not exercised by any currently-passing query or test I could find
in `tests/test_translation.cc`. It's plausible `BuiltinResolver`'s `sort`/`reverse_sort` lowering handles a
curried family transparently (lexicographic sort by the family's key columns, which is exactly what Q1
wants for `order by l_returnflag, l_linestatus`), but I have no direct evidence either way. **Recommend
testing this idiom in isolation first** (a two-line repro: a 2-parameter family summed/counted, then
`sort[family]`) before relying on Pattern A alone to unblock these six queries. If it turns out
unsupported, the fallback is to rewrite each into the same double-nested `reverse_sort` shape already
proven for Q3/Q18/Q21 (mechanical, just more verbose).

### 1.3 Pattern C — Q2's `__rest1...` vararg capture

**Root cause.** `def result(i,x1, __rest1...): final_sort(i,x1,_,__rest1...) and i <= 100` uses Rel's
rest-capture/vararg binding syntax, which has no counterpart in `RelParser.g4`'s `binding`/`productInner`
rules — hence the plain syntax error. This is pure sugar for "name the remaining seven columns
individually"; `final_sort` here always produces a fixed 10-tuple (see the double-`reverse_sort` idiom in
§1.2), so the arity is statically known and the rest-capture buys nothing but brevity.

```rel
// before
def result(i,x1, __rest1...): final_sort(i,x1,_,__rest1...) and i <= 100

// after
def result(i, x1, x2, x3, x4, x5, x6, x7, x8):
    final_sort(i, x1, _, x2, x3, x4, x5, x6, x7, x8) and i <= 100
```

Zero semantic change — this is a textual desugaring, not a rewrite of logic.

### 1.4 Pattern D — Q5's relational-form `reverse_sort`

**Root cause (confirmed in `builtin_resolver.cc`).** `sort`/`reverse_sort` are recognized *only* via the
bracket/partial-application path (`reverse_sort[R]`); there is no branch in `TryLowerFull` for the
relational call form `reverse_sort(R, i, v, n)`. Q5 is the only query in the set that calls it this way, so
it falls through to "undefined relation" — matching the observed `E100`. Q3/Q16/Q18/Q21 all use the bracket
form (via the double-nesting idiom), which is why they don't hit this.

```rel
// before
def revenue_sum[n in nations]: sum[...]
def unsorted_result(v, target_nation): exists((n) | revenue_sum(n, v) and n_name(n, target_nation))
def result(i, n, v): reverse_sort(unsorted_result, i, v, n)

// after — reuse the exact idiom already proven by Q3/Q18/Q21
def inside_rev_sort[v]: reverse_sort[unsorted_result[v]]
def final_sort[]: reverse_sort[inside_rev_sort]
def result(i, n, v): exists((i1) | final_sort(i, v, i1, n))
```

### 1.5 Pattern E — Q8's 3-argument `default_value`

**Root cause (confirmed in `builtin_resolver.cc` and `translator.cc`).** rel2sql's `default_value[a, b]`
is a strict 2-argument scalar `COALESCE(a, b)` — it requires `params.size() == 2` and lowers straight to
SQL `COALESCE`. TPC-H Q8 calls the *3-argument* stdlib form, `default_value[D, F, c]` ("make `F` total over
domain `D` with default `c`" — confirmed against the official Rel stdlib docs), which is a genuinely
different, domain-join operation that rel2sql has no implementation of at all. With 3 arguments the arity
guard simply doesn't match, so the call falls through untranslated and `default_value` is looked up as an
ordinary (undefined) relation — hence `E100`.

This is expressible without the builtin at all, using the standard "total function via union + negated
existence" pattern (the same trick used for Q13 below):

```rel
// before
def unsorted_result[]: default_value[years_domain, market_share, parse_decimal[64,4,"0"]]

// after
def unsorted_result(o_year, v):
    (years_domain(o_year) and market_share(o_year, v))
    or
    (years_domain(o_year) and v = parse_decimal[64,4,"0"] and not exists((v2) | market_share(o_year, v2)))
```

### 1.6 Pattern F — Q13's `<++` (left override)

**Root cause (confirmed in both grammar files).** `RelLexer.g4` defines `T_LEFT_OVERRIDE ('<++')` and
`T_RIGHT_OVERRIDE ('++>')` as real tokens, but `RelParser.g4`'s `term`/`expr`/`formula` rules never consume
them — they're lexed and then immediately rejected by the parser. There is no other construct in the
accepted grammar that reproduces general relational override semantics. **This is a real, hard grammar
gap** — but Q13's specific use of it is a special case that doesn't need the general operator:

```rel
// before — override: use the left side's count, or 0 for any customer key not covered by it
def temp[]:
    ([ck]: count[(ok): o_custkey(ok, ck) and not(like_match(raw"%@@1%@@2%", (ok.o_comment)))])
    <++ (customer, 0)
```

is exactly "make the count function total over the domain `customer`, default `0`" — the same shape as
Q8's `default_value`, so the same union+negation elimination applies:

```rel
// after
def matched(ck, custdist):
    custdist = count[(ok): o_custkey(ok, ck) and not(like_match(raw"%@@1%@@2%", (ok.o_comment)))]

def temp(ck, custdist):
    (customer(ck) and matched(ck, custdist))
    or
    (customer(ck) and custdist = 0 and not exists((c2) | matched(ck, c2)))
```

See §2.1 for why I still think rel2sql should eventually support `<++`/`++>` natively, even though it
isn't required for this benchmark.

### 1.7 Pattern G — Q19's `quantity_range`, a helper whose parameter is only ever compared

**Root cause (confirmed in `safety_inferrer.cc`).** A `def`'s own formal parameters are **not** pre-seeded
as "bound" before the safety fixpoint runs — they go through the identical `ComputeBindingsSafety` path as
an `exists`-bound variable, meaning they only count as safe if they get a positive defining occurrence
(an atom, or an `=` equality) somewhere in the body. A `<=`/`<`/`>`/`>=` comparison never counts
(`RelComparison::Visit` only inserts bindings for `op == EQ`). Q19's helper

```rel
def quantity_range(o, l, param): param <= l_quantity[o, l] <= param + 10
```

uses `param` *only* in inequalities, so it can never become "bound" no matter how the comparison is
phrased or chained — this is what the `E102 (safety check failed)` is actually about (the reported
variable name in the stale `.err` file is a rewriter-internal placeholder, but the structural cause is the
same class of problem as Pattern A: a variable that's never positively grounded).

The fix here is different from Pattern A, though, because `param` isn't a global 0-ary def — it's a
parameter that's always instantiated with a **compile-time literal** at every call site (Q19's three call
sites pass `@@4`, `@@5`, `@@6`, which `scripts/tpch_rewrite.py`'s existing `substitute_params` step already
turns into literal numbers before translation ever runs). A literal needs no safety binding at all, so
simply inlining the helper eliminates the problem:

```rel
// before
@inline def quantity_range(o, l, param): param <= l_quantity[o, l] <= param + 10
...
quantity_range(o, l, @@4) and 1 <= (pk.p_size) <= 5 and ...   // (x3, once per sub_query)

// after — inline at each of the 3 call sites, using the already-substituted literal directly
@@4 <= l_quantity[o, l] and l_quantity[o, l] <= @@4 + 10 and 1 <= (pk.p_size) <= 5 and ...
```

(and the equivalent for `@@5`/`@@6` in `sub_query2`/`sub_query3`). Drop the `quantity_range` def entirely.

### 1.8 Pattern H — Q21's `sub_query1`/`sub_query2`, a data-derived parameter only ever compared

Same root cause as Pattern G (a def parameter used only in `!=`), but here the parameter (`o1l1_suppkey`)
is bound to an actual data value at the call site (`sub_query1(o1, sk)` where `sk` comes from
`l_suppkey(o1, l1, sk)`), not a literal — so inlining-with-a-literal doesn't apply. Instead, add a
redundant-but-always-true positive atom that grounds the parameter, exploiting the fact that every actual
caller already guarantees it:

```rel
// before
@inline def sub_query1(o1, o1l1_suppkey): exists((l2) | l_suppkey[o1, l2] != o1l1_suppkey)

@inline def sub_query2(o1, l1, o1l1_suppkey):
    not(exists((l3) |
        l_suppkey[o1, l3] != o1l1_suppkey and
        l_suppkey(o1, l1, o1l1_suppkey) and
        l_receiptdate[o1, l3] > l_commitdate[o1, l3]
    ))

// after
@inline def sub_query1(o1, o1l1_suppkey):
    exists((l1b) | l_suppkey(o1, l1b, o1l1_suppkey)) and
    exists((l2) | l_suppkey[o1, l2] != o1l1_suppkey)

@inline def sub_query2(o1, l1, o1l1_suppkey):
    l_suppkey(o1, l1, o1l1_suppkey) and
    not(exists((l3) |
        l_suppkey[o1, l3] != o1l1_suppkey and
        l_receiptdate[o1, l3] > l_commitdate[o1, l3]
    ))
```

**Caveat worth double-checking by hand:** this makes each helper's *standalone* extension narrower than the
original (it now requires `o1l1_suppkey` to actually be a supplier key of `o1`), which is fine here only
because that's already guaranteed at the one call site each helper has in `lineitems_query`. I traced this
through by hand (§ analysis) and it holds, but this is exactly the kind of local reasoning that's worth a
second pair of eyes (or a differential test against the reference SQL) before trusting it at SF1/SF10.

### 1.9 Pattern I — Q22's `dec[0]` arity error

**Root cause.** `tpch_common_defs.rel` defines `dec` as a bare alias to a partial application:
`def dec { decimal[64, 4] }`. Every call site (across nearly all 22 queries) uses it as `dec[x]`, treating
it as if it were itself a unary macro — but as written, `dec` is a **0-ary def whose value is a curried
relation**, and re-applying that curried value via `dec[0]` requires rel2sql to support runtime
re-currying of a stored higher-order value, which its restricted application model doesn't do (hence
"Expression-as-term operand must have arity 1, got 0"). This is the one spot where the benchmark's Rel
happens to lean on a genuinely higher-order feature, but the fix is trivial because nothing actually
*needs* the higher-order behavior — every use immediately re-applies it to exactly one argument:

```rel
// before (tpch_common_defs.rel)
@inline
def dec { decimal[64, 4] }

// after
@inline
def dec[x]: decimal[64, 4, x]
```

This is a one-line, one-place fix (in the shared `tpch_common_defs.rel`, not per-query) and is semantically
identical for every existing call site (`dec[X]` still means `decimal[64,4,X]`), so it can't change any
result.

### 1.10 Q16 — optimizer hang, not a translation failure

Q16 already parses and would presumably translate; per `manifest.json` it's marked `translate: skip` with
`"skip_reason": "MergeWith on long OR chains can hang"` — an 8-way `size = @@3 or size = @@4 or ... or
size = @@10` disjunction inside `inner_query`. This is categorically different from everything above: it's
not a missing language feature, it's a **robustness/termination bug in an existing optimizer pass**
(`MergeWith`) on a particular formula shape. Two independent things worth trying, in order:

1. **Query-level workaround (try first, cheap):** restructure the 8-way `or` chain into set/membership
   form, e.g. a small helper `def target_size(s): s = @@3 or s = @@4 or ...` referenced as
   `target_size(size)`, or (if the grammar's literal-set syntax reaches the parser cleanly) a direct
   membership test against an inline set literal. This changes the AST shape without changing semantics,
   and might dodge whatever pattern `MergeWith` chokes on.
2. **If that doesn't help:** this is a genuine rel2sql defect (see §2.2) — a hang is worse than a rejection,
   since it doesn't fail loudly, and it deserves a bug report / repro independent of this benchmark.

---

## Part 2 — Genuine rel2sql gaps

Being honest about what's actually irreducible here: **nothing in this specific set of 22 queries strictly
requires new rel2sql capability to produce a correct translation** — every failure above has a
result-preserving rewrite that stays inside the accepted grammar. That said, two things surfaced during
this analysis that are worth rel2sql taking on anyway, because the workarounds are fragile or because they
reflect the translator disagreeing with documented Rel semantics rather than just lacking sugar:

### 2.1 `<++` / `++>` (override) — recommend adding as a desugaring pass, not full new codegen

The general override operator is lexed but has zero parser/AST/translator support (§1.6). Q13's specific
use happens to reduce to a union+negation pattern, but that reduction relied on knowing, by hand, which
argument position is the "key" the override is keyed on — that's not something you can safely automate in
general (it required reading and reasoning about how `temp[]`'s left-hand side is shaped). Any future Rel
program that uses `<++`/`++>` on relations where the key/value split isn't a simple "last column is the
value" case would hit this same wall, and a human would have to redo this reasoning by hand each time.
The good news is the fix is cheap relative to a full new SQL-generation feature: `R <++ S` (and `S ++> R`)
is *always* semantically equal to `R or (S and not exists(matching key in R))`, given the standard
convention that override applies to the trailing column(s). Implementing `<++`/`++>` as a **preprocessing
AST desugaring** into that union+not-exists shape (before the existing translator ever sees it) would
close this gap without touching SQL codegen at all — the same trick I did by hand in §1.6, just done
generically and automatically, and it would also need a way to determine/declare which columns are "key"
vs "value" (probably: value = last column, key = everything else, matching the common Rel convention,
with an explicit error if someone overrides two relations of mismatched arity).

### 2.2 `MergeWith` optimizer hang on long OR chains (Q16)

Independent of language support — the grammar and translator both presumably accept Q16's query, but the
optimizer pass doesn't terminate in reasonable time on its 8-way disjunction. This deserves its own
minimal repro and bug report (a synthetic `def f(x): x=1 or x=2 or ... or x=8` fed through the optimizer
standalone) regardless of whether the §1.10 workaround happens to dodge it for Q16 specifically — an
optimizer that can hang on a query-shape this common is a real reliability risk for any future benchmark
query, not just this one.

### 2.3 Worth a source-level fix even though it isn't a hard blocker: `default_value`'s 3-arg form

`default_value[D, F, c]` ("make `F` total over domain `D` with default `c`") is documented as a real,
first-class stdlib relation. rel2sql currently implements only a degenerate 2-argument
special case (`COALESCE`) under the same name. This isn't blocking anything here (§1.5's rewrite works),
but it means rel2sql silently disagrees with the documented Rel semantics for this name rather than merely
lacking sugar for it — someone reading the stdlib docs and reaching for `default_value[D, F, c]` will get a
confusing "relation not defined" instead of a "not yet supported" signal. Worth either implementing the
3-arg form for real (it's exactly the union+not-exists shape from §1.5, so it's a small, well-scoped
addition) or at minimum special-casing the 3-arg call to raise a clear "not implemented" error instead of
falling through to "undefined relation."

---

## Suggested next steps

1. Get a `rel2sql_bin` build runnable end-to-end (even just for these 22 queries) so every rewrite above
   can be checked against the pipeline's own `compare_local` DuckDB harness rather than trusted from static
   source reading alone.
2. Apply Patterns A/C/D/E/F/G/H/I as edits to `benchmarks/TPCH/rel/queries/*.rel` and
   `tpch_common_defs.rel`; extend `scripts/tpch_rewrite.py` for the mechanical ones (Pattern A, Pattern I)
   so they don't need to be maintained by hand.
3. Validate §1.2's open question (single-level `sort[family]`) with a minimal repro before assuming
   Q1/Q4/Q8/Q15/Q20/Q22 are fully unblocked by Pattern A alone.
4. Re-run the pipeline; update `manifest.json` and `docs/tpch-rel2sql-roundtrip-catalog.md` to reflect the
   new pass/fail state (that doc is currently stale relative to the actual manifest — e.g. it lists Q19/Q21
   as blocked only by "EDB binding," not the E102 safety issue actually observed).
5. File the two Part 2 items (override desugaring, `MergeWith` hang) as separate rel2sql issues, since
   they're worth fixing on their own merits independent of this benchmark.

---

## Verification notes (2026-09-11) — what a real build changed

`rel2sql_bin` now builds (`bazel build --config=default //:rel2sql_bin`; plain `bazel build` without
`--config=default` fails with unrelated Xcode/Clang-modules errors — use `--config=default`, which
`Taskfile.yml` already does for every `task tpch:*` target). Running every pattern above against the
real binary confirmed most of the diagnoses but turned up several things the static reading couldn't
have caught:

- **§1.1 (Pattern A) was already fixed upstream before this session.** Q1/Q3/Q4/Q6/Q7/Q15 all
  translate today *without* the `exists(...)` rewrite — a bare 0-ary def used in an inequality
  (`<`, `<=`, …) where neither operand needs the comparison to establish a new binding now
  materializes as a joined SQL source directly. This is a different, narrower mechanism than the
  plan predicted, and it does **not** generalize to equalities that must ground a fresh variable
  (see the new finding on division, below) or to arithmetic operands.
- **§1.2 is answered: single-level `sort[family]` on a multi-column curried family works fine**,
  confirmed with a minimal repro and reused in Q1/Q4/Q8/Q15/Q20/Q22 without the double-nesting
  rewrite. `RelBuiltinOrderExpr`'s translation is arity-generic; it was never actually family-size
  dependent. (An unrelated finding surfaced during that repro: a `def` body that's a disjunction of
  3+ equality branches over a *hand-defined* relation — not the OR-chain-as-filter shape used for
  Q16's fix — makes the translator produce exponentially-nested CTEs and can hang; this is the same
  family of MergeWith-adjacent issue as §1.10/§2.2, not a sort/family problem.)
- **Pattern I's suggested fix (`def dec[x]: decimal[64, 4, x]`) doesn't work** — `decimal` only
  accepts 2 params (`decimal[prec, scale]`, itself a stub that renders as `CAST(NULL AS …)` when
  never given a value). Worse: the *original* `def dec { decimal[64, 4] }` + `dec[x]` idiom that
  Q6/Q20/Q22 all relied on doesn't fail loudly — it silently returns `NULL` for every `dec[x]` call
  and was already corrupting Q6's translated SQL before this session (Q6 showed `translate: ok` in
  the stale manifest, but the WHERE clause it produced was nonsense). Since every `dec[X]` call site
  in the benchmark passes a literal, the actual fix applied was to delete the `dec` macro and inline
  the literal directly (`dec[0.5]` → `0.5`), not to make `dec` re-appliable.
- **Two newly-discovered gaps not in the original plan, with no query-level workaround found:**
  (1) `date_year[R[k]]` used as an operand of an `=` comparison — this is the exact shape of
  `tests/test_translation.cc`'s `TpchQ9*` tests, which **currently fail at HEAD** (not something
  this session broke); it blocks Q9 (previously believed to pass) and Q8. (2) Safety inference never
  grounds a variable through an equality that divides by another (non-constant) variable —
  `src/preprocessing/safety_inferrer.cc`'s `DivisorIsSafe` explicitly rejects any divisor containing
  a column reference — which blocks Q14's `100 * promo_revenue / all_revenue` and, once date_year is
  fixed, would also block Q8's `market_share[o_year]: sum[...] / sum[...]`. Both are real C++ fixes,
  not rewrites.
- **`bottom(limit, R, rank_var, sort_col_var, ...)` used with real (non-`_`) variables in the rank
  and pass-through positions is unimplemented** — `TryLowerFull`'s handler silently drops those
  params, and the safety inferrer then can't ground them. Q10 was rewritten to the same
  double-nested `reverse_sort` idiom used elsewhere instead of using `bottom`.
- **Two `scripts/tpch_rewrite.py` `PARAMS` bugs, unrelated to any pattern in this plan:** Q6 and
  Q20 each had an extra parameter spliced into the middle of their list (a leftover, unreferenced
  upper-bound date), which silently shifted every subsequent `@@N` substitution to the wrong value
  — e.g. Q6's `l_quantity < @@3` was substituting `0.06` (the discount) instead of `24` (the
  quantity), and Q20's `n_name = "@@3"` was substituting a date string instead of `"CANADA"`. Fixed
  by removing the stray entries.
- **Q16's optimizer-hang workaround (§1.10, "try first, cheap") worked**: replacing the 8-way
  `size = @@3 or size = @@4 or ...` with a small fact-table `def target_size{ (@@3); (@@4); ... }`
  referenced as `target_size(size)` avoids the hang entirely and translates in well under a second.
- **Multiple top-level `def NAME { ... }` clauses sharing a name do not union — the last one wins,
  silently.** This isn't in the plan at all. It affects Q22's `def selected_country_code[]: "@@1"` /
  `"@@2"` / … idiom (7 separate defs, only the last would have survived) and would have produced a
  *wrong answer*, not a translate error. Fixed by using the grammar's `relAbs` semicolon-list form
  in one def: `def selected_country_code{ "@@1"; "@@2"; ...; "@@7" }`. The same technique (tuple
  facts instead of repeated defs or OR-of-string-equality) fixed Q19's `container_size`.
- **Even after fixing translation, 11 of the 19 now-translating queries fail `execute_empty`** on
  genuine, pre-existing SQL-codegen bugs unrelated to any Rel-language gap: dangling/stale table
  aliases (Q2, Q7, Q21, Q22 — the same `GenerateTableAlias` mismatch class as the date_year bug),
  `not(like_match(...))` inside an aggregate filter compiling to a multi-column `NOT IN` subquery
  (Q13, Q16), `MAX(...)` emitted directly inside a `WHERE` clause instead of a subquery (Q15), an
  OR-of-equality-on-a-bound-variable lowering to a `UNION` subquery with a mismatched column alias
  (Q19), and DuckDB date/int type mismatches in generated comparisons (Q3, Q4, Q5). None of these
  are things a `.rel`-file rewrite can work around; see `manifest.json`'s `execute_empty_note` per
  query. The C++ test suite corroborates that this is pre-existing breakage, not something this
  session caused: `bazel test //tests:test_translation` has **16 failing tests at HEAD**
  (`TpchQ9*`, `BuiltinDateYearOnPartialApplication`, `ComparisonOperators1-5`,
  `ComparisonStringLiteral`, `NegativeLiteral1-3`, `FloatLiteral`, `EdgeCase1`), none of which this
  session's `.rel`/`tpch_rewrite.py` edits could have caused or can fix.
- One more optimizer robustness issue, in the same family as §1.10/§2.2 but distinct: **Q2
  segfaults the optimizer** at default optimization (translates and executes fine with `-u`). Given
  `Q2`'s `final_sort` is the widest double-nested-`reverse_sort` shape in the benchmark (8 value
  columns), this is plausibly related to but not proven identical to the MergeWith hang. Manifest
  entry uses `"unoptimized": true` as the workaround, same mechanism already used for Q16 in the
  original manifest.

---

## Round 2 (2026-09-14) — genuine rel2sql C++ fixes, not just query rewrites

Per explicit follow-up instruction, went past the "don't touch rel2sql itself" boundary from
Round 1 and fixed four of the C++ bugs identified above. All are committed
(`54907e8`, `a7c7af7`, `d807b00`, `e963ebd`); each was verified against `tests/test_translation`
(same 16 pre-existing, unrelated failures before/after every commit — see below) and
`tpch_pipeline_test` before being committed. **Net result: Q13, Q16, and Q22 now pass
`execute_empty` end to end** (translate + produce SQL DuckDB actually accepts on empty tables),
on top of the 8 that already did. 11 of 22 queries now fully clear `execute_empty`.

### Fixed and committed

1. **`Translator::Visit(RelExprAbstraction)` didn't project a binding variable consumed inside a
   builtin call.** `def f[c]: substring[c_phone[c], 1, 2]` assumed "c" would show up as a
   same-named output column of the body's translated SQL — true for a plain relation
   application, false for a builtin (the value it needs doesn't propagate the argument's
   variable name through). Fixed by reusing `MakeColumnForBindingOnExprSource` (the helper
   `VisitAggregateBindingsExpr` already uses successfully) plus a new fallback,
   `FindColumnForVariableViaBaseTable`, that traces the variable back to the base-table argument
   position it came from when it isn't already projected anywhere.
2. **`Visit(RelBuiltinSubstringExpr)` stringified its arguments into a `VerbatimTerm`** —
   opaque text with no substructure for any optimizer pass (flattening, alias renumbering,
   dangling-column rebinding) to see into, so a Column embedded in that string went stale the
   moment its source got renamed or promoted. Added `SubstringTerm` (structured `str`/`start`/
   `len` Terms), mirroring the existing `DateExtractTerm` — which carries almost the same doc
   comment already — and wired it into every pass that already handles `DateExtractTerm`.
3. **`RelExprToSqlTerm` always took `sel->columns[0]`.** `BuildFullApplSql` documents its own
   projection order as "param order then remaining base columns": for a partial application
   with a bound key (`c_phone[c]`), column 0 is the key, not the value. Fixed by taking
   `sel->columns.back()` (a no-op for the arity-1-no-params case every existing test covers).
4. **`CollectApplParams`'s `param_idx` counted only non-wildcard arguments.** `R(_, x)` bound
   "x" to column 1 (the wildcard's rightful column) instead of column 2 — every argument after a
   wildcard was silently off by however many wildcards preceded it. Fixed by incrementing
   `param_idx` unconditionally at the top of the loop instead of after the wildcard check.
5. **`Visit(RelNegation)`'s `NOT IN` subquery is `SELECT * FROM formula_source`**, relying on
   `formula_source` already being narrowed to exactly the negated formula's free variables (e.g.
   `not(o_custkey(_, c))` should expose only "c"). An unqualified `Wildcard` has no fixed column
   set — it re-expands against whatever the current FROM sources are — so when the flattener
   later inlined `formula_source` (promoting its own wider-arity base table directly into this
   FROM), the wildcard picked up the extra columns. Fixed by inhibiting flattening of
   `formula_source` specifically when its own column count doesn't match its underlying sources'
   combined arity (plain `not D(x)` where arity already matches still flattens exactly as
   before — a first, broader attempt at this fix unconditionally inhibited flattening and broke
   `NegationFormula1/2/4`'s exact-output-string tests; this narrower condition doesn't).

Fix 4 combined with fix 5 is what unblocked Q13/Q16 (both `not(like_match(...))` inside an
aggregate filter over a wide relation — like_match's pattern argument is a wildcard-adjacent
slot) and Q22 (`not(o_custkey(_, c))`); fixes 1–3 together unblocked Q22's
`substring[c_phone[c], 1, 2]` specifically.

### Found the root cause for Q3/Q4/Q5, but the fix isn't safe yet — reverted

Q3/Q4/Q5's `Binder Error: Cannot compare values of type DOUBLE and type DATE/TIMESTAMP` traces to
a **fifth** bug, in a different subsystem: a chained comparison `lower <= o_orderdate[o] < upper`
desugars into a conjunction where the second half gets handled via TermRewriter's `{expr}(_z)`
lift. The safety inferrer's `ComputeRelAbsApplicationSafety`
(`src/preprocessing/safety_inferrer.cc`) builds an `IntensionalDomain(expr)` wrapped in a
`Projection` whose index is the lifted atom's own (always-0) param position — but `expr`'s SQL
translation carries a "key" column (here, "o") ahead of the actual value column, the same
key-before-value ordering fix 3 above already had to work around for `RelExprToSqlTerm`. Index 0
lands on "o" (order key, a `DOUBLE`) instead of the date value, which is exactly the observed
type mismatch.

I patched this at the `DomainToSql`/`IntensionalDomain` boundary in `translator.cc` (narrowing to
the trailing `arity` columns before anyone indexes into it, mirroring fix 3's `.back()` logic) —
it fixed Q4's repro cleanly, but broke `TpchQ12PartialAppFlatten.RewrittenProgramHasNoDanglingBindingRefs`
(a regression-guard test from the prior "Fix TPC-H Q12 partial-app translation" commit) with a
new E104 "expression must be sourceable" failure. **Reverted rather than ship it** — Q12 already
works today and breaking it to fix Q3/Q4/Q5 would be a net loss. Also tried a second, independent
fix at `ParseLiftedPartialAppFromAtom` (only re-`Visit()` a shared AST node if it hasn't been
translated yet, since a chained comparison's desugaring can genuinely share the middle term
between both resulting `RelComparison`s) — this alone had **no effect** on the Q4 repro (so that
specific node isn't actually shared the way I'd assumed) but **on its own** was *also* enough to
break the same Q12 test, so it's reverted too. Whatever Q12 depends on here needs to be understood
before either change is safe — likely that some other call site genuinely relies on
`ParseLiftedPartialAppFromAtom` re-deriving a fresh translation of the same node in a different
context, which a blanket "translate once" cache breaks.

Net effect on this specific gap: still open, still diagnosed precisely (repro:
`def result { (o): foo(o) and lower <= ( o_orderdate[o] ) < upper }` with `lower`/`upper` as
0-ary defs and a real EDB `o_orderdate` — reproduces without any TPC-H-specific content), just not
safely fixed yet. Blocks Q3, Q4, Q5.

### Still open, not investigated further this round

- Q7, Q21: dangling table-alias references, same *symptom* as the substring bug (fix 2) but a
  *different* trigger — neither uses `substring`, so fix 2 doesn't reach them. Not yet
  root-caused.
- Q8, Q9: `date_year[...]` as a comparison operand — the `TryEmitDateYearPartialAppComparison`
  family of functions, entirely separate code from anything touched this round. Still the same
  16 pre-existing `test_translation` failures.
- Q14 (and Q8's `market_share`): division by a non-constant variable never grounds an equality
  (`DivisorIsSafe` in `safety_inferrer.cc`). Not investigated further.
- Q15: `MAX(...)` emitted directly inside a `WHERE` clause. Not investigated.
- Q19: a `UNION` subquery's column alias doesn't match the outer join's reference. Not
  investigated.
- Q2: optimizer segfault at default optimization (works with `-u`). Not investigated.

## Round 3 (2026-09-14) — found and fixed the real Q3/Q4/Q5 root cause; one bug fixed, one new one found

Round 2 diagnosed the Q3/Q4/Q5 type-mismatch symptom down to *an* `IntensionalDomain`
column-indexing bug but couldn't safely fix it (both attempted patches broke the Q12
regression-guard test). This round went one level deeper and found the actual root cause,
upstream of all of that: `RelASTBuilder::visitChainedComparison` (`src/rel_ast/rel_ast_builder.cc`)
desugars `a <= b < c` into two `RelComparison` nodes that **alias the same underlying AST
object** for the shared middle term `b` (`left = cmp->rhs;` — a shared_ptr copy, not a value
copy). `TermRewriter::Visit(RelComparison)` (`src/rewriter/term_rewriter.cc`) lifts a
partial-application-as-term operand by mutating the term **in place** — `term = std::move(id)`,
where `term` is a reference straight into the shared node's own field. Walking through the
first comparison's `rhs` (`RelParenthesisTerm -> RelExprAsTerm`) replaces that shared node's
inner term with a fresh `RelIDTerm` (e.g. `_x0`) as a side effect; the second comparison, whose
`lhs` points at that *same* object, then sees a bare `_x0` instead of the `RelExprAsTerm` it
needs, so its own lift silently no-ops (`lifted.empty()`). The result: `o_orderdate[o]`'s
existential (with its join back to `o`) only got built for the *first* half of the chain, and the
second half became a free reference to `_x0` — a variable that only exists inside the first
half's own existential scope — which is exactly why `o_orderdate` ended up unjoined
("cross-joined") in the final SQL, and why the previous round's `IntensionalDomain` patch (a
downstream *symptom* fix, narrowing to the wrong column) could never actually be correct: there
was no bug in which column `IntensionalDomain` picked, there was a missing join upstream of it.

**Fix** (`RelASTBuilder::visitChainedComparison`): instead of aliasing `cmp->rhs` for the next
comparison's `lhs`, re-run `visit(rhs_ctx)` against the same ANTLR parse context to build a
second, independent AST subtree. `RelASTBuilder` has no per-context memoization (checked), so
this is safe and cheap — it's exactly what building two separate `term`s from the same source
text would look like if the user had written `b` out twice. Committed as `ee7b332`. Verified: same
16 pre-existing `test_translation` failures before/after, `TpchQ12PartialAppFlatten` still passes,
`tpch_pipeline_test` all green.

**Concrete impact measured this round** (translated each of Q3/Q4/Q5/Q7/Q21 before/after against
a fresh copy of `benchmarks/TPCH/data/tpch_sf001.duckdb` — the fix's benefit doesn't show up
uniformly because each of these queries has at least one *other*, unrelated bug still blocking
it):
- **Q4**: `execute_empty` now genuinely passes (manifest updated) — `o_orderdate` is correctly
  joined to `o` in the generated SQL. But seeing real (non-empty) data against the SF0.01
  database exposed a **second, previously-invisible bug** (see below) that makes Q4's actual
  results wrong, ~2.6x too high. This was unreachable before because the query never produced
  syntactically valid SQL in the first place.
- **Q3, Q7**: still fail `execute_empty`, unaffected in outcome (same "fail" as before) — but the
  *shape* of the first error changed for Q7 (see below). Q3 additionally still hits the
  original, unrelated `HUGEINT`-vs-`DATE` comparison bug as its first error.
- **Q5**: still fails `execute_empty`, but its first error changed shape from a type mismatch to
  `Binder Error: Referenced table "T14" not found!`.
- **Q21**: unaffected — Q21 doesn't use a chained comparison at all, so this fix was never
  expected to reach it; still blocked by its own documented dangling-alias bug in
  `sub_query2`'s `NOT IN` subquery.

### New bug found: `exists(...)` nested in an aggregate body's formula doesn't deduplicate its own witness variable

Q4's real (SF0.01) results are `[247, 289, 303, 251, 349]` per `o_orderpriority` where the
reference SQL gives `[93, 103, 109, 102, 128]` — every bucket inflated by roughly the same
~2.6x factor. Root cause: `count[(o): ... and exists((num) | l_commitdate[o,num] <
l_receiptdate[o,num])]` should count each qualifying order `o` once, with `num` purely a
witness — but the generated SQL exposes `l_commitdate`/`l_receiptdate` joined directly on
`(o, num)` (i.e. **both** columns, not just `o`) straight into the aggregate's outer join, so an
order with N matching lineitems gets counted N times instead of once. The generic
`Translator::Visit(RelExistential)` path (`src/sql/translator.cc:3622`) *does* correctly wrap its
result in a `SELECT DISTINCT <free_variables only>` (dropping bound witnesses like `num`) — but
that path isn't the one that actually fires here. One of the special-case
lifted-partial-application patterns further up in `Visit(RelConjunction)`/`Visit(RelExistential)`
(`TryEmitLiftedPartialApp*` / `IsTermRewriterLiftedBindingConjunction`, `src/sql/translator.cc`
~1458–1547 and ~3630–3700) intercepts this shape first and inlines the comparison's base tables
directly without dropping the witness variable(s) afterward. Not yet root-caused to a specific
function — flagged here for a future session. Likely affects any TPC-H query whose aggregate body
conjoins an `exists(...)` with a bound variable beyond the ones it shares with the outer scope
(Q4 confirmed; worth checking Q21/Q22's `NOT EXISTS` shapes too, though those look structurally
different since they're negated).

### Still open after this round

Everything listed in Round 2's "still open" section remains open, plus:
- The `exists`-in-aggregate-body dedup bug above (blocks true Q4 correctness).
- Q5/Q7's `"Referenced table T14 not found"` scoping bug — a chained-comparison-bound variable
  (`lower`/`upper`) being carried as a named output column through a join tree where the alias
  it references is nested too deep to be in scope. Distinct from the fixed AST-sharing bug (this
  one is about naming/scope of an *output projection*, not about a missing join), not yet
  root-caused.
