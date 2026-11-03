# polys.cc: review, Laguerre fix, jackpot speed-up plan

Immir — 2026-11-02 22:47 EST

Laguerre's method replaced fixed-count Newton and cut the failure rate from
13.6% of batches to none in 5000 seeds. Since we start from the roots of f, a
new roots-based solver (added beside Laguerre, which is kept) is 7× faster and
far more accurate. The next big win, for a billion runs with about a dozen
jackpots, is a cheap screen that cannot miss a jackpot, with full precision only
on survivors.

Updated 2026-11-02: roots-based solver section added.

## Review findings

Two correctness bugs are fixed; the performance items are open. Before the fix,
136 of 1000 seeds produced garbage in at least one lane, with coefficient errors
up to 5e6.

| Issue | Where | Effect | Status |
| --- | --- | --- | --- |
| Fixed 16 Newton iterations, no convergence test | `critical_points` | Unconverged lane deflates a bad root and corrupts every later root; 13.6% of batches from `r[1]`, 28% from 0 | Fixed (Laguerre) |
| `operator-(a, f)` computed f − a | line 116 | `3.0 - x` gave `x - 3` | Fixed |
| No polishing after deflation | `critical_points` | Error builds up root to root | Fixed |
| `L1-err` is really L∞ of coefficients, checks factorisation not root accuracy | `main` | Over-optimistic check | Open |
| No guard on f″ = 0; division ignores remainder | `critical_points`, `operator/` | Silent inf/NaN | Partly fixed: NaN lanes now warn |
| `vector<vec>` allocates on every `+`, `*`, `shift`; `operator/` is O(d²) allocations | throughout | Heap traffic dominates | Open |
| Power-accumulation `eval` instead of Horner; deflation by long division instead of synthetic division | `eval`, `operator/` | Extra ops and rounding | Partly fixed: Laguerre uses Horner |
| By-value `polys` parameters | `eval`, `dump` | Copies | Partly fixed |
| `#define N 4` hard-codes AVX2 width; `#define append`; non-standard `uint` | top of file | 8 lanes free on AVX-512 | Open |

## Laguerre fix

Laguerre converges globally on real-rooted polynomials, and f real-rooted
implies f′ real-rooted (Rolle), so any real start works. The change is in the
working tree, not committed.

```latex
G = \frac{p'}{p}, \quad H = G^2 - \frac{p''}{p}, \quad
z \leftarrow z - \frac{n}{G + \operatorname{sign}(G)\sqrt{(n-1)(nH - G^2)}}
```

- `eval_with_derivatives`: one Horner pass for p, p′, p″ and Σ|aᵢ||z|ⁱ (the
  rounding-error scale).
- `laguerre_step`: vectorised across lanes; branch-free sign choice via
  `copysign`; negative radicand from rounding clamped to 0.
- Stopping rule per lane: |dz| ≤ 1e-14(1+|z|), or
  |p(z)| ≤ 2n·DBL_EPSILON·Σ|aᵢ||z|ⁱ (root to working precision, dz = 0). The
  step test alone gave about 60 spurious warnings per 2000 seeds near close
  roots.
- Cap of 64 iterations; an unconverged or NaN lane prints a warning to stderr
  and flushes.
- `critical_points`: find roots on the deflated f′, then polish each against
  the original f′; const-ref arguments; asserts root count = degree.
- `operator-(a, f)` is now `-f + a`.

Stress test, 5000 seeds of 4 lanes, degree-6 f with roots uniform in [−10, 10]:

| Start point | Coefficient error > 1e-6 | Warnings | Max coefficient error |
| --- | --- | --- | --- |
| 0 | 0 | 0 | 4.9e-9 |
| `r[1]` | 1 | 0 | 1.9e-6 |
| +100 | 0 | 0 | 2.2e-7 |
| −1e4 | 1 | 0 | 4.2e-6 |

The two cases above 1e-6 are rounding, not divergence: coefficients there are
1e4–1e5, so the relative error is about 1e-11.

## Roots-based solver (implemented)

We start from the roots of f, so critical points need no coefficients, no
deflation and no Laguerre. Code is in `polys.cc` from the "Special case"
comment onwards; `main` runs it beside Laguerre and compares. Uncommitted.

After sorting, Rolle puts exactly one critical point strictly inside each gap
(rₖ, rₖ₊₁). It is the zero of the log-derivative

```latex
g(x) = \frac{f'(x)}{f(x)} = \sum_i \frac{1}{x - r_i}, \qquad
g'(x) = -\sum_i \frac{1}{(x - r_i)^2} < 0
```

so g is strictly decreasing on each gap and every gap is solved independently.

