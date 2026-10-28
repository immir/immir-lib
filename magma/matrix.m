/*
    Matrix(< [1,2,3], [4,5,6] >) =  [ 1 2 3 ]
                                    [ 4 5 6 ]

    Matrix(< [1,2,3], 4 >)     =    [ 1 2 3 ]
                                    [ 4     ]
                                    [   4   ]
                                    [     4 ]

    Matrix(< < [1,2,3], 4 > >)  =   [ 1 4     ]
                                    [ 2   4   ]
                                    [ 3     4 ]

    Block matrices: with H an n x m matrix and q a constant:

    q := 13; R<x> := quo< P | P.1^5 - 1 > where P is PolynomialRing(GF(q));
    H := Int(RepresentationMatrix((x - x^2 + x^4) / (1 + x - x^3)));
    Matrix(<< q, 0 >,          =   [13  .  .  .  .  .  .  .  .  . ]
            < H, 1 >>)             [ . 13  .  .  .  .  .  .  .  . ]
                                   [ .  . 13  .  .  .  .  .  .  . ]
                                   [ .  .  . 13  .  .  .  .  .  . ]
                                   [ .  .  .  . 13  .  .  .  .  . ]
                                   [ . 12  2 11  2  1  .  .  .  . ]
                                   [ 2  . 12  2 11  .  1  .  .  . ]
                                   [11  2  . 12  2  .  .  1  .  . ]
                                   [ 2 11  2  . 12  .  .  .  1  . ]
                                   [12  2 11  2  .  .  .  .  .  1 ]

    NB: uses my util.m Int() intrinsic...

    Rules:
      - the outer tuple stacks its items vertically; a nested tuple joins
        its items horizontally, and directions alternate with depth
      - a sequence [a,b,..] is a row when stacked vertically and a column
        when joined horizontally; a sequence of sequences is a matrix
      - a scalar c is c*I (necessarily square), sized to fit; 0 is a zero
        block of any shape
      - if sizes are otherwise undetermined, block rows of equal length are
        aligned as a grid, and then zero blocks are taken square
      - block sizes are solved for exactly; ambiguity is an error
*/

function Flip(dir)
  return dir eq "V" select "H" else "V";
end function;

