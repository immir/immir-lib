# plethy

Standalone Schur-function plethysm: computes `s_outer[s_inner]` in the Schur
basis. The implementation uses C++23 and OpenMP; there are no external library
dependencies.

Build from this directory with `make`. For example:

```sh
./plethy --raw 2,1 1,1
```

Set `PLETHY_BIN` to the executable path when using a binding from another
working directory. Both bindings also search `PATH`; Sage additionally checks
for the executable beside its binding.

## Sage

```sage
load("/path/to/immir-lib/plethy/sage/plethy.py")
s = SymmetricFunctions(QQ).schur()
plethysm(s[2,1], s[1,1])
```

The outer and inner arguments may each be a partition/list or a single Schur
basis element. `plethysm_hash128(outer, inner)` returns the canonical 128-bit
hash. The optional keyword arguments `max_length` and `max_part` apply output
filters.

## Magma

```magma
Attach("/path/to/immir-lib/plethy/magma/plethy.m");
A := Plethysm([2,1], [1,1]);
```

The sequence form returns an associative array from partitions to integer
coefficients. `PlethysmHash128(outer, inner)` returns the canonical hash. The
`AlgSymElt` overload accepts Schur-basis elements and expands a linear
combination in the outer argument. Set `PLETHY_BIN` or put `plethy` on `PATH`
before starting Magma.
