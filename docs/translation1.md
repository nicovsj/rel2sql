# Translation of terms

## In parameters

### Approach 1: Syntactic method

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

#### Limitations of the syntactic method

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


### Approach 2: Variable replacement

Let's go back to the full application
```
  A(x+1)
```
We can rewrite this as
```
  A(z) and z = x+1
```
This is not yet allowed in our translation because of how safety analysis is perfomed, but we can actually compute the safety of the whole conjunction.

What we really have is:
```
 safe( A(z) ) = { (z) in A }
 safe( z = x+1 ) = { (x) in safety(z) - 1; (z) in safety(x) + 1 }
 safe( A(z) and z = x+1 ) = { (z) in A ; (x) in A - 1}
```


```
{(x,y): A(x) or B(y)}[1,2]
```
## In expressions

To translate Rel expressions like

```
  x+1 where A(x)
```

we might just rewrite it as

```
  {(z) : z=x+1 and A(x)}
```

In general we can always do these transformations:
```
  [x]:x+1 => [x]: {(z):z=x+1}
  (..., x+1, ...) => (..., (z):z=x+1, ...)
  {...; x+1; ...} => {...; (z):z=x+1; ...}
```

now we have to deal with conditions like:
```
  f(x1, ..., xn) = g(y1, ..., ym)
```
where we'll assume that `f` and `g` are linear polynomials in their arguments.

let's suppose that we see only one of these terms among a conjunction of safe formulas, so

```
 f(x1, ..., xn) = g(y1, ..., ym) and C1 and ... and Ck
```

We CAN translate this if
```
|{x1, ..., xn, y1, ..., ym} - (FV(C1) ∪ ... ∪ FV(Ck))| <= 1
```

meaning that all the free variables of the terms `f` and `g` are present in the conjunction but for one.

Let's suppose that we have the case where variable `xi` is the one that is not present in the conjunction. And let's say that
```
xi = a1 x1 + ... + ai-1 xi-1 + ai+1 xi+1 + ... + an xn + b1 y1 + ... + bm ym + c
```

given that `f` and `g` are linear polynomials in their arguments then finding these coefficients is always possible and easy to do. Then we can translate this as

```sql
SELECT tr(x1) AS x1, ..., tr(xi-1) AS xi-1, tr(xi+1) AS xi+1, ..., tr(xn) AS xn,
       tr(y1) AS y1, ..., tr(ym) AS ym,
       ai * tr(xi) + ... + ai-1 * tr(xi-1) + ai+1 * tr(xi+1) + ... + an * tr(xn) + b1 * tr(y1) + ... + bm * tr(ym) + c AS xi
FROM C1, C2, ..., Ck
WHERE EQ(free_variables)
```

So for example if we have:
```
x = y + 1 and A(y)
```
then this will be translated as:
```sql
SELECT T1.y, T1.y + 1 AS x
FROM (
  SELECT T0.A1 AS y
  FROM A AS T0
) AS T1;
```

So in general this is a special case for linear terms present in an equality condition.
