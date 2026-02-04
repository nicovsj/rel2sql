# Translation of terms

## In parameters

To translate Rel expressions like

```
  A(x+1)
```

into SQL, we need to bound the free variable `x` wrt the relation `A`. This is rather straightforward in the example as we can just use the root of the polynomial associated to the term `x+1`.

```
  SELECT T0.A1 - 1 AS X
  FROM A AS T0
```

For simplicity we will restrict the terms in parameters to be **non-null linear polynomials in at most one variable**. This will guarantee that a single root exists.

So in general for a non-null linear term `t(x)` whose affine summary is `a x + b`, the expression

```
  A(t(x))
```

will be translated into

```
  SELECT (T0.A1 - b)/a AS X
  FROM A AS T0
```

where `b` is the root of the polynomial associated to the term.

We cannot discriminate perfectly between linear and non-linear terms without exponential worst case complexity, but we can at least guarantee that our restriction is sound with a single pass over the AST.

For this, consider that every numerical term `t` is assigned (when possible) an **affine summary**
```
  t(x) ≡ a x + b
```
for some coefficients `(a,b) ∈ R^2` with respect to a *single* variable `x`. The computation is purely syntactic and happens by a bottom‑up pass on the term AST:

- **Leaves**
  - **Constant** `c` (numerical literal): we set \((a,b) = (0, c)\).
  - **Identifier** `x` (and only one variable): we set \((a,b) = (1, 0)\). If the term contains zero or more than one distinct variable, we mark it as **invalid** for this analysis.

- **Unary / parentheses**
  - **Parenthesized term** `(t)`: we simply reuse the coefficients of `t`.

- **Binary arithmetic**
  - **Addition / subtraction**: for `t = u ± v`, assuming

    ```
      u(x) = a1 x + b1        v(x) = a2 x + b2
    ```

    we set

    ```
      a = a1 ± a2               b = b1 ± b2
    ```

    If either side is invalid, then `t` is invalid.
  - **Multiplication**: for `t = u * v`, we only allow at most one operand to depend on the variable:
    - If both `u` and `v` are constants, we set `(a,b) = (0, b1*b2)`.
    - If exactly one of them is affine in \(x\) and the other is constant, say
      ```
        u(x) = a1 x + b1        v(x) = b2
      ```

      then
      ```
        t(x) = (a1 x + b1) b2 = (a1 b2) x + (b1 b2).
      ```
    - If **both** operands contain the variable, we conservatively reject the term as potentially non‑linear.
  - **Division**: for `t = u / v`, we only allow division by a **constant**:
    - If `v` has any variable dependency, or its affine summary has nonzero slope (`a2 != 0`), or the constant part is zero (`b2 = 0`), we mark `t` as invalid.
    - Otherwise, with
      ```
        u(x) = a1 x + b1        v(x) = b2 != 0
      ```
      we set
      ```
        t(x) = (a1 x + b1) / b2 = (a1 / b2) x + (b1 / b2).
      ```

Any node for which we successfully compute `(a,b)` represents a **syntactically linear** term of degree at most 1. If at any point a rule above fails, we mark the term as **invalid** (either genuinely non‑linear, or syntactically too complex to certify as linear by this method).

From the final coefficients we can also:

- Check **non‑nullity**: the polynomial is identically zero iff `a = 0` and `b = 0`; we reject such terms in parameter positions.
- Compute the unique **root**, when it exists: if `a != 0`, the root is `-b / a`; if `a = 0`, there is no unique root.

### Limitations of the syntactic method

This procedure is intentionally conservative: it never accepts a term whose true polynomial degree is greater than 1, but it may reject some genuinely linear terms when linearity only appears **after algebraic simplifications**.

For example, consider the term:
```
  t(x) = (x*x - x*x + x) / x.
```
Semantically this simplifies to
```
  t(x) = (0 + x) / x = 1,
```
which is a constant (hence linear) polynomial. However, our syntactic checker sees:

- `x*x` as a product of two variable‑dependent subterms (non‑linear),
- and a division by `x` in the outer `/`,

so it correctly refuses to assign an affine summary and marks the term as invalid. This is acceptable for our purposes: we only need a **sound** (never‑wrong) filter for linear terms, not a complete one.


## In expressions

To translate Rel expressions like

```
  x+1 where A(x)
```

we might just rewrite it as

```
  {(z) : z=x+1} where A(x)
```

but then we have the problem of translating the formula `z=x+1` into SQL which is not permitted currently in our translation as it is not in a conjunction with other formulas. We could instead rewrite it as
```
  (z) : z=x+1 and A(x)
```
