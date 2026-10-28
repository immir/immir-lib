
/* Terminal plots.
   Immir, circa Nov 2026,
   AI assisted */

help := "
   Magma terminal plotting library.

     plot(xs : options)              print the plot
     plot(i)                         examples: i=1,2,...
     PlotDefaults(: options)         session defaults for color, axes, width,
                                     height, adjust, hires and lines; returns
                                     the previous ones, so PlotDefaults() shows
                                     them and PlotDefaults(old) restores them

   xs is a sequence of values, a sequence of <x, y> pairs, or a sequence
   (or List) of such sequences, plotted on common axes.  Options:

     color   ANSI colours, one per series     (default: unless NO_COLOR is set)
     axes    tick marks and labels            (true)
     width   total columns, 0 for terminal    (0)
     height  total rows, 0 for terminal       (0)
     adjust  shrink a terminal-sized plot     (1)
     logx    log10 x axis                     (false)
     logy    log10 y axis                     (false)
     xrange  [lo, hi], clipping outside       ([] for automatic)
     yrange  [lo, hi], clipping outside       ([] for automatic)
     names   legend entry per series          ([] for [1], [2], ...)
     title   centred above the plot           (\"\")
     hires   Braille dots, 2 x 4 per cell     (false)
     lines   join consecutive points          (false)

   Without hires, a cell shows how many points hit it (1..9A..Z#), or \".\"
   when it is only crossed by a line.
";

OPTS := recformat< color, axes, width, height, adjust, logx, logy,
                   xrange, yrange, names, title, hires, lines >;

/* session defaults: <name, initial value, type> */
DEFAULTS := [* <"color", GetEnvironmentValue("NO_COLOR") eq "", BoolElt>,
               <"axes", true, BoolElt>, <"width", 0, RngIntElt>, <"height", 0, RngIntElt>,
               <"adjust", 1, RngIntElt>, <"hires", false, BoolElt>, <"lines", false, BoolElt> *];
DEFS := recformat< color, axes, width, height, adjust, hires, lines >;

get, set := NewEnv([ d[1] : d in DEFAULTS ]);
for d in DEFAULTS do set(d[1], d[2]); end for;

function current()
    r := rec< DEFS | >;
    for d in DEFAULTS do r``(d[1]) := get(d[1]); end for;
    return r;
end function;

/* validate and install new defaults (a DEFS record); returns the old ones */
function install(new)
    bad := [ d[1] : d in DEFAULTS | not assigned new``(d[1]) or Type(new``(d[1])) ne d[3] ];
    error if #bad gt 0, "PlotDefaults: bad value for " cat Join(bad, ", ");
    error if new`width lt 0 or new`height lt 0, "PlotDefaults: width and height must be >= 0";
    old := current();
    for d in DEFAULTS do set(d[1], new``(d[1])); end for;
    return old;
end function;

zz := "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ#"; // marks for 1, 2, ... hits
COLORS := [31, 32, 34, 33, 35, 36];          // red green blue yellow magenta cyan
BOLD := 1;                                   // cells hit by more than one series
BITS := [[1, 2, 4, 64], [8, 16, 32, 128]];   // Braille dot bits: [column][row from top]

/* rows, columns of the terminal; 24 x 80 (or Magma's column setting) when
   tput cannot tell, e.g. for an unknown TERM */
function termsize(adjust)
    TERM := GetEnvironmentValue("TERM");
    TERM := TERM ne "" select TERM else "xterm"; // assume xterm compatible if TERM not set
    try
        rc := StringToIntegerSequence(Pipe("tput -T " cat TERM cat " lines cols 2>/dev/null", ""));
    catch e
        rc := [];
    end try;
    rc := #rc eq 2 select rc else [24, GetColumns() gt 0 select GetColumns() else 80];
    return rc[1] - adjust, rc[2] - adjust;
end function;

function points(s)
    return Type(s[1]) eq Tup select s else [ <i, s[i]> : i in [1..#s] ];
end function;

/* a non-empty List of non-empty sequences of <x, y> */
function series(xs)
    error if not ISA(Type(xs), SeqEnum) and not ISA(Type(xs), List),
        "plot needs a sequence or a List";
    error if #xs eq 0, "nothing to plot";
    S := ISA(Type(xs), List) or ISA(Type(xs[1]), SeqEnum) select xs else [* xs *];
    error if exists{s : s in S | #s eq 0}, "nothing to plot";
    return [* points(s) : s in S *];
end function;

/* exact label for n * 10^e: fixed point for 1e-4 <= |value| < 1e5,
   otherwise scientific, e.g. 2e6, 1.5e-7 */
function label(n, e)
    if n eq 0 then return "0"; end if;
    while n mod 10 eq 0 do n div:= 10; e +:= 1; end while;
    sign := n lt 0 select "-" else "";
    a := IntegerToString(Abs(n));
    if e ge 0 and e + #a le 5 then
        return sign cat a cat "0"^e;
    elif e lt 0 and e ge -4 then
        d := -e;
        ip := Abs(n) div 10^d;
        fp := Abs(n) mod 10^d;
        return sign cat IntegerToString(ip) cat "." cat IntegerToString(10^d + fp)[2..d+1];
    end if;
    m := #a eq 1 select a else a[1] cat "." cat a[2..#a];
    return sign cat m cat "e" cat IntegerToString(e + #a - 1);
end function;

/* about n "nice" ticks (multiples of 1, 2 or 5 times a power of 10) in
   [lo, hi], as <value, label> */
function ticks(lo, hi, n)
    raw := (hi - lo) / Max(1, n);
    e := Floor(Log(10, RealField() ! raw));
    s := [ m : m in [1, 2, 5, 10, 20, 50] | m * 10^e ge raw ][1];
    step := s * 10^e;
    return [ <k * step, label(k * s, e)> : k in [Ceiling(lo / step)..Floor(hi / step)] ];
end function;

/* ticks for a log10 axis spanning [lo, hi] (in log10 units), as <log10 value,
   label>: powers of 10 at nice exponents when at least two fit, else 1, 2,
   5 times powers of 10, else linear ticks */
function logticks(lo, hi, n)
    ks := [ Integers() ! t[1] : t in ticks(lo, hi, n) | IsIntegral(t[1]) ];
    if #ks ge 2 then
        return [ <k, label(1, k)> : k in ks ];
    end if;
    lg := func< v | Log(10, RealField() ! v) >;
    ts := [ <lg(m * 10^k), label(m, k)> : m in [1, 2, 5], k in [Floor(lo)..Ceiling(hi)]
          | lg(m * 10^k) ge lo and lg(m * 10^k) le hi ];
    return #ts ge 2 select ts
        else [ <lg(t[1]), t[2]> : t in ticks(10^lo, 10^hi, n) | t[1] gt 0 ];
end function;

/* the part t0 <= t <= t1 of the segment p + t(q - p), 0 <= t <= 1, inside
   the box lo <= z <= hi (Liang-Barsky) */
function clip(p, q, lo, hi)
    t0 := 0; t1 := 1;
    for k in [1, 2] do
        d := q[k] - p[k];
        if d eq 0 then
            if p[k] lt lo[k] or p[k] gt hi[k] then return false, 0, 0; end if;
        else
            a := (lo[k] - p[k]) / d;
            b := (hi[k] - p[k]) / d;
            t0 := Max(t0, Min(a, b));
            t1 := Min(t1, Max(a, b));
        end if;
    end for;
    return t0 le t1, t0, t1;
end function;

/* integer dots along the segment pq, clipped to [0, W-1] x [0, H-1] */
function segment(p, q, W, H)
    ok, t0, t1 := clip(p, q, [0, 0], [W - 1, H - 1]);
    if not ok then return []; end if;
    n := Max(1, Ceiling(Max(Abs(q[1] - p[1]), Abs(q[2] - p[2])) * (t1 - t0)));
    at := func< t | <Round(p[1] + t * (q[1] - p[1])), Round(p[2] + t * (q[2] - p[2]))> >;
    return [ at(t0 + i * (t1 - t0) / n) : i in [0..n] ];
end function;

function render(series, o)

    error if #o`names notin {0, #series}, "need one name per series";
    legend := o`color and (#series gt 1 or #o`names gt 0);
    titled := o`title ne "";
    sx, sy := Explode(o`hires select [2, 4] else [1, 1]); // dots per cell

    mx := [3,3]; // left margin, right margin, including frame line
    my := [3,3]; // bottom margin, top margin, including frame line

    rows, cols := termsize(o`adjust); // reduce size to avoid line splitting
    rows := o`height gt 0 select o`height else rows;
    cols := o`width gt 0 select o`width else cols;
    r := rows - &+my - (o`axes select 1 else 0) - (legend select 1 else 0) - (titled select 1 else 0);

    xs := [ p[1] : p in S, S in series ] cat o`xrange;
    ys := [ p[2] : p in S, S in series ] cat o`yrange;
    error if o`logx and exists{x : x in xs | x le 0}, "logx needs positive x values and range";
    error if o`logy and exists{y : y in ys | y le 0}, "logy needs positive y values and range";
    lg := func< v | Log(10, RealField() ! v) >;
    tx := o`logx select lg else func< x | x >;
    ty := o`logy select lg else func< y | y >;
    P := [* [ <tx(p[1]), ty(p[2])> : p in S ] : S in series *];

    // additional internal plot margin: 5% of the range, or 1/20 if constant
    pad := func< vs | (Max(vs) eq Min(vs) select 1 else Max(vs) - Min(vs)) / 20 >;
    bounds := func< vs, range, t | #range eq 2 select [t(range[1]), t(range[2])]
                                   else [Min(vs) - pad(vs), Max(vs) + pad(vs)] >;
    error if #o`xrange notin {0, 2} or #o`yrange notin {0, 2}, "ranges are [lo, hi]";
    x0, x1 := Explode(bounds([ p[1] : p in S, S in P ], o`xrange, tx));
    y0, y1 := Explode(bounds([ p[2] : p in S, S in P ], o`yrange, ty));
    error if x0 ge x1 or y0 ge y1, "ranges need lo < hi";

    tick := func< lo, hi, n, logged | not o`axes select [] else
                  logged select logticks(lo, hi, n) else ticks(lo, hi, n) >;
    yticks := tick(y0, y1, r div 3, o`logy);
    lw := #yticks eq 0 select 0 else Max([ #t[2] : t in yticks ]) + 1; // y labels + space
    c := cols - &+mx - lw;
    error if r le 0 or c le 0, "too small to plot: increase width/height, or reduce adjust";
    xticks := tick(x0, x1, c div 12, o`logx);

    // the plot area is (r+1) x (c+1) cells of sy x sx dots
    W := (c + 1) * sx;
    H := (r + 1) * sy;
    fx := func< x | (x - x0) * (W - 1) / (x1 - x0) >;
    fy := func< y | (y - y0) * (H - 1) / (y1 - y0) >;
    xx := func< x | mx[1] + Round(fx(x)) div sx >;
    yy := func< y | my[1] + Round(fy(y)) div sy >;
    cell := func< d | <my[1] + d[2] div sy, mx[1] + d[1] div sx> >;
    inside := func< d | d[1] ge 0 and d[1] lt W and d[2] ge 0 and d[2] lt H >;

    F := [* [ <fx(p[1]), fy(p[2])> : p in S ] : S in P *];
    raw := [* [ d : d in [ <Round(p[1]), Round(p[2])> : p in S ] | inside(d) ] : S in F *];
    crossed := [* not o`lines select [] else
                  [ d : d in segment(S[i], S[i+1], W, H), i in [1..#S-1] ] : S in F *];

    /*       +----------------------frame------------------+
             |............margin...........................|
             |...                                        ..|
         2e3 -...            plot area: r x c            ..|
             |...                                        ..|
             |.............................................|
             |.............................................|
             +---------|---------------|---------------|---+
                      10              20              30       */

    w := c + &+mx;
    h := r + &+my;
    frame := [ "+", "-" ^^ (w-2), "+" ];
    blank := [ "|", " " ^^ (w-2), "|" ];
    s := [ frame, blank ^^ (h-2), frame ]; // s[1] is the bottom row
    left := [ " "^lw : _ in [1..h] ];

    for t in yticks do
        s[yy(t[1])][1] := "-";
        left[yy(t[1])] := " "^(lw - 1 - #t[2]) cat t[2] cat " ";
    end for;
    for t in xticks do
        s[1][xx(t[1])] := "|";
    end for;

    ESC := CodeToString(27);
    paint := func< code, z | o`color select ESC cat "[" cat IntegerToString(code) cat "m"
                                               cat z cat ESC cat "[0m"
                                       else z >;
    hue := func< k | COLORS[1 + (k - 1) mod #COLORS] >;

    owners := [ { cell(d) : d in raw[k] cat crossed[k] } : k in [1..#series] ];
    if o`hires then
        braille := func< m | CodeToString(226) cat CodeToString(160 + m div 64)
                                               cat CodeToString(128 + m mod 64) >;
        bit := func< d | BITS[1 + d[1] mod sx][sy - d[2] mod sy] >;
        masks := AssociativeArray();
        for cb in { <cell(d), bit(d)> : d in raw[k] cat crossed[k], k in [1..#series] } do
            masks[cb[1]] := (IsDefined(masks, cb[1]) select masks[cb[1]] else 0) + cb[2];
        end for;
        glyph := func< p | braille(masks[p]) >;
    else
        hits := {* cell(d) : d in raw[k], k in [1..#series] *};
        glyph := func< p | p in hits select zz[Min(#zz, Multiplicity(hits, p))] else "." >;
    end if;
    for p in &join owners do
        ks := [ k : k in [1..#owners] | p in owners[k] ];
        s[p[1]][p[2]] := paint(#ks eq 1 select hue(ks[1]) else BOLD, glyph(p));
    end for;

    /* x labels centred under their ticks, dropping any that would collide */
    under := " "^(lw + w);
    used := 0;
    for t in xticks do
        start := lw + xx(t[1]) - #t[2] div 2;
        if start gt used + 1 and start + #t[2] - 1 le #under then
            under := under[1..start-1] cat t[2] cat under[start + #t[2]..#under];
            used := start + #t[2] - 1;
        end if;
    end for;

    name := func< k | #o`names gt 0 select Sprint(o`names[k]) else "[" cat IntegerToString(k) cat "]" >;
    return (titled select [ " "^Max(0, (lw + w - #o`title) div 2) cat o`title ] else [])
         cat [ left[i] cat &cat s[i] : i in [h..1 by -1] ]
         cat (o`axes select [ under ] else [])
         cat (legend select [ &cat [ " " cat paint(hue(k), name(k)) : k in [1..#series] ]
                              cat (#series gt 1 select "  " cat paint(BOLD, "overlap") else "") ]
                     else []);

end function;

intrinsic PlotDefaults (: color := get("color"), axes := get("axes"), width := get("width"),
                          height := get("height"), adjust := get("adjust"),
                          hires := get("hires"), lines := get("lines")) -> Rec
   { Set session defaults for plot and plotlines; returns the previous ones,
     so PlotDefaults() shows them and PlotDefaults(old) restores them. }
    return install(rec< DEFS | color := color, axes := axes, width := width, height := height,
                               adjust := adjust, hires := hires, lines := lines >);
end intrinsic;

intrinsic PlotDefaults (old::Rec) -> Rec
   { Restore defaults returned by an earlier PlotDefaults call. }
    require Format(old) cmpeq DEFS : "PlotDefaults needs a record it returned earlier";
    return install(old);
end intrinsic;

intrinsic plotlines (xs : color := get("color"), axes := get("axes"),
                          width := get("width"), height := get("height"), adjust := get("adjust"),
                          logx := false, logy := false,
                          xrange := [], yrange := [], names := [], title := "",
                          hires := get("hires"), lines := get("lines")) -> SeqEnum
   { The plot of xs as a sequence of strings; see plot. }
    return render(series(xs), rec< OPTS | color := color, axes := axes, width := width,
                  height := height, adjust := adjust, logx := logx, logy := logy,
                  xrange := xrange, yrange := yrange, names := names, title := title,
                  hires := hires, lines := lines >);
end intrinsic;

intrinsic plot (xs : color := get("color"), axes := get("axes"),
                     width := get("width"), height := get("height"), adjust := get("adjust"),
                     logx := false, logy := false,
                     xrange := [], yrange := [], names := [], title := "",
                     hires := get("hires"), lines := get("lines"))
   { Plot a sequence of values, or of <x, y> pairs; or a sequence (or List)
     of such sequences, on common axes.  Options: see the head of plot.m. }
    L := plotlines(xs : color := color, axes := axes, width := width, height := height,
                   adjust := adjust, logx := logx, logy := logy, xrange := xrange,
                   yrange := yrange, names := names, title := title, hires := hires,
                   lines := lines);
    old_cols := GetColumns();
    SetColumns(0); // using console codes, so can't rely on auto-line-breaking
    try
        for l in L do printf "%o\n", l; end for;
    catch e
        SetColumns(old_cols);
        error e;
    end try;
    SetColumns(old_cols);
end intrinsic;

EXAMPLES := [
    < "a polynomial",
      "[(x/10+5)*(x/10+3)*(x/10-1)*(x/10-2)*(x/10-3.) : x in [-30..30]]", "" >,
    < "more points than columns: marks count hits per cell",
      "[Sqrt(x) : x in [1..500]]", "" >,
    < "<x, y> pairs: a Lissajous curve",
      "[<Sin(t/50.), Sin(2*t/50.)> : t in [0..315]]", "" >,
    < "real points of y^2 = x^3 - x: an oval and an unbounded branch",
      "[<x, s*Sqrt(x^3 - x)> : x in [-6/5 + k/50 : k in [0..170]], s in [1,-1] | x^3 - x ge 0]", "" >,
    < "the same curve over GF(101), centred lifts: symmetric under y -> -y",
      "[<Integers()!(P[1] + 50) - 50, Integers()!(P[2] + 50) - 50> : P in Points(EllipticCurve([GF(101)|-1,0])) | P[3] eq 1]", "" >,
    < "two series: sin and cos",
      "[[Sin(x/10.) : x in [0..125]], [Cos(x/10.) : x in [0..125]]]",
      "names := [\"sin\", \"cos\"], title := \"sin and cos\"" >,
    < "series of different types in a List: pi(x) against x/log(x)",
      "[* [<x, #PrimesUpTo(x)> : x in [10..5000 by 50]], [<x, x/Log(x)> : x in [10..5000 by 50]] *]",
      "names := [\"pi(x)\", \"x/log(x)\"]" >,
    < "three series: partial Fourier sums of a square wave",
      "[[&+[Sin((2*j-1)*x/20.)/(2*j-1) : j in [1..k]] : x in [0..250]] : k in [1, 2, 8]]", "" >,
    < "tiny range: labels in scientific notation",
      "[<k*1e-7, Exp(-k/30.)*1e-6> : k in [0..100]]", "" >,
    < "log scale: 2^n, n! and n^n",
      "[[<n, 2^n> : n in [1..40]], [<n, Factorial(n)> : n in [1..40]], [<n, n^n> : n in [1..40]]]",
      "logy := true, lines := true, names := [\"2^n\", \"n!\", \"n^n\"]" >,
    < "log-log: pi(x) is nearly a power law",
      "[<x, #PrimesUpTo(x)> : x in [Round(10^(k/4.)) : k in [4..24]]]",
      "logx := true, logy := true, lines := true" >,
    < "Braille dots and lines: the Lissajous curve again",
      "[<Sin(t/50.), Sin(2*t/50.)> : t in [0..315 by 5]]", "hires := true, lines := true" >,
    < "zoom with clipping: the oval of y^2 = x^3 - x",
      "[<x, s*Sqrt(x^3 - x)> : x in [-6/5 + k/200 : k in [0..680]], s in [1,-1] | x^3 - x ge 0]",
      "xrange := [-1.2, 0.2], yrange := [-0.6, 0.6], hires := true" >
];

intrinsic plot(i::RngIntElt)
    { Examples: plot(1), ..., plot(13) }
    require i in [1..#EXAMPLES] :
        "unknown example number in plot(); try 1 to " cat IntegerToString(#EXAMPLES);
    desc, args, opts := Explode(EXAMPLES[i]);
    print "// " cat desc;
    print "// plot(" cat args cat (opts eq "" select "" else " : " cat opts) cat ");";
    _ := eval "plot(" cat args cat " : adjust:=4" cat (opts eq "" select "" else ", " cat opts) cat "); return 1;";
end intrinsic;

intrinsic plot()
    { Run plot() for help. }
    print help;
end intrinsic;
