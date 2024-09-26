
intrinsic generator (state, stepper) -> . {X}
  get,set := NewEnv(["state"]);
  set("state",state);
  f := function ()
    state := get("state");
    x,state := stepper(state);
    set("state", state);
    return x;
  end function;
  return f;
end intrinsic;

// generators 'cat' to concat

intrinsic 'cat' (gen1, gen2) -> . {X}
  get,set := NewEnv(["gens"]);
  set("gens", [gen1, gen2]);
  f := function ()
    gens := get("gens");
    while #gens gt 0 do
      x := gens[1]();
      if x cmpne <> then
        return x;
      end if;
      gens := gens[2..#gens];
      set("gens", gens);
    end while;
    return <>;
  end function;
  return f;
end intrinsic;

intrinsic snoob (x) -> . {Same Number Of One Bits (Hacker's Delight).}
  smallest := BitwiseAnd(x, -x);
  if smallest eq 0 then return <>; end if;
  ripple := x + smallest;
  ones := BitwiseXor(x, ripple);
  ones := (ones div 4) div smallest;
  return BitwiseOr(ripple, ones);
end intrinsic;


intrinsic SubsetsGenerator(S::SetIndx) -> . {X}
  get,set := NewEnv(["state","set"]);
  set("state", 0);
  set("set", S);
  return function ()
    i := get("state");
    S := get("set");
    n := #S;
    s := {};
    set("state", i+1);
    return i;
  end function;
end intrinsic;

/*
   g := SubsetsGenerator(units);
   units := {@ &* g() : i in [1..num_units] @};

   [ [ s[i] : i in [1..6] | Intseq(j,2,6)[i] eq 1 ] : j in [0..63] ];

   g := SubsetGenerator(units);
   s := g();
   repeat
     // process s
     u := &* s;
     s := g();
   until s cmpeq <>;


   > g := generator(0, func<i|i lt 10 select i else <>,i+1>);
   > h := generator(17, func<i|i lt 20 select i else <>,i+1>);
   > s := g cat h;
   > [* s() : i in [1..20] *];
   [* 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 17, 18, 19, <>, <>, <>, <>, <>, <>, <> *]


   given n (size of set) and m (index of subset);

     find k such that B(n,k) < m
        top element is n div B(n,k);


*/
