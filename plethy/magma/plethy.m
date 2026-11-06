freeze;

// magma/plethy.m -- Magma interface to the plethy `plethy` CLI.
//
// Magma package: attach once per session (or in ~/.magmarc) with
//
//     Attach("path/to/plethy/magma/plethy.m");
//
// after which the intrinsics are globally available.
//
// Two type-dispatched forms of Plethysm are provided:
//
//     Plethysm([2,1], [1,1])                 // SeqEnum -> Assoc
//     Plethysm([2,1], [1,1] : MaxLength := 3)
//     PlethysmHash128([2,1], [1,1])          // SeqEnum -> RngIntElt (128-bit canonical)
//
//     S := SFASchur(Rationals());
//     Plethysm(S.[2,1], S.[1,1])             // AlgSymElt -> AlgSymElt
//     Plethysm(S.[2,1] + S.[3], S.[1,1])     // linear in 1st arg auto-expanded
//
// All forms shell out to plethy via its --raw I/O mode (one fork per call;
// see ../README.md).  Binary discovery: $PLETHY_BIN > SchurlyBin (schurly
// AttachSpec) > bare `plethy` on $PATH.  plethy exposes only the 128-bit hash.

// Binary discovery (call-time): $PLETHY_BIN > baked SchurlyBin (present when
// attached via a schurly AttachSpec) > bare `plethy` on $PATH.
_plethy_bin := function()
    b := GetEnvironmentValue("PLETHY_BIN");
    if b eq "" then
        has, f := IsIntrinsic("SchurlyBin");   // handle, so standalone attach still compiles
        if has then b := f("plethy"); end if;
    end if;
    if b eq "" then b := "plethy"; end if;
    // Magma exports OMP_NUM_THREADS=1 to child processes; inherited, it pins the
    // OpenMP tool to a single thread (and can't be raised via PLETHY_THREADS,
    // which caps at omp_get_max_threads()).  Strip it so the tool chooses its own
    // thread count, exactly as when run directly from a shell.
    return "env -u OMP_NUM_THREADS " cat b;
end function;

// ----- private helpers --------------------------------------------------

_fmt_part := function(p)
    if #p eq 0 then return ""; end if;
    return Join([IntegerToString(x) : x in p], ",");
end function;

// Parse plethy's --raw output (COEF k p1 ... pk per line) into an
// AssociativeArray keyed by partition (SeqEnum of Integer).
_parse_terms := function(raw)
    A := AssociativeArray();
    for line in Split(raw, "\n") do
        toks := Split(line, " \t");
        if #toks eq 0 then continue; end if;
        coef := StringToInteger(toks[1]);
        k    := StringToInteger(toks[2]);
        A[[StringToInteger(toks[2+i]) : i in [1..k]]] := coef;
    end for;
    return A;
end function;

// 0 means "no bound" (CLI flag omitted).
_filter_flags := function(maxLength, maxPart)
    s := "";
    if maxLength gt 0 then s cat:= Sprintf(" -l %o", maxLength); end if;
    if maxPart   gt 0 then s cat:= Sprintf(" -p %o", maxPart);   end if;
    return s;
end function;

// ----- public intrinsics ------------------------------------------------

intrinsic Plethysm(outer::SeqEnum[RngIntElt], inner::SeqEnum[RngIntElt]
    : MaxLength := 0, MaxPart := 0) -> Assoc
{ Compute s_outer[s_inner] in the Schur basis via the plethy CLI.
  outer and inner are partitions as SeqEnums of Integer (non-increasing).
  Returns an AssociativeArray mapping each non-zero partition to its
  integer coefficient.
  MaxLength / MaxPart > 0 pass through to plethy's -l / -p filters,
  dropping output terms with len(nu) > MaxLength or nu[1] > MaxPart. }
    cmd := Sprintf("%o --raw '%o' '%o'%o",
                   _plethy_bin(), _fmt_part(outer), _fmt_part(inner),
                   _filter_flags(MaxLength, MaxPart));
    return _parse_terms(Pipe(cmd, ""));
end intrinsic;

intrinsic PlethysmHash128(outer::SeqEnum[RngIntElt], inner::SeqEnum[RngIntElt]
    : MaxLength := 0, MaxPart := 0) -> RngIntElt
{ Return the 128-bit canonical hash of s_outer[s_inner], as printed by
  `plethy -H`.  Term-order-independent, SHA-256-based (see the paper for the
  exact rule).  Same semantics as Plethysm for MaxLength / MaxPart. }
    cmd := Sprintf("%o --raw -qH '%o' '%o'%o",
                   _plethy_bin(), _fmt_part(outer), _fmt_part(inner),
                   _filter_flags(MaxLength, MaxPart));
    return StringToInteger(Trim(Pipe(cmd, "")), 16);
end intrinsic;

intrinsic Plethysm(f::AlgSymElt, g::AlgSymElt
    : MaxLength := 0, MaxPart := 0) -> AlgSymElt
{ Natural-interface plethysm on the Schur basis of an SFASchur algebra:
  given f, g in S := SFASchur(R), compute s_f[s_g] and return an element
  of S.  Plethysm is linear in the first argument and is expanded
  automatically; the second argument must be a single Schur basis term
  with coefficient 1 (plethysm is nonlinear in the inner argument; expand
  at the call site if needed). }
    S := Parent(f);
    require Parent(g) eq S:
        "f and g must lie in the same SFASchur algebra";
    g_parts := Support(g);
    g_coefs := Coefficients(g);
    require #g_parts eq 1 and g_coefs[1] eq 1:
        "second argument must be a single Schur basis element s_{mu}";
    inner   := g_parts[1];
    f_parts := Support(f);
    f_coefs := Coefficients(f);
    result  := S ! 0;
    for i in [1..#f_parts] do
        if f_coefs[i] eq 0 then continue; end if;
        A := Plethysm(f_parts[i], inner : MaxLength := MaxLength, MaxPart := MaxPart);
        // Balanced &+ builds the (possibly huge) sum in near-linear time; a
        // per-term `result +:= ...` loop is O(n^2) and hangs for large results.
        // The `S |` universe folds an empty A straight to S!0.
        result +:= &+[ S | (f_coefs[i] * A[nu]) * (S . nu) : nu in Keys(A) ];
    end for;
    return result;
end intrinsic;