function Leaf(x, dir)
  if ISA(Type(x), Mtrx) then
    return <"M", Matrix(x), [Integers()|]>;
  elif ISA(Type(x), ModTupRngElt) then
    return Leaf(Eltseq(x), dir);
  elif ISA(Type(x), SeqEnum) then
    error if #x eq 0, "empty sequence in matrix pattern";
    M := ISA(Type(x[1]), SeqEnum) select Matrix(x)
      else dir eq "V" select Matrix(1, #x, x) else Matrix(#x, 1, x);
    return <"M", M, [Integers()|]>;
  end if;
  return <"S", x, [Integers()|]>;
end function;

/* preorder parse of x, sitting in a join of direction dir (so a tuple
   joins in the other direction) and numbered from i;
   entry i is <"V"|"H", 0, child indices>, <"M", matrix, []> or
   <"S", scalar, []> */
function Parse(x, dir, i)
  if not ISA(Type(x), Tup) then return [* Leaf(x, dir) *]; end if;
  error if #x eq 0, "empty tuple in matrix pattern";
  subs := [* *];
  kids := [Integers()|];
  for y in TupleToList(x) do
    Append(~kids, i + 1 + #subs);
    subs cat:= Parse(y, Flip(dir), i + 1 + #subs);
  end for;
  return [* <Flip(dir), 0, kids> *] cat subs;
end function;

/* unknowns: Nrows of entry i is variable Rv(i), Ncols is Cv(i);
   an equation is < [<var, coeff>], rhs > */
function Rv(i) return 2*i - 1; end function;
function Cv(i) return 2*i; end function;

function Equations(F)
  eqs := [* *];
  for i in [1..#F] do
    kind, data, kids := Explode(F[i]);
    case kind:
      when "M":
        eqs cat:= [* <[<Rv(i), 1>], Nrows(data)>, <[<Cv(i), 1>], Ncols(data)> *];
      when "S":
        if data ne 0 then
          eqs cat:= [* <[<Rv(i), 1>, <Cv(i), -1>], 0> *];
        end if;
      when "V":
        eqs cat:= [* <[<Cv(k), 1>, <Cv(i), -1>], 0> : k in kids *];
        eqs cat:= [* <[<Rv(i), 1>] cat [<Rv(k), -1> : k in kids], 0> *];
      when "H":
        eqs cat:= [* <[<Rv(k), 1>, <Rv(i), -1>], 0> : k in kids *];
        eqs cat:= [* <[<Cv(i), 1>] cat [<Cv(k), -1> : k in kids], 0> *];
    end case;
  end for;
  return eqs;
end function;

/* solve x*A = W; returns consistency, a solution, nullity */
function Solve(eqs, nvars)
  Q := Rationals();
  A := Matrix(Q, nvars, #eqs, [<v[1], e, v[2]> : v in eqs[e][1], e in [1..#eqs]]);
  W := Vector(Q, [Q | e[2] : e in eqs]);
  ok, x, K := IsConsistent(A, W);
  return ok, ok select x else W, ok select Dimension(K) else -1;
end function;

/* optional constraint groups, in priority order: first align the block
   columns of stacked block rows (and dually the block rows of joined
   block columns), then make zero blocks square */
function SoftEquations(F)
  grids := [* *];
  for i in [1..#F] do
    kind, _, kids := Explode(F[i]);
    if kind in {"V", "H"} and forall{k : k in kids | F[k][1] eq Flip(kind)} then
      cells := [F[k][3] : k in kids];
      if #{#x : x in cells} eq 1 then
        v := kind eq "V" select Cv else Rv;
        grids cat:= [* [* <[<v(x[m]), 1>, <v(cells[1][m]), -1>], 0>
                        : x in cells[2..#cells], m in [1..#cells[1]] *] *];
      end if;
    end if;
  end for;
  zeros := [* [* <[<Rv(i), 1>, <Cv(i), -1>], 0> *]
           : i in [1..#F] | F[i][1] eq "S" and F[i][2] eq 0 *];
  return grids cat zeros;
end function;

function Sizes(F)
  n := 2 * #F;
  eqs := Equations(F);
  ok, x, nullity := Solve(eqs, n);
  error if not ok, "inconsistent block sizes in matrix pattern";

  for group in SoftEquations(F) do
    if nullity eq 0 then break; end if;
    trial := eqs cat group;
    ok, y, k := Solve(trial, n);
    if ok then eqs := trial; x := y; nullity := k; end if;
  end for;

  error if nullity ne 0, "block sizes in matrix pattern are underdetermined";
  sz := Eltseq(x);
  error if exists{s : s in sz | not IsIntegral(s) or s lt 0},
    "block sizes in matrix pattern are not non-negative integers";
  sz := ChangeUniverse(sz, Integers());
  return [<sz[Rv(i)], sz[Cv(i)]> : i in [1..#F]];
end function;

function Build(F, sz, R, i)
  kind, data, kids := Explode(F[i]);
  nr, nc := Explode(sz[i]);
  case kind:
    when "M":
      M := ChangeRing(data, R);
    when "S":
      M := data eq 0 select ZeroMatrix(R, nr, nc) else ScalarMatrix(R, nr, R ! data);
    when "V":
      M := ZeroMatrix(R, 0, nc);
      for k in kids do M := VerticalJoin(M, Build(F, sz, R, k)); end for;
    when "H":
      M := ZeroMatrix(R, nr, 0);
      for k in kids do M := HorizontalJoin(M, Build(F, sz, R, k)); end for;
  end case;
  assert Nrows(M) eq nr and Ncols(M) eq nc;
  return M;
end function;

function Pattern(R, rows)
  F := Parse(rows, "H", 1);
  if Type(R) eq BoolElt then
    R := Universe([ f[1] eq "M" select BaseRing(f[2]) ! 0 else f[2]
                  : f in F | f[1] in {"M", "S"} ]);
  end if;
  return Build(F, Sizes(F), R, 1);
end function;

intrinsic Matrix(rows::Tup) -> Mtrx
  { (IMR) Build a matrix from a pattern of rows, blocks and scalars. }
  return Pattern(false, rows);
end intrinsic;

intrinsic Matrix(R::Rng, rows::Tup) -> Mtrx
  { (IMR) Build a matrix over R from a pattern of rows, blocks and scalars. }
  return Pattern(R, rows);
end intrinsic;
