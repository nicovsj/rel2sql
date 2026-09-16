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

### Second fix this round: `FlattenerOptimizer` flattened scalar aggregate subqueries (unblocked Q15)

Q15's `def result_s_suppkey(suppkey): revenue[suppkey] = max[revenue]` produced
`WHERE T0.A1 = (MAX(T1.A2))` — a bare aggregate call directly in a `WHERE` clause, which DuckDB
rejects (matches the original manifest note exactly). Traced this to the **optimizer**, not the
translator: `Translator::Visit(RelBuiltinAggregateExpr)` (`max[revenue]`, no `GROUP BY` since it
aggregates the whole relation) correctly produces `SELECT MAX(A2) AS A1 FROM revenue` — confirmed
by comparing against `-u` (unoptimized) output, which has the aggregate properly wrapped in a CTE.
The bug is in `FlattenerOptimizer::CanFlattenSubquery` (`src/optimizer/flattener_optimizer.cc`):
it only refused to flatten a subquery when `group_by.has_value()`, so a *scalar* (ungrouped)
aggregate subquery — one row, no `GROUP BY`, but still a real aggregate over its own independent
`FROM` — was treated as flattenable like any plain projection. Flattening merges the subquery's
`FROM` into the outer query and substitutes its column reference with the raw aggregate
expression; for a scalar aggregate this changes an independent whole-relation aggregation into a
(syntactically invalid) correlated one.

**Fix**: added `FlattenerOptimizer::HasAggregateColumn` (recursively checks a `Select`'s projected
terms for a `Function` — the AST node for aggregate calls — through `Operation`/`ParenthesisTerm`
wrapping) and refuse to flatten when it's true, mirroring the existing `group_by.has_value()`
check. Committed as `38e5bbb`.

This was a **real, exercised bug**, not just a latent gap: two existing `test_translation.cc`
tests (`ComparisonPartialAppl`, `ComparisonNotEqualPartialAppl`) had the buggy output
(`WHERE (SUM(T1.A1)) > 0`) baked into their `EXPECT_EQ` string and used
`OPT_EXPECT_EQ_NO_DUCKDB` specifically to avoid running that (invalid) SQL through DuckDB — the
per-test comment said as much ("Avoid DuckDB execution: the unoptimized SQL is correct but
verbose"). Both updated to the corrected output and switched to `OPT_EXPECT_EQ` (which does
execute), and now pass for real.

**Q15's `execute_empty` now passes** (manifest updated) but real (SF0.01) results are still wrong
— returns 0 rows instead of the reference's 1. Traced this to yet another, separate bug: in
`Translator::Visit(RelComparison)`'s `cover`/CTE-building loop (`src/sql/translator.cc` ~3352+),
the domain for the lifted `_x0` (standing for `revenue[suppkey]`) resolves through `DomainToSql`
to `revenue`'s **key** column (`suppkey`) instead of its **value** column (`total_revenue`) — the
same "`IntensionalDomain` picks the wrong column because a partial application's SQL carries a
key column ahead of the value column" issue diagnosed (but not safely fixed) for Q3/Q4/Q5 in
Round 2. Re-applying that round's verified-safe `IntensionalDomain` narrowing patch on top of this
fix *does* correct the column (`T0.A2` instead of `T0.A1`) but introduces a new, different bug: a
spurious `T0.A1 = T0.A2` self-equality condition appears in the `WHERE` clause (0 rows either
way) — something about wrapping the domain's `Sourceable` in an extra `Source`/alias layer breaks
whatever reference-equality-based deduplication normally keeps two different bound variables'
CTEs from being conflated. Reverted again rather than ship a new bug in place of an old one. Net:
the `IntensionalDomain` narrowing patch remains diagnosed-but-unsafe across two independent
attempts now (Q3/Q4/Q5 in Round 2 broke Q12; Q15 in Round 3 introduces this spurious condition) —
worth a from-scratch redesign rather than a third attempt at patching the same call site.

### Third fix this round: `CTEInliner` silently renamed a CTE's logical column on inline (unblocked Q19, and 10 of the 16 "pre-existing" `test_translation` failures)

