
// ----------------------------------------------------------------------

get,set := NewEnv(["MinAbs"]);
set("MinAbs", true);

intrinsic Int(:MinAbs := false) { Set default representative choice. }
    set("MinAbs", MinAbs);
end intrinsic;

intrinsic Int(x::FldFinElt : MinAbs := get("MinAbs")) -> RngIntElt { }
  xx := Integers()!x; p := Characteristic(Parent(x));
  return MinAbs and xx gt p div 2 select xx-p else xx;
end intrinsic;

intrinsic Int(x::RngIntResElt : MinAbs := get("MinAbs")) -> RngIntElt { }
  xx := Integers()!x; n := #Parent(x);
  return MinAbs and xx gt n div 2 select xx-n else xx;
end intrinsic;

intrinsic Int(M::ModMatFldElt[FldFin] : MinAbs := get("MinAbs")) -> . { Convert. }
  P := ChangeRing(Parent(M), Integers());
  return P![ Int(x : MinAbs := MinAbs) : x in Eltseq(M) ];
end intrinsic;

intrinsic Int(M::AlgMatElt[FldFin] : MinAbs := get("MinAbs")) -> . { Convert. }
  P := ChangeRing(Parent(M), Integers());
  return P![ Int(x : MinAbs := MinAbs) : x in Eltseq(M) ];
end intrinsic;

intrinsic Int(M::ModMatRngElt[RngIntRes] : MinAbs := get("MinAbs")) -> . { Convert. }
  P := MatrixAlgebra(Integers(), Nrows(M), Ncols(M));
  return P![ Int(x : MinAbs := MinAbs) : x in Eltseq(M) ];
end intrinsic;

intrinsic Int(M::AlgMatElt[RngIntRes] : MinAbs := get("MinAbs")) -> . { Convert. }
  P := MatrixAlgebra(Integers(), Nrows(M));
  return P![ Int(x : MinAbs := MinAbs) : x in Eltseq(M) ];
end intrinsic;


// ----------------------------------------------------------------------
// functional stuff (to be moved)

intrinsic _functional_ops () { (IMR) Functional stuff } end intrinsic;

