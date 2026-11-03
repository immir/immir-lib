# polys.cc: review, Laguerre fix, jackpot speed-up plan

Immir — 2026-11-02 22:47 EST

Laguerre's method replaced fixed-count Newton and cut the failure rate from
13.6% of batches to none in 5000 seeds. Since we start from the roots of f, a
new roots-based solver (added beside Laguerre, which is kept) is 7× faster and
far more accurate. For a billion batches with about a dozen jackpots, the
jackpot search screens with rigorous lower bounds (box endpoints, then
start points) and solves fully only on survivors: about 18 ns per batch in a
synthetic test, with no missed jackpots.

Updated 2026-11-02 (same day): roots-based solver and jackpot search added.

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
further gains come from fewer or cheaper divisions; the jackpot search below
avoids most of them.

## Jackpot search (implemented)

Jackpot condition: lane i holds f_i in its own coordinate x_i over a fixed box
B = ∏[loᵢ, hiᵢ] (the fundamental domain; dimension = number of lanes), the norm
is ∏ᵢ fᵢ(xᵢ), and a jackpot is max_B |norm| < 1. The maximum separates:

```latex
\max_B \Big|\prod_i f_i(x_i)\Big| = \prod_i M_i, \qquad
M_i = \max\big(|f_i(lo_i)|,\ |f_i(hi_i)|,\ |f_i(c)| \text{ for critical } c \in (lo_i, hi_i)\big)
```

since |fᵢ| has one peak per gap between roots and is monotone outside them.
Any point x in the box gives |fᵢ(x)| ≤ Mᵢ, so point evaluations are rigorous
lower bounds: a batch whose lower-bound product is ≥ 1 + 1e-12 is certainly not
a jackpot (the margin is far above the rounding of 24 multiplies).

| Stage | Computes | Cost | Rejects (synthetic test) |
| --- | --- | --- | --- |
| 0 | \|fᵢ\| at the box endpoints; no sort, no divisions | ~3 ns | 99.94% at rmax = 4, 99.998% at rmax = 5 |
| 1 | sort + \|fᵢ\| at two-pole start points inside the box | ~80 ns | nearly all the rest: survivors ≈ jackpots (1501 vs 1491) |
| 2 | full `critical_points_from_roots`, exact Mᵢ, ∏Mᵢ < 1 | ~270 ns | final decision |

- Total ≈ 18 ns per batch, of which ≈ 13.6 ns is the test RNG, so ≈ 18 s per
  1e9 batches on one core.
- Jackpots print batch index, max|norm|, Mᵢ and roots, flushed.
  |∏Mᵢ − 1| < 1e-9 prints a flushed `BORDERLINE … verify exactly` line.
- Roots from a counter-based RNG (splitmix64 of (seed, batch)), so any batch is
  regenerated from its index. Replace `batch_roots` with the real generator.
- Usage: `./polys batches=1000000000 rmax=5 lo=-0.5 hi=0.5 seed=…`; `check=1`
  also runs the full solve on every batch. Without `batches=` the old demo runs.
- Code: `screen_lower_bound`, `max_abs_on_box`, `jackpot_search` in `polys.cc`.

Validation: check mode over 5M batches (rmax = 3, 4, 5) found 0 jackpots the
screen rejected; staged and check-mode runs gave identical jackpot lists (11,417
at rmax = 3, 159 at rmax = 4, same seed).

Caveats:

- Test data is synthetic (roots uniform in [−rmax, rmax], box [−0.5, 0.5]⁴);
  the stage-0 rate depends on the real root distribution relative to the box.
  Re-run once with `check=1` on the real generator.
- |norm| is assumed (absolute values taken).
- Stage 1's bound is up to 1.3–1.7× below the true maximum in the worst case
  (critical point just inside the box edge, start point just outside); that
  costs extra full solves, not correctness.
- Proof-grade jackpots need a rigorous upper bound on each Mᵢ: bound |f| on the
  final bracket using f′(c) ≈ 0, or re-check the few cases exactly in Magma.

## Further speed-ups

- OpenMP over batches (independent; 24 cores here): 1e9 in about a second.
- AVX-512 is available but the 4-lane `vec` uses half of each 8-double
  register: pack two batches per vector.
- Fixed-degree `std::array` rewrite, Horner and synthetic division for the
  coefficient-based Laguerre path, if it stays in use.

## Open questions and next steps

- [x] What is the jackpot condition? max over a fixed box of |∏ᵢ fᵢ(xᵢ)| < 1
      (see Jackpot search).
- [x] In the real workload, are the roots of f known, or only its coefficients?
      Roots are known.
- [x] Commit the Laguerre, `operator-` and roots-based solver changes
- [ ] Plug in the real root generator and run once with `check=1`
- [ ] Confirm |norm| (not signed norm) is the right condition
- [ ] Rigorous verification of jackpots (bracket bound or exact Magma check)
- [ ] Replace the `L1-err` check with a relative residual
      |f′(zᵢ)| / Σ|f′ₖ||zᵢ|ᵏ
- [ ] Fixed-degree `std::array` rewrite with Horner and synthetic division
- [ ] OpenMP and two-batches-per-vector for AVX-512