Q19's `def ship_mode(o, l, shipmode): l_shipmode(o, l, shipmode) and (shipmode = "AIR" or
shipmode = "AIR REG")` produced `WHERE T0.A3 = T3.shipmode` where `T3` (the OR's UNION) had no
`shipmode` column at all — its actual columns were aliased `A3`. Confirmed via `-u` (unoptimized)
output that the **translator** builds this correctly: `WITH E0(o, l, shipmode) AS (...), E1(o, l,
shipmode) AS (...) ... (SELECT E0.shipmode FROM E0 WHERE E0.shipmode = 'AIR' UNION SELECT
E1.shipmode FROM E1 WHERE E1.shipmode = 'AIR REG') AS T4 ... WHERE T3.shipmode = T4.shipmode` —
both CTEs and every reference are consistently named. The bug is introduced by **`CTEInliner`**
(`src/optimizer/cte_inliner.cc`): when it inlines a CTE like `E0(o,l,shipmode)`, it builds a
`column_map` from the CTE's *logical* names (`"shipmode"`) to the underlying table's *physical*
names (`"A3"`) and uses `SourceAndColumnReplacer` to rewrite every `E0.shipmode` reference to
`new_source.A3`. That correctly fixes up references *elsewhere* in the tree — but the bare,
unaliased `SELECT E0.shipmode` inside each UNION branch gets its own *printed* column name from
whatever `Column` it wraps; once that `Column` becomes `new_source.A3`, the branch's own output
silently renames itself from `shipmode` to `A3`, and nothing had been done to keep the *outer*
`T4.shipmode` reference in sync. `SourceAndColumnReplacer` (`src/optimizer/replacers.h`) already
has the exact mechanism for this — a `replace_alias` constructor flag that, when a substitution
lands in a `TermSelectable` with no existing alias, sets that alias to the *original* (logical)
name — but both of `CTEInliner`'s call sites passed `replace_alias=false`, while the two other
call sites that use the default (`flattener_optimizer.cc`, `constant_optimizer.cc`) get it right
for free.

**Fix**: flip both `CTEInliner` call sites to `replace_alias=true`. Committed as `a8af889`.

This turned out to be **the most impactful fix of the session by far**: it resolved TPC-H Q19's
`execute_empty` (manifest updated; real SF0.01 results are still wrong — a huge negative `int128`
instead of the reference's positive decimal revenue, a separate, not-yet-investigated bug) *and*
fixed 10 of the 16 `test_translation` failures that Round 1/2 had catalogued as "pre-existing,
unrelated to this session's changes": `ComparisonOperators1-5`, `ComparisonStringLiteral`,
`NegativeLiteral1-3`, `FloatLiteral`. These weren't edge cases — they're basic comparison/literal
translation tests, and they were failing because their expected SQL (asserted via `OPT_EXPECT_EQ`,
which **does** execute against DuckDB) depended on a CTE-inlined column reference staying valid.
The `test_translation` baseline is now **6** failures, not 16:
`TpchQ9RewrittenProgramOptimized`, `TpchQ9FullExistsOptimized`, `TpchQ9ResultDefOptimized`,
`TpchQ9SumBodyOptimized`, `BuiltinDateYearOnPartialApplication`, `EdgeCase1` — all pre-existing,
all unrelated to CTE inlining (the Q9 group is the known `date_year`-in-comparison regression;
`EdgeCase1` and `BuiltinDateYearOnPartialApplication` are separate, already-catalogued gaps).

### Still open after this round

Everything in Round 2's "still open" list, minus nothing (Q15's flattener fix and Q19's CTE-alias
fix don't fully resolve either query — see their `execute_empty_note`s in manifest.json for the
specific remaining bug in each), plus:
- The `exists`-in-aggregate-body dedup bug (blocks true Q4 correctness).
- Q5/Q7's `"Referenced table T14 not found"` scoping bug (not yet root-caused).
- Q15's wrong-column-then-spurious-condition bug in `Visit(RelComparison)`'s domain/CTE
  resolution for a partial application with a bound key (not yet root-caused; the
  `IntensionalDomain` narrowing patch is diagnosed-but-unsafe, see above).
- Q19's wrong real-data result (huge negative `int128` vs. the reference's positive decimal) —
  not yet investigated at all; distinct from the now-fixed column-naming bug.

### Fourth fix this round: `SelfJoinOptimizer` crashed on a wide multi-way join (Q2's optimizer segfault)

Q2 (`double reverse_sort` over an 8-column relation) had a `"unoptimized": true` override in the
manifest since before this session, with the note "optimizer segfaults on this query's wide
(8-column) double reverse_sort family at default optimization." Reproduced under `lldb` with a
`-c dbg` build: `EXC_BAD_ACCESS` inside `BaseOptimizer::Visit(Expression&)`, called from
`SelfJoinOptimizer::Visit(Select&)`, called from `Optimizer::Visit(Select&)`'s *first*
`self_join_optimizer_.Visit(expression)` call (before `CTEInliner` even runs). Added a temporary
debug print in the FROM-sources loop and confirmed the vector genuinely contained a **null**
`shared_ptr<Source>` by the time the crashing iteration was reached — not just a `Source` with a
null `sourceable`.

Root cause: `SelfJoinOptimizer::Visit(Select& select)`
(`src/optimizer/self_join_optimizer.cc`) walked `select.from.value()->sources` with a
range-based `for` loop while recursively visiting each source's own nested subquery. For Q2's
shape, one of those recursive visits ends up mutating that *same* sources vector elsewhere in
the tree — most likely a `Select` object reachable through more than one `Source` (an aliased/
shared subtree) — reallocating its buffer. The range-based loop's cached `begin()`/`end()` then
point at freed memory; continuing iteration on those stale iterators reads that memory back as
null entries, and dereferencing one segfaults in the generic visitor's virtual dispatch. Switching
the loop to index-based (`select.from.value()->sources[idx]`, re-checking `.size()` fresh every
iteration) makes a reentrant mutation observable instead of walking off a stale iterator, and the
crash reproduced no faster than immediately — after the fix, Q2 translates and *optimizes*
cleanly with no `-u` override needed to avoid crashing. Committed as `3172236`.

**This did not flip Q2's manifest status.** With the crash gone, the *optimized* path now hits a
different, already-documented bug instead: `Binder Error: Values list "T0" does not have a column
named "A3"` — the "dangling table alias (stale `GenerateTableAlias` reference)" issue the manifest
already names for Q2. `"unoptimized": true` stays in place; the segfault specifically is fixed and
verified (full `task test` clean, same 6-test baseline, `tpch_pipeline_test` green), but Q2 needs
the same dangling-alias root-cause work as Q3/Q7/Q21 before it can drop the override.

## New tool: `ScopeValidator` (`src/optimizer/scope_validator.{h,cc}`, `c0eb0f2`)

Three of this round's four fixes (`CTEInliner`, the flattener's scalar-aggregate case, the
chained-comparison AST sharing) were the same failure shape underneath: some pass rewrites a
`Column` by string-matching against a source alias, gets it wrong, and produces AST that's
syntactically well-formed but references something no longer in scope. That's exactly what makes
this bug family expensive to trace — it surfaces (if at all) as a confusing DuckDB binder error
several passes and sometimes several queries downstream of whichever rewrite actually broke it.

`ScopeValidator` is a `Optimizer::Optimize()`-time invariant check (runs once, right after
`RebindDanglingSelectColumns`, so after the whole pipeline including alias renumbering) that walks
every `Column` reference and verifies: (1) its source alias is a `FROM` source or visible CTE in
scope at that point in the tree — CTE visibility accumulates down through nested subqueries the
way `WITH` scoping actually works, table-alias visibility doesn't; (2) where the source is
transparent enough to introspect (a plain `Select`, or anything with explicit `def_columns` like a
CTE), the column *name* is genuinely one of that source's exposed columns. Deliberately permissive
past that: a `Table` with unknown attribute names, a `Union`, anything with a wildcard column —
skip the name check rather than risk a false positive. Throws `TranslationException`
(`ErrorCode::DANGLING_COLUMN_REFERENCE`, E902) naming the exact column and alias.

Verified zero false positives across the full `test_translation` suite and all 22 TPC-H queries.
It immediately caught the real, pre-existing Q3/Q5/Q7/Q21 dangling-alias bug — previously silent
through `translate` and only surfacing as a DuckDB error at `execute_empty` — now a precise E902 at
`translate` time. Manifest updated to match (`translate: fail`, `stderr_contains: "E902"`); this
doesn't fix that bug, it just catches it earlier and names it precisely, which is the point: the
next time a rewrite introduces this shape of bug, it should fail loudly and locally instead of
costing another multi-hour trace like Q5/Q7's "Referenced table T14 not found" did this round.

## Round 4 (2026-09-14) — root-caused Q3's dangling alias; found a foundational value-column bug affecting nearly every query

Used `ScopeValidator`'s precise error (`column 'T117.orderdate' references an alias not visible
here`) to root-cause Q3 directly instead of tracing DuckDB errors by hand. Built a minimal repro
(`o_orderdate(ok,orderdate) and o_orderdate[ok] < target_date and revenue = sum[...]`) and found
two independent bugs, both committed as `2b7a56b`:

**Bug 1 — `FindAggregateThresholdPattern` misdetection.** This function (used by
`Visit(RelFormulaAbstraction)` to recognize a `value = agg[...] and value CMP idb`-shaped
formula, e.g. for `count[shipmode] > 5`-style patterns) walked the formula tree in a single pass,
inferring `value_var` from whichever `var CMP idb` comparison it saw *first* — with no check that
`var` had anything to do with the aggregate export equality found elsewhere. For Q3,
`o_orderdate[ok] < target_date` (a plain date filter, unrelated to the revenue aggregate) got
misdetected as the pattern's threshold comparison, purely because `target_date` (a 0-ary
`@inline def`) is also classified as an IDB. This corrupted the whole query: `revenue` ended up
compared directly against `target_date` (`WHERE T4.revenue < T5.A1` in the generated SQL), and
the real `o_orderdate`/`o_shippriority` join was dropped from the query entirely — which is
exactly what `ScopeValidator` caught (a `SELECT` referencing a table never joined in). Fixed by
splitting into two passes: find the aggregate's real `value_var` first, then only accept a
threshold comparison that references that specific variable.

**Bug 2 — lifted-atom params bind to the wrong (key, not value) column.** `Visit(RelFullApplication)`
assumed a `RelFullApplication`'s params align 1:1 with its base's own columns starting at
position 1. True for a direct relation reference (`o_orderdate(ok, orderdate)`), false when the
base is a TermRewriter-lifted `{inner}(z)` wrapping a partial application — e.g.
`{l_extendedprice[o,num]}(_x1)`, which is exactly what `l_extendedprice[o,num] * (1 -
l_discount[o,num])` desugars to. The wrapped partial application's own translation carries its
"key" columns (o, num) ahead of its value column (`BuildFullApplSql`'s own documented "param
order then remaining base columns" convention) — so the single param `_x1` was silently binding
to column 1 (`o`, the order key) instead of the value. **This is the root cause of every TPC-H
query computing `l_revenue`/`l_charge` (`l_extendedprice[o,num] * (1 - l_discount[o,num])`,
TPC-H's single most common expression) producing garbage** — confirmed present in already-"passing"
queries too (Q1, Q10, ...), just never caught because `execute_empty` only checks for crashes, not
correctness. `DomainToSql`'s `IntensionalDomain` case had the identical bug for the same
underlying reason, on a separate code path (an arithmetic term like `_x1 * (1 - _x2)` resolves
each operand's own domain through here). Both fixed: `Visit(RelFullApplication)` now offsets
every param's base-column index to the trailing columns when the base is wider than the params
supplied (`CollectApplParams` gained an `index_offset` parameter), and `IntensionalDomain`
narrows to its trailing `arity` columns the same way. **This is the same narrowing fix that was
investigated and reverted in Round 2/3 as "not needed for Q4/Q15's specific repros"** — turns out
it's needed for this far more common shape; Round 2/3 just hadn't hit a repro that required it.

Verified extensively given the blast radius (touches `Visit(RelFullApplication)`, used by nearly
every query): full `task test` clean (same 6 pre-existing `test_translation` failures),
`tpch_pipeline_test` shows only Q3 changing status, zero regressions across all 22 queries'
execute_empty/translate manifest status. Used `git stash` to confirm results against real SF0.01
data are not regressions: `Q6`'s revenue now matches the reference *exactly* (`1193053.2253`),
`Q12`'s one produced row matches the reference row exactly, `Q3`'s top result (orderkey 450,
revenue `205447.4232`) exactly matches the reference, and `Q10` changed from nonsensical negative
`int128` garbage to a correctly-shaped positive decimal (confirmed via `git stash` to the
pre-session baseline that the garbage predates this round, not a regression it introduced).

**Still open**: Q3 itself is not fully correct yet — several higher-revenue orders present in the
reference are missing from our top-10 (not yet root-caused, likely a filtering/join-completeness
gap independent of the two bugs above). Q10 similarly still doesn't match the reference despite
the value-column fix resolving its revenue computation. Given the scope of what Bug 2 touches,
every other query in the "still open" lists above (Q4, Q15, Q19's remaining correctness gaps,
Q8/Q9/Q14's translate failures, Q5/Q7/Q21/Q2's dangling-alias family) should be re-examined in a
future round now that this foundational bug is fixed — some may turn out to have been entirely
explained by it.

## Round 5 (2026-09-14) — Q9's date_year regression, root cause and fix

Picked the highest-leverage remaining item: `date_year`, responsible for 4 of the 6 remaining
`test_translation` failures (`TpchQ9*`) plus `BuiltinDateYearOnPartialApplication`, and for Q9's
`translate: fail`. Root-caused with an `lldb` **C++ exception breakpoint**
(`break set -E C++`) rather than a function-entry breakpoint on `ExpectSourceable` — the latter is
misleading here since `ExpectSourceable` is called constantly throughout translation and a plain
breakpoint just stops on the *first* call, not the one that throws.

**Bug**: `TryEmitDateYearExistential` (the special case that fires for `exists(z |
{date_year[R[k]]}(z) and year_var = z)`, i.e. exactly what `date_year[...]` used as a comparison
operand desugars to after `TermRewriter` lifts it) built its result correctly as a `Select`, then
wrapped that `Select` in a `sql::ast::Source` — a FROM-clause alias binding, not itself a
`Sourceable` — before assigning it to `node->sql_expression`. Every other `RelFormula`
translation assigns a `Sourceable` there; this one didn't. The bug stayed invisible as long as
nothing downstream called `ExpectSourceable` on this particular existential's result — which is
exactly what happens once it's embedded in a larger conjunction (Q9's whole shape: `date_year[...]
and exists(...)`), where the generic `Visit(RelConjunction)` path does call `ExpectSourceable` on
both sides.

**Fix**: don't wrap — `inner_srcable` (the already-correct `Select`) *is* the `Sourceable` this
function needs to return. One-line change, `2f045d9`.

**Impact**: `test_translation`'s regression baseline drops from 6 to 1 (only `EdgeCase1` remains,
unrelated). Q9 now translates, executes, and was verified against real SF0.01 data — ALGERIA's 7
rows (1992-1998) match the reference exactly. Q8 still fails `translate`, but now for exactly the
reason its own manifest note already predicted it would once Q9's blocker cleared: Q14's
division-by-a-non-constant-variable safety-inference gap (`E102` instead of `E104` now — confirms
the prediction was correct, not a new problem). Manifest updated accordingly.

**Still open**: `EdgeCase1` (the one remaining `test_translation` failure, not yet investigated
this round), Q14's division-by-variable gap (which now also blocks Q8), and everything else in the
"still open" lists above.

## Round 6 (2026-09-15) — Q5/Q7's dangling-alias root cause, and a false lead on Q11

Picked up the dangling-alias family (Q2/Q5/Q7/Q21) next. Q5 and Q7 share a symptom: ScopeValidator
throws on a column referencing an alias like `T14.A1` that's genuinely out of scope. Root-caused
with the scratchpad-minimization technique — `q5repro3.rel` isolates the trigger to a
chained-comparison over 0-ary `@inline` relations:

```
def lower { parse_date["1994-01-01", "Y-m-d"] }
def upper { date_add[parse_date["1994-01-01", "Y-m-d"], ^Year[1]] }
def result { sum[[o]: l_extendedprice[o, 1] where lower <= ( o_orderdate[o] ) < upper] }
```

**Bug**: `ComputeAggregateGroupKeys`/the `extra_ids` loop in `VisitAggregateBindingsExpr` (both
built on `CollectRelIdTermNames`, a blind tree walk collecting every bare identifier) never
distinguished a genuine free variable from a bare reference to a 0-ary relation like `lower`/
`upper` used directly as a comparison operand — both parse identically as a `RelIDTerm`. Treating
`lower`/`upper` as group keys / binding columns pulled their own nested translation's alias into
the caller's column list, where it's out of scope.

**First fix attempt (reverted)**: exclude any id where `ctx.IsRelation(id)` is true. This fixed
Q5/Q7 but broke `TpchQ11AggregateThreshold.RewrittenResultGroupsByPart` — Q11's aggregate is
grouped by `part`, a variable name that collides with the `part` EDB relation, so the blanket
filter wrongly excluded it too (missing `GROUP BY`).

**Second attempt (also reverted)**: only exclude when `ctx.IsRelation(id)` *and* `id` isn't in
`expr->free_variables` (computed by `VariablesVisitor`, in principle the scope-correct source of
truth). Reasoned this should let `part` through since it's genuinely free at that scope. It didn't
— Q11 regressed *worse*, into a hard ScopeValidator throw (`T198.part` not visible) instead of the
original missing-`GROUP BY` assertion failure. Added `fprintf`-based debug tracing directly inside
`ComputeAggregateGroupKeys` to settle the contradiction between the reasoning and the empirical
result: `expr->free_variables` printed as **empty** for the exact sub-expression scope in question.
`free_variables` is not populated reliably for every nested sub-expression node — it's scope-correct
where it *is* populated, but not a safe existence check on its own.

**Actual fix**: stopped relying on `free_variables` for this distinction entirely. Added
`CollectApplicationArgIds`, a tree walk that collects every id appearing as an actual argument to
an application (`RelFullApplication` or `RelPartialApplication`'s `params`) anywhere in the
expression — e.g. `part`/`supplier` in `ps_supplycost[part, supplier]`. An id used that way is
necessarily a real variable, regardless of name collision with a relation. An id that matches a
relation name but was *never* used as an application argument (e.g. `lower`/`upper`, always bare
comparison operands, never `lower[...]`) is a bare relation reference, not a variable. This
required handling both `RelFullApplication` (formula-level atoms) and `RelPartialApplication`
(the value-producing form used inside arithmetic, e.g. Q11's `ps_supplycost[part,supplier] *
ps_availqty[part,supplier]`) — missing the latter was why the first debug pass showed `arg_ids={}`
even for Q11's genuinely-argument-position `part`.

**Impact**: `task test` clean (only the pre-existing unrelated `EdgeCase1` failure remains); Q11
still passes. Q5 and Q7 both now translate successfully — `tpch_pipeline_test` no longer expects
`E902` for either. Verified against real SF0.01 data: Q5 matches the reference exactly (all 5
nations). Q7 translates and its `execute_empty` stage passes, but running it against real data
hangs — a **separate, pre-existing** bug: the generated `shipment_volume` subquery has a
tautological self-join (`T4.num = T4.num AND T4.o = T4.o`) where a real join to the
date_year-lifted `l_shipdate` table should be, plus an orphaned unused `l_revenue` table, causing a
cartesian product against non-empty data. Traced (not yet fixed) to
`ParseDateYearPartialAppExtract`'s `appl_params.size() != 1` restriction: Q9's
`date_year[o_orderdate[ok]]` supplies one param and hits the working single-key special case;
Q7's `date_year[l_shipdate[o,num]]` supplies two (arity-3 `l_shipdate`), falls through to a
different, buggy generic path. Committed as `bffeb06`.

**Still open**: Q7's tautological-join bug (separate from the dangling-alias fix above), Q2 and
Q21's dangling-alias errors (`T114.part`, `T118.o1` — not yet investigated, may or may not share
Q5/Q7's root cause), `EdgeCase1`, Q14's division-by-variable gap, and everything else in the "still
open" lists above.

## Round 7 (2026-09-15) — Q7's tautological-join bug, root cause and fix; Q21 fixed as a side effect

Also hit a real environment problem this round, unrelated to rel2sql itself: macOS upgraded to
27.0 mid-session, which shipped a new `MacOSX27.0.sdk` whose `.tbd` stub files use an architecture
spec (`arm64e.x1`) the currently installed linker doesn't understand, plus a too-low default
`-mmacosx-version-min` for some C++23 stdlib availability annotations. Fixed by pinning
`-mmacosx-version-min=14.0` and the link-step `-isysroot` to the already-working `MacOSX14.5.sdk`
in `~/.bazelrc` (machine-local, not the repo — `.bazelrc`'s existing `build:macos` config already
pins the *compile*-step SDK correctly; only the link step needed the same treatment). Safe to
remove once Xcode Command Line Tools / Homebrew LLVM are back in sync with the OS SDK.

Picked up Q7's tautological self-join, flagged but not root-caused at the end of Round 6.
Minimized with the scratchpad technique down to:

```
def result { sum[[o, num, y]:
    l_extendedprice[o, num] where
    y = date_year[l_shipdate[o, num]]
]
}
```

which reproduced both symptoms: `WHERE T0.A1 = T0.A1 AND T0.A2 = T0.A2` (comparing the
`l_extendedprice` atom to itself) and an orphaned, unconstrained second `l_extendedprice` source.

**Bug 1 (the tautology)**: `date_year[l_shipdate[o,num]]`'s translation runs through
`ExtractScalarSqlTerm` (shared by every builtin call on a partial application — `date_year`,
`substring`, decimal casts, arithmetic, ...), which picks out `l_shipdate[o,num]`'s own translated
select's *last* column (the shipdate value) as the scalar term, and discards the preceding
columns — the `o`/`num` key columns that named the variables the value came from. `date_year`'s
own translated SELECT ends up with a single column (the extracted year), never exposing `o`/`num`
by name. When the outer `l_extendedprice[o,num] where y=date_year[...]` conjunction
(`Visit(RelCondition)`) then tries to join its two sides on every shared free variable
(`EqualityShorthandRel`), it can't find `o`/`num` as real columns on the `date_year` side, so
`ResolveOutputColumnNameForVariableOnSource` falls back to guessing the bare variable name — a
column reference (`rhs_source.o`) that doesn't actually exist. That dangling reference isn't
caught by ScopeValidator; a later optimizer pass (the dangling-column rebinder) silently "fixes"
it by pointing it at whichever *other* in-scope column happens to share that name — which is
`l_extendedprice`'s own `o` column, i.e. exactly the same source on both sides of the equality.
Confirmed via targeted `fprintf` tracing directly in `EqualityShorthandRel`.

**Bug 2 (the orphan)**: `Visit(RelComparison)`'s generic path builds a "domain grounding" CTE
source for *every* bound in `node->safety.SmallCover()`, without checking whether that bound's
variables even intersect the comparison node's own `free_variables`. `SmallCover()` can return
bounds covering the broader safety context, not just this node — the `y = _x0` comparison
TermRewriter leaves behind after lifting `date_year[...]` still carries a SmallCover bound for
`{o,num}` via the sibling atom that grounds them elsewhere in the same conjunction, even though
`y = _x0` itself has nothing to do with `o`/`num`. That bound got a domain source unconditionally,
which — having no free-variable overlap to attach a join condition to — sat in the FROM clause
fully unconstrained. Confirmed via `typeid`-tagged tracing across all five `DomainToSql(bound.domain)`
call sites in the file to find which one fired.

**Fix**:
1. `ExtractScalarSqlTerm` now also returns the discarded key columns (`ScalarSqlTerm::extra_columns`);
   `RelBuiltinDateExpr`'s `ExtractYear` branch projects them alongside its own value column,
   following the existing "key columns first, value last" convention (the same one
   `BuildFullApplSql`/`ExtractScalarSqlTerm` itself already rely on), so a variable that produced a
   builtin's value stays exposed as a real output column instead of silently dropped.
2. That alone wasn't sufficient — an intermediate merge step (the TermRewriter lift-application
   wrapper, or the conjunction merge) can still fail to carry the name the rest of the way up. Added
   `ProjectMissingFreeVariables`, called from `Visit(RelCondition)` and (scoped to a
   `RelConjunction` formula) `Visit(RelExistential)`'s generic fallback: for each free variable not
   already a named output column, recover it by tracing back to the base table that actually
   produced it (`FindColumnForVariableViaBaseTable`, pre-existing, previously only used in
   `Visit(RelExprAbstraction)`).
   - First attempt applied this unconditionally in `Visit(RelExistential)` and regressed
     `TranslationTest.NestedQuantifiers2` (a `forall` inside an `exists`): `FindColumnForVariableViaBaseTable`
     recurses into arbitrarily nested subqueries, and a `RelUniversal`'s own translation
     deliberately keeps two copies of its inner select in *separate*, non-mergeable subqueries
     (`subquery_outer`/`subquery_inner`, one marked `inhibit_subquery_flatten` — see Round 2's
     `NestedQuantifiers3`-adjacent fix) specifically so the flattener can't merge them. Recovering a
     column from inside that nesting and exposing it at the outer level produced a *new* dangling
     reference (`Table alias 'T1' is referenced but does not exist`), since that subquery never
     gets flattened away by design.
   - Added an `inhibit_subquery_flatten` check to `FindColumnForVariableViaBaseTable` itself (don't
     search into a source marked that way) — necessary but not sufficient, since `subquery_outer`
     itself isn't marked and the regression persisted.
   - Root cause of *why* it's safe for Q7 but not for the Universal case: in Q7's chain, every
     intermediate wrapping select is used exactly once and reliably gets flattened away by
     `FlattenerOptimizer`, making the recovered reference valid in the final SQL. The Universal's
     `subquery_outer` wraps the *same* underlying select object that `subquery_inner` also wraps
     (deliberately, to compare them) — that reuse is precisely what blocks flattening, and there's
     no cheap local signal for "is this select referenced from multiple places" at the point
     `ProjectMissingFreeVariables` runs. Rather than trying to detect that generally, scoped the
     `Visit(RelExistential)` call to fire only when `node->formula` is a `RelConjunction` — exactly
     the TermRewriter-lifted `{inner}(z) and y=z` shape this targets, never a `RelUniversal`.
3. `Visit(RelComparison)`'s domain-grounding loop now skips a `SmallCover` bound whose variables
   don't intersect the comparison's own `free_variables`.

**Impact**: `task test` clean (only the pre-existing unrelated `EdgeCase1` failure remains,
confirmed via `git stash` at the start of this round). Verified by hand against real SF0.01 data:
Q7 now matches the reference exactly (all 4 rows) and returns instantly — it previously hung
indefinitely (killed after 90s) once the dangling-alias fix from Round 6 let it reach execution.
Q21 — flagged since Round 3 as sharing Q5/Q7's dangling-alias symptom class but never
root-caused — turned out to hit this exact mechanism and is now fixed as a side effect; verified
against real data too (Supplier#000000074, numwait 9, exact match). `tpch_pipeline_test` updated
and passing; manifest updated for both Q7 and Q21.

**Still open**: Q2's dangling-alias error (`T114.part`) — checked after this round's fixes and
confirmed unchanged (same error, not touched by this mechanism, so likely a different root cause),
`EdgeCase1`, Q14's division-by-variable gap, and everything else in the "still open" lists above.

## Round 8 (2026-09-15) — Q8/Q14's division-by-variable gap, root cause and fix

Picked the highest-leverage remaining `translate: fail`: Q14's `DivisorIsSafe` gap, which the
manifest already flagged as also blocking Q8 once reached.

**Q14**: `def result[]: 100 * promo_revenue / all_revenue`, where `promo_revenue` and
`all_revenue` are both `@inline def`s (0-ary, scalar `sum[...]` results). `DivisorIsSafe`
rejected any divisor whose term tree contained a `RelIDTerm` node at all — but `VariablesVisitor`
only populates a `RelIDTerm`'s `variables` set for an actual row-level variable
(`VariablesVisitor::Visit(RelIDTerm)` checks `builder_->IsVar(id)` first); a bare identifier that
resolves to a relation instead gets an empty `variables` set. `DivisorIsSafe`'s dynamic-type check
couldn't tell "genuine unbound column variable" from "bare 0-ary relation reference" apart — the
exact same class of bug fixed for Q5/Q7 in Round 6/7 (`lower`/`upper` mistaken for free
variables), just in the safety inferrer instead of the translator. Fixed by checking
`id->variables.empty()` instead of the node's dynamic type, and giving `TermToDomain` a new case:
a bare 0-ary relation reference is its own domain (`DefinedDomain(id, arity)`) — a deterministic
value computed once, not per row.

**Q8**: same manifest-predicted symptom, but `market_share[o_year]`'s divisor
(`sum[lineitem_vol_all[o_year]]`) is a full aggregate *expression*, not a named 0-ary relation —
Q14's fix didn't touch it (confirmed: Q14 fixed, Q8 still threw the identical `E102` on the exact
same witness-variable shape). TermRewriter lifts the aggregate into its own witness variable
(e.g. `_x17`), grounded via the pre-existing `ComputeRelAbsApplicationSafety`
(`IntensionalDomain` wrapped in a `Projection`) — a completely different, already-working
mechanism from the Q14 case. `DivisorIsSafe` still rejected it purely because the divisor term
*is* now a genuine bound variable (`id->variables = {"_x17"}`).

First attempt: drop the `DivisorIsSafe` gate entirely, matching how ADD/SUB/MUL already work (no
separate pre-check beyond "can `TermToDomain` build a domain for this operand at all"). This fixed
both Q8 and Q14, but broke `SafetyTest.CompositionalDivByVariableFails` — a test *deliberately*
asserting that `R(x) and S(y) and x/y=z` must **not** ground `z` when `y` ranges over a whole
relation `S(y)`. That's a real, distinct concern from Q8's case: `y` here is a genuine multi-row
EDB column (many possible values simultaneously), so `z = x/y` doesn't have one determined value
the way Q8's `_x17` does (fixed once the aggregate's own group-by inputs are fixed). Blindly
dropping the gate can't tell these apart.

**Actual fix**: kept `DivisorIsSafe`, made it context-aware (`safety`, `RelContextBuilder`) instead
of a pure syntactic check, and gave it three safe cases: a compile-time constant, a bare 0-ary
relation reference, or a variable whose *bound domain* is itself "functionally determined"
(`DomainIsFunctionallyDetermined`: peels `Projection` wrapping to check for an `IntensionalDomain`
at the base — precisely the shape `ComputeRelAbsApplicationSafety` produces for an
aggregate-application witness, and nothing else produces). A variable bound to a raw
`DefinedDomain` (an EDB/IDB table column, `ComputeIDApplicationSafety`'s shape for a plain atom
like `S(y)`) still fails — preserving the test's original guarantee exactly.

**Impact**: `task test` clean (only the pre-existing unrelated `EdgeCase1` failure remains). Q14
and Q8 both now translate, execute, and were verified by hand against real SF0.01 data: Q14
matches exactly (`promo_revenue` 15.48654581228407); Q8 matches exactly too (1995: 0.0, 1996: 0.0
— SF0.01 genuinely has no BRAZIL line items in either year). Manifest updated for both.

**Still open**: Q2's dangling-alias error (`T114.part`, unchanged, not investigated), `EdgeCase1`,
and everything else in the "still open" lists above.

## Round 9 (2026-09-15) — Q2's dangling-alias root cause, and a foundational name-collision bug

Picked up Q2 — the last remaining `translate`/`execute_empty` failure. Minimized with the
scratchpad technique (working around several raw-grammar surprises along the way: the CLI binary
parses only the raw ANTLR grammar, `def NAME { ... }` braces required, no `NAME(params): body`
sugar and no `.field` chaining — both are `scripts/tpch_rewrite.py`-level rewrites applied before
the file ever reaches `rel2sql_bin`) down to:

```
def mycost { (part, supplier, suppcost):
    ps_supplycost(part, supplier, suppcost) and
    ( r_name[n_regionkey[s_nationkey[supplier]]] ) = "EUROPE"
}
def result { (v1): exists((s in supplier, p in part) |
        v1 = ps_supplycost[p, s] and
        ps_supplycost[p, s] = min[mycost[p]]
    ) }