- `sorted<n>`: odd-even transposition network, branch-free, lanes independent
  (the general-n version of the circuit in NOTES).
- `two_pole_start`: keep the gap's two poles exact, freeze the rest of g at the
  midpoint m as C = Σ_{i≠k,k+1} 1/(m − rᵢ). With x = a + τ and w = b − a this
  gives Cτ² + (2 − Cw)τ − w = 0, whose root in (0, w) is, stably,
  τ = 2w / (2 − Cw + √(4 + C²w²)). Same idea as LAPACK's `dlaed4` for secular
  equations.
- `critical_points_from_roots<n>`: Newton on g, bisection fallback when a step
  leaves the bracket; all gaps in one loop for instruction-level parallelism.
  Zero-width gap (double root) returns that root. Same 64-iteration cap and
  flushed warning as Laguerre.
- `eval_from_roots`: critical value f(c) = ∏(c − rᵢ).
- Bug found while testing: a strict bracket test (`newton > lo`) rejected tiny
  Newton steps that landed on the endpoint x had just become, so bisection threw
  the point away; 51 iterations. The inclusive test gives 5–6.

| | Laguerre on coefficients | From roots |
| --- | --- | --- |
| Iterations (5000 seeds) | up to 64 per root, roots found sequentially | 5–6 per batch (slowest gap and lane, all gaps together) |
| Worst residual \|g(c)\| / Σ\|1/(c − rᵢ)\|, long double, 20000 batches | 3.2e-7 | 5.5e-11 |
| Time per batch of 4 polynomials | 1857 ns (incl. expanding f) | 266 ns |
| 1e9 polynomials, one core | ≈ 460 s | ≈ 67 s |

The largest disagreement between the methods (7e-10) is Laguerre's error: the
coefficient form is badly conditioned when critical points are close. No
warnings from either over 5000 seeds. Benchmark harness: `h.cc` in that
session's scratchpad (includes `polys.cc` with `main` renamed).

At 266 ns the cost is roughly what ~180 vector divisions per batch take, so
further gains come from fewer or cheaper divisions (next section).

## Jackpot speed-up plan

At a jackpot rate near 1e-8, almost all work should be a cheap screen that never
misses a jackpot. Full precision runs only on survivors. The roots-based solver
above is implemented; the screen is not.

### Screen, then verify

1. Screen in float32: twice the lanes (8 on AVX2, 16 on AVX-512), fixed
   iteration count, no convergence checks, no polishing, no warnings,
   branch-free.
2. Make the screen one-sided: widen the jackpot threshold by more than the
   screen's error bound, so a jackpot always passes and only extra candidates
   leak through.
3. Reject whole vectors at once: one any-lane test per vector; at a 1e-8 hit
   rate nearly every vector stops there.
4. Verify survivors with the double-precision Laguerre code; print and flush
   jackpots immediately.
5. Store nothing for rejects: seed a counter-based RNG (e.g. Philox) by batch
   index, so a candidate is regenerated from its index and the hot loop never
   writes memory.

### Screen on top of the roots-based solver

1. Fixed iteration count, no convergence test: with the two-pole start, 3–4
   iterations probably reach screening accuracy (to be measured).
2. float32: twice the lanes and cheaper division.
3. Bound before iterating: if the jackpot test is on critical values, gap
   endpoints, the two-pole start and the monotonicity of g give interval bounds
   on each f(c), so many batches may be rejected with no Newton steps.
4. Re-run survivors with the double-precision `critical_points_from_roots`.

### Engineering either way

- Fixed degree: `std::array<vec, D+1>` (or root arrays), structure-of-arrays
  layout, no heap.
- Horner evaluation; synthetic division qₖ = fₖ₊₁ + z·qₖ₊₁ if staying with
  coefficients.
- Lane count from a `constexpr`, not `#define N 4`.
- Split the billion across cores; there is no shared state.
- Estimated one to two orders of magnitude over the current `vector<vec>` code
  from engineering alone; not yet measured.

## Open questions and next steps

The screen design depends on the jackpot condition.

- [ ] What is the jackpot condition? (critical value below a threshold, two
      critical points within δ, a critical point in a region, …)
- [x] In the real workload, are the roots of f known, or only its coefficients?
      Roots are known.
- [ ] Review and commit the Laguerre, `operator-` and roots-based solver changes
      in `polys/polys.cc`
- [ ] Measure the fixed iteration count the screen needs with the two-pole start
- [ ] Replace the `L1-err` check with a relative residual
      |f′(zᵢ)| / Σ|f′ₖ||zᵢ|ᵏ
- [ ] Fixed-degree `std::array` rewrite with Horner and synthetic division
- [ ] Build the float32 screen + double verify and benchmark against the
      current code