// NB: reversed arguments
intrinsic 'eq' (x) -> . {"}  return func<y|'eq'(y,x)>; end intrinsic;
intrinsic 'ne' (x) -> . {"}  return func<y|'ne'(y,x)>; end intrinsic;
intrinsic 'ge' (x) -> . {"}  return func<y|'ge'(y,x)>; end intrinsic;
intrinsic 'gt' (x) -> . {"}  return func<y|'gt'(y,x)>; end intrinsic;
intrinsic 'le' (x) -> . {"}  return func<y|'le'(y,x)>; end intrinsic;
intrinsic 'lt' (x) -> . {"}  return func<y|'lt'(y,x)>; end intrinsic;

intrinsic head (xs) -> . {"} return xs[1];        end intrinsic;
intrinsic tail (xs) -> . {"} return xs[2..#xs];   end intrinsic;
intrinsic init (xs) -> . {"} return xs[1..#xs-1]; end intrinsic;
intrinsic last (xs) -> . {"} return xs[#xs];      end intrinsic;

intrinsic fst  (x) -> . {"} return x[1];             end intrinsic;
intrinsic snd  (x) -> . {"} return x[2];             end intrinsic;
intrinsic flip (f) -> . {"} return func<a,b|f(b,a)>; end intrinsic;

intrinsic iif  (b,x,y) -> . {"} return b select x else y; end intrinsic;

intrinsic map (f, xs::List) -> List {"}         return [* f(x) : x in xs *]; end intrinsic;
intrinsic map (f, xs::Tup) -> Tup {"}           return < f(x) : x in xs >;   end intrinsic;
intrinsic map (f, xs::Set) -> Set {"}           return { f(x) : x in xs };   end intrinsic;
intrinsic map (f, xs::SetMulti) -> SetMulti {"} return {* f(x) : x in xs *}; end intrinsic;
intrinsic map (f, xs::SetIndx) -> SetIndx {"}   return {@ f(x) : x in xs @}; end intrinsic;
intrinsic map (f, xs::SeqEnum) -> SeqEnum {"}   return [ f(x) : x in xs ]; end intrinsic;

intrinsic filter (f, xs::List) -> List {"}         return [* x : x in xs | f(x) *]; end intrinsic;
intrinsic filter (f, xs::Tup) -> Tup {"}           return <  x : x in xs | f(x) >;  end intrinsic;
intrinsic filter (f, xs::Set) -> Set {"}           return {  x : x in xs | f(x) };  end intrinsic;
intrinsic filter (f, xs::SetMulti) -> SetMulti {"} return {* x : x in xs | f(x) *}; end intrinsic;
intrinsic filter (f, xs::SetIndx) -> SetIndx {"}   return {@ x : x in xs | f(x) @}; end intrinsic;
intrinsic filter (f, xs::SeqEnum) -> SeqEnum {"}   return [  x : x in xs | f(x) ];  end intrinsic;

intrinsic map    (f, xs::.) -> SeqEnum {"}         return [ f(x) : x in Eltseq(xs) ];       end intrinsic;
intrinsic filter (f, xs::.) -> SeqEnum {"}         return [  x : x in Eltseq(xs) | f(x) ];  end intrinsic;

// fmap? what contexts?
intrinsic fmap (f, x::Mtrx) -> Mtrx {"} return Matrix([ map(f,Eltseq(v)) : v in Rows(x) ]); end intrinsic;

intrinsic foldl (f, z, xs) -> SeqEnum {"}
  for x in xs do z := f(z,x); end for; return z;
end intrinsic;

intrinsic foldl (f, xs) -> SeqEnum {"}
  z := head(xs); for x in tail(xs) do z := f(z,x); end for; return z;
end intrinsic;

intrinsic foldr (f, z, xs) -> SeqEnum {"}
  for x in Reverse(xs) do z := f(x,z); end for; return z;
end intrinsic;

intrinsic scanl (f,z,xs) -> . {"}
  return [ i eq 0 select z else f(Self(i),xs[i]) : i in [0..#xs] ];
end intrinsic;

intrinsic scanr (f,z,xs) -> . {"}
  return Reverse(scanl(flip(f), z, Reverse(xs)));
end intrinsic;

intrinsic unfoldr (f,x) -> . {"} // f(x) = either <y,x'> or <>
  yy := []; y := f(x);
  while y cmpne <> do
    Append(~yy, y[1]); x := y[2]; y := f(x);
  end while;
  return yy;
end intrinsic;

// function composition using '*'
intrinsic '*' (f::UserProgram, g) -> UserProgram {"} return func<x | f(g(x)) >; end intrinsic;
intrinsic '*' (f::Intrinsic,   g) -> UserProgram {"} return func<x | f(g(x)) >; end intrinsic;

// ----------------------------------------------------------------------

function fac(n, o)
  return flip(Join)(" * ",
    [ Sprintf(o cat iif(x[2] eq 1, "", "^%o"), x[1], x[2])
    : x in Factorisation(n) ]);
end function;

intrinsic Fac(n::RngIntElt) -> .
  { (IMR) Pretty-print factorisation. }
  return fac(n, "%o");
end intrinsic;

intrinsic Fac(n::.) -> . {"}
  return fac(n, "(%o)");
end intrinsic;

intrinsic Fac(n::FldRatElt) -> . {"}
    return Fac(Numerator(n)) cat
           (den eq 1 select "" else (" / (" cat Fac(den) cat ")"))
               where den is Denominator(n);
end intrinsic;

// ----------------------------------------------------------------------

intrinsic Wall() -> .
  { (IMR) Returns wall time in seconds since 1 Jan 1970. }
  return eval Pipe("date +%s.%N", "");
end intrinsic;

// ----------------------------------------------------------------------

intrinsic LogBinomial(n,k) -> .
  { Log of binomial. }
  return LogGamma(n+1) - LogGamma(k+1) - LogGamma(n-k+1);
end intrinsic;

intrinsic Log2Binomial(n,k) -> .
  { Log base 2 of binomial. }
  return LogBinomial(n,k) / Log(2);
end intrinsic;

// ----------------------------------------------------------------------

intrinsic RealSubfield (K) -> .
  { Real subfield of cyclotomic field K. }
  return Explode(Rep([ z : z in Subfields(K) | Degree(L) eq Degree(K)/2 and
                       IsReal(rho(L.1)) where L,rho := Explode(z) ]));
end intrinsic;