```

— which reproduced `T16.part`/`T16.supplier` referencing an alias absent from `mycost`'s own
rendered FROM clause. Root-caused with `ScopeValidator` temporarily disabled (to inspect the raw
SQL) plus targeted `fprintf` tracing across several layers (`Visit(RelFormulaAbstraction)`,
`FlattenerOptimizer::TryFlattenSubquery`/`CanFlattenSubquery`, `BuildTermMap`) to rule out AST-node
sharing between `mycost`'s own definition and its reuse inside `min[mycost[p]]` (confirmed via
pointer tracing that `Visit(RelFormulaAbstraction)` runs exactly once per def — that hypothesis was
wrong) before finding the real cause one layer deeper.

**Bug**: `RelContextBuilder::AddVar` — `if (idb_.count(var) || edb_.count(var)) return;` — silently
refuses to register a variable name that's already a known relation. `part`/`supplier` are both
TPC-H EDB relations *and* `mycost`'s own binding-variable names; since `AddVar("part")` no-ops,
`IsVar("part")` returns false everywhere in the program, so `VariablesVisitor::Visit(RelIDTerm)`
never marks it free — the conjunction body's own translated SELECT never projects "part"/"supplier"
as columns at all (confirmed by tracing `BuildTermMap`'s output for the flattened subquery: only
`{suppcost}`, not `{part, supplier, suppcost}`). `Visit(RelFormulaAbstraction)`'s binding-column
construction (`Column(vb->id, formula_source)`) blindly assumed every binding was projected by
name and never checked — so once the wrapping subquery got flattened away by the optimizer
(hoisting its sources up, replacing references via a term-name lookup that simply has no entry for
"part"), the reference was left pointing at the now-discarded subquery's alias. This is the same
*class* of bug — a bound variable's name colliding with a relation name, causing something
downstream to wrongly treat it as "not a real variable" — that Q5/Q7 (Round 6, `lower`/`upper`) and
Q11 (Round 6, `part` in `ComputeAggregateGroupKeys`) hit; this is its root, in `RelContextBuilder`
itself, rather than a downstream translator symptom. Fixing `AddVar` at the source was considered
but rejected as too high-risk/wide-blast-radius for this session (the "var vs relation" ambiguity
is relied on elsewhere in ways not fully audited); fixed at the same layer as the other instances
instead.

**Fix**: ported `Visit(RelExprAbstraction)`'s existing recovery pattern —
`FindColumnForVariableViaBaseTable`, tracing which base-table argument position a binding was
actually bound to, independent of whether `VariablesVisitor` marked it free — into
`Visit(RelFormulaAbstraction)`, which lacked it. Scoped to a `RelConjunction` formula, exactly
matching Round 7's `Visit(RelExistential)` fix and for the identical reason: unscoped, it also
tried to recover through a `RelUniversal`'s own translation, which deliberately wraps its inner
select in two non-mergeable `Source` copies — reaching into that nesting produced a *new* dangling
reference (regressed `TranslationTest.WeirdEdgeCase1`'s `tfa` case) since that subquery is never
flattened away by design.

**Impact**: `task test` clean (only `EdgeCase1` remains). Q2 now translates and executes fully
*optimized* — the `unoptimized: true` workaround (originally added for an unrelated
`SelfJoinOptimizer` segfault, fixed earlier this session) is no longer needed and was removed from
the manifest. Verified against real SF0.01 data: **not yet correct** — only 2 of the reference's 4
rows come back (partkeys 249 and 1015 match exactly; 1634 and 323 are missing). This is a
newly-reachable, separate bug (Q2 never got this far before), likely in the
`min[supplycost[p]]`/`reverse_sort` ranking logic — flagged in the manifest, not investigated this
round.

**Still open**: the newly-found Q2 real-data ranking gap, `EdgeCase1`, and everything else in the
"still open" lists above. All 22 queries now translate and execute without error for the first time
this session.

## Round 10 (2026-09-16) — the name-collision root cause, fixed lexically; Q11 and Q2 correct

**Symptom**: Q11 returned 1997 rows (nearly every part) instead of the reference's single row, with
implausible values; the `> threshold` filter constrained nothing. Q2 returned only 2 of the
reference's 4 rows (Round 9's newly-found gap).

**Root cause**: the collision documented in Round 9 — `RelContextBuilder::AddVar` silently refuses
to register a variable whose name is already a known relation — but tracked to where it actually
does the damage. In Q11, `part` and `supplier` are bound variables (`def result {(part, v): ...}`,
`sum[[supplier]: ...]`) whose names are also EDBs. Because `AddVar` dropped them, `IsVar` was false
for them everywhere, and two things followed. First, `VariablesVisitor` never marked them free, so
every mechanism keyed on `free_variables` skipped them. Second, and the actual source of the wrong
numbers, `CollectApplParams` classifies an argument as a *relation* argument when
`context_.IsRelation(id)` holds: `ps_supplycost[part, supplier]` was therefore translated as a
domain join against the full `part` and `supplier` tables rather than as two bound variables. The
argument's own value was never projected under its name, so the nation filter
(`n_name[s_nationkey[supplier]] = "GERMANY"`) could not be correlated with the supplier being
aggregated — it degenerated into an uncorrelated existence check, and the sum ran over every
part/supplier pair.

Removing `AddVar`'s guard outright — the "fix it at the source" option Round 9 considered and
rejected — was tried and measured: it breaks 19 of the 22 queries plus `test_rel_ast` and
`test_translation`. The ambiguity is real and the guard is load-bearing; a bare `B(A, x)` genuinely
does mean the relation `A` (`TranslationTest.FullApplication8`).

**Fix**: disambiguate *lexically* instead of globally. A new preprocessing pass
(`src/preprocessing/binding_shadow_marker.{h,cc}`) walks the AST carrying a scope stack of names
bound by enclosing binders (the four node types with `bindings`: `RelExprAbstraction`,
`RelFormulaAbstraction`, `RelExistential`, `RelUniversal`, counted so nested rebinding is handled)
and sets `RelIDTerm::shadows_relation` on any id that is both a known relation and bound in scope.
Two consumers read it: `VariablesVisitor::Visit(RelIDTerm)` now marks such an id free, and
`CollectApplParams` routes it to the term slots rather than the relation slots. Everything
downstream that already keys on `free_variables` then works unchanged. `B(A, x)` is untouched
because `A` is bound by nothing.

**Impact**: full suite green. Verified against real SF0.01 data, **Q11 and Q2 now match the
reference exactly** — Q11 at 1 row (partkey 1376, value 13271249.89), Q2 at all 4 rows. 17 of 22
queries now match exactly, up from 15; no query regressed. Q11's threshold fraction is the one
scale-factor-dependent TPC-H parameter (spec: `0.0001/SF`), and `scripts/tpch_rewrite.py` had the
SF 1 value while the data and `benchmarks/TPCH/sql/q11.sql` are SF 0.01; it now uses `0.01`, so the
comparison is apples-to-apples.

Also fixed: `TranslationTest.EdgeCase1`, red since the commit that introduced its expectation
(`170ebb4`). The expectation was simply never correct — the translator's output there is stable
across every commit since and is semantically right (same result set, verified in DuckDB; it just
lacks a redundant fourth copy of `B` joined on a tautology). The expectation was corrected.

**Still open**: Q3, Q4, Q10, Q12, Q13 real-data mismatches (all pre-existing, none related to this
round's root cause). Separately, the translation of a term like `z = x-y` still materialises the
term by cross-joining extra copies of the source relation rather than projecting the expression
directly — harmless for correctness (the witness always exists) but needless work, visible in
`EdgeCase1`'s expected SQL.

## Round 11 (2026-09-16) — Q12's dropped union branch: duplicate defs were parsed and discarded

**Symptom**: Q12 returned only the MAIL row (exactly right at 64/86) and no SHIP row — 1 row where
the reference has 2.

**Root cause**: Q12 expresses a two-element set as two defs of the same name, deliberately, because
the benchmark's Rel sources avoid brace-unions (see the comment in `queries/12.rel`):

```
@inline def selected_shipmode[]: "@@1"
@inline def selected_shipmode[]: "@@2"
```

`ArityVisitor::Visit(RelProgram)` detected the duplicate, pushed the second body onto
`RelDef::multiple_defs`, and disabled that def. But nothing ever read `multiple_defs` — the field
was written in exactly one place and read in none, so the second alternative was parsed, stored,
and silently discarded. `selected_shipmode` translated to `SELECT DISTINCT 'MAIL' AS A1`, and every
SHIP lineitem was filtered out. The MAIL row matching the reference exactly is what made this look
like a subtle aggregation bug rather than a whole missing branch.

**Fix**: fold the duplicate into the first def instead of stashing it — `def X {a}` followed by
`def X {b}` means the same thing as `def X {a; b}`, so the second body's alternatives are moved
into the first def's `RelUnion` and everything downstream (arity, variable analysis, the
literal-union translation) handles it as the one union it always was. The alternatives are *moved*,
not copied, because every other pass walks all defs including disabled ones and would otherwise
process the same nodes twice; for the same reason the fold is skipped for a def already disabled,
since `ArityVisitor` runs twice per pipeline. The dead `multiple_defs` field was removed.

**Impact**: full suite green. Q12 now matches the reference exactly (MAIL 64/86, SHIP 61/96). 18 of
22 queries match exactly on real SF0.01 data, up from 17; no query regressed.

**Still open**: Q3, Q4, Q10, Q13 real-data mismatches.
