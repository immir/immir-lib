
/* Implement some functional programming primitives in Magma.
   Circa 2023, Immir.

   Container intrinsics come in SeqEnum, List and SetIndx flavours
   (Magma has no union types in signatures); all share the index-based
   helpers below. Results preserve the container type, except that scans
   and zips of an SetIndx return a SeqEnum, since their values may repeat. */

function fold_left(f, acc, xs, first)
  for i in [first..#xs] do acc := f(acc, xs[i]); end for;
  return acc;
end function;

function fold_right(f, acc, xs, last)
  for i in [last..1 by -1] do acc := f(xs[i], acc); end for;
  return acc;
end function;

function scan_left(f, x0, xs, first)
  return [ i eq first - 1 select x0 else f(Self(i - first + 1), xs[i])
         : i in [first - 1..#xs] ];
end function;

function zip_with(f, xs, ys)
  return [ f(xs[i], ys[i]) : i in [1..#xs] ];
end function;

to_list := func< xs | [* x : x in xs *] >;

// ----------------------------------------------------------------------

intrinsic map (f, xs::SeqEnum) -> SeqEnum
  { return [f(xs[1]), f(xs[2]), ... ] }
  return [ f(x) : x in xs ];
end intrinsic;

intrinsic map (f, xs::List) -> List {"}
  return [* f(x) : x in xs *];
end intrinsic;

intrinsic map (f, xs::SetIndx) -> SetIndx {"}
  return {@ f(x) : x in xs @};
end intrinsic;

intrinsic head (xs::SeqEnum) -> .
  { return head of sequence }
  require #xs gt 0: "head cannot be applied to empty sequence";
  return xs[1];
end intrinsic;

intrinsic head (xs::List) -> . {"}
  require #xs gt 0: "head cannot be applied to empty list";
  return xs[1];
end intrinsic;

intrinsic head (xs::SetIndx) -> . {"}
  require #xs gt 0: "head cannot be applied to empty indexed set";
  return xs[1];
end intrinsic;

intrinsic tail (xs::SeqEnum) -> SeqEnum
  { return tail of sequence }
  require #xs gt 0: "tail cannot be applied to empty sequence";
  return xs[[2..#xs]];
end intrinsic;

intrinsic tail (xs::List) -> List {"}
  require #xs gt 0: "tail cannot be applied to empty list";
  return [* xs[i] : i in [2..#xs] *];
end intrinsic;

intrinsic tail (xs::SetIndx) -> SetIndx {"}
  require #xs gt 0: "tail cannot be applied to empty indexed set";
  return {@ xs[i] : i in [2..#xs] @};
end intrinsic;

intrinsic car (xs) -> .
  { synonym for head }
  return head(xs);
end intrinsic;

intrinsic cdr (xs) -> .
  { synonym for tail }
  return tail(xs);
end intrinsic;

// ----------------------------------------------------------------------

intrinsic foldl (f, x0, xs::SeqEnum) -> .
  { foldl returns f(...f(f(x0,xs[1]),xs[2])...xs[n]) }
  return fold_left(f, x0, xs, 1);
end intrinsic;

intrinsic foldl (f, x0, xs::List) -> . {"}
  return fold_left(f, x0, xs, 1);
end intrinsic;

intrinsic foldl (f, x0, xs::SetIndx) -> . {"}
  return fold_left(f, x0, xs, 1);
end intrinsic;

intrinsic foldl (f, xs::SeqEnum) -> .
  { foldl returns f(...f(f(xs[1],xs[2]),xs[3])...xs[n]) }
  require #xs gt 0: "foldl without initial value needs a non-empty argument";
  return fold_left(f, xs[1], xs, 2);
end intrinsic;

intrinsic foldl (f, xs::List) -> . {"}
  require #xs gt 0: "foldl without initial value needs a non-empty argument";
  return fold_left(f, xs[1], xs, 2);
end intrinsic;

intrinsic foldl (f, xs::SetIndx) -> . {"}
  require #xs gt 0: "foldl without initial value needs a non-empty argument";
  return fold_left(f, xs[1], xs, 2);
end intrinsic;

intrinsic foldr (f, x0, xs::SeqEnum) -> .
  { foldr returns f(xs[1],f(xs[2],...f(xs[n],x0)...)) }
  return fold_right(f, x0, xs, #xs);
end intrinsic;

intrinsic foldr (f, x0, xs::List) -> . {"}
  return fold_right(f, x0, xs, #xs);
end intrinsic;

intrinsic foldr (f, x0, xs::SetIndx) -> . {"}
  return fold_right(f, x0, xs, #xs);
end intrinsic;

intrinsic foldr (f, xs::SeqEnum) -> .
  { foldr returns f(xs[1],f(xs[2],...f(xs[n-1],xs[n])...)) }
  require #xs gt 0: "foldr without initial value needs a non-empty argument";
  return fold_right(f, xs[#xs], xs, #xs - 1);
end intrinsic;

intrinsic foldr (f, xs::List) -> . {"}
  require #xs gt 0: "foldr without initial value needs a non-empty argument";
  return fold_right(f, xs[#xs], xs, #xs - 1);
end intrinsic;

intrinsic foldr (f, xs::SetIndx) -> . {"}
  require #xs gt 0: "foldr without initial value needs a non-empty argument";
  return fold_right(f, xs[#xs], xs, #xs - 1);
end intrinsic;

// ----------------------------------------------------------------------

intrinsic scanl (f, x0, xs::SeqEnum) -> SeqEnum
  { scanl returns [x0, f(x0,xs[1]), f(f(x0,xs[1]),xs[2]), ...] }
  return scan_left(f, x0, xs, 1);
end intrinsic;

intrinsic scanl (f, x0, xs::List) -> List {"}
  return to_list(scan_left(f, x0, xs, 1));
end intrinsic;

intrinsic scanl (f, x0, xs::SetIndx) -> SeqEnum {"}
  return scan_left(f, x0, xs, 1);
end intrinsic;

intrinsic scanl (f, xs::SeqEnum) -> SeqEnum
  { scanl returns [xs[1], f(xs[1],xs[2]), f(f(xs[1],xs[2]),xs[3]), ...] }
  require #xs gt 0: "scanl without initial value needs a non-empty argument";
  return scan_left(f, xs[1], xs, 2);
end intrinsic;

intrinsic scanl (f, xs::List) -> List {"}
  require #xs gt 0: "scanl without initial value needs a non-empty argument";
  return to_list(scan_left(f, xs[1], xs, 2));
end intrinsic;

intrinsic scanl (f, xs::SetIndx) -> SeqEnum {"}
  require #xs gt 0: "scanl without initial value needs a non-empty argument";
  return scan_left(f, xs[1], xs, 2);
end intrinsic;

// ----------------------------------------------------------------------

intrinsic zipWith (f, xs::SeqEnum, ys::SeqEnum) -> SeqEnum
  { zipWith returns [f(xs[1],ys[1]), f(xs[2],ys[2]), ...]; lengths must agree }
  require #xs eq #ys: "zipWith arguments must have equal length";
  return zip_with(f, xs, ys);
end intrinsic;

intrinsic zipWith (f, xs::List, ys::List) -> List {"}
  require #xs eq #ys: "zipWith arguments must have equal length";
  return to_list(zip_with(f, xs, ys));
end intrinsic;

intrinsic zipWith (f, xs::SetIndx, ys::SetIndx) -> SeqEnum {"}
  require #xs eq #ys: "zipWith arguments must have equal length";
  return zip_with(f, xs, ys);
end intrinsic;

intrinsic iterate (f, x, n::RngIntElt) -> SeqEnum
  { iterate returns [x, f(x), f(f(x)), ..., f^n(x)] (n applications, n+1 terms) }
  require n ge 0: "iterate needs a non-negative number of applications";
  return [ i eq 0 select x else f(Self(i)) : i in [0..n] ];
end intrinsic;
