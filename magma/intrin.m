
/* Implement some functional programming primitives in Magma.
   Circa 2023, Immir. */

intrinsic map (f, xs::SeqEnum) -> SeqEnum
  { return [f(xs[1]), f(xs[2]), ... ] }
  return [ f(x) : x in xs ];
end intrinsic;

intrinsic tail (xs::SeqEnum) -> SeqEnum
  { return tail of sequence }
  require #xs gt 0: "tail cannot be applied empty sequence";
  return xs[[2..#xs]];
end intrinsic;

car := head;
cdr := tail;

intrinsic head (xs::SeqEnum) -> SeqEnum
  { return head of sequence }
  require #xs gt 0: "head cannot be applied empty sequence";
  return xs[1];
end intrinsic;

intrinsic foldl (f, x0, xs::SeqEnum) -> .
  { foldl returns f(...f(f(x0,xs[1]),xs[2])...xs[n]) }
  return #xs eq 0 select x0 else $$(f, f(x0,head(xs)), tail(xs));
end intrinsic;

intrinsic foldl (f, xs::SeqEnum) -> .
  { foldl returns f(...f(f(xs[1],xs[2]),xs[3])...xs[n]) }
  return foldl(f, head(xs), tail(xs));
end intrinsic;

intrinsic scanl (f, x0, xs::SeqEnum) -> .
  { scanl returns [x0, f(x0,xs[1]), f(f(x0,xs[1]),xs[2]), ...]  }
  return #xs eq 0 select [x0] else [x0] cat $$(f, f(x0, head(xs)), tail(xs));
end intrinsic;

intrinsic scanl (f, xs::SeqEnum) -> .
  { scanl returns [f(xs[1],xs[2]), f(f(xs[1],xs[2]),xs[3]), ...]  }
  return scanl(f, head(xs), tail(xs));
end intrinsic;

