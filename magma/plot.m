
function getsize (:adjust:=0)
    TERM := GetEnvironmentValue("TERM");
    TERM := TERM ne "" select TERM else "xterm"; // assume xterm compatible if TERM not set
    rows,cols := Explode(StringToIntegerSequence(Pipe("tput -T " cat TERM cat " lines cols", "")));
    return rows-adjust, cols-adjust;
end function;

zz := "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ#";
procedure inc(~z)
    z := z notin zz select zz[1] else zz[Min(#zz,Index(zz,z)+1)];
end procedure;

intrinsic plot (ys::SeqEnum : color:=true, adjust:=1)
   { Plot a sequence of values. }

    mx := [3,3]; // left margin, right margin, including frame line
    my := [3,3]; // bottom margin, top margin, including frame line

    r,c := getsize(:adjust := adjust); // reduce size to avoid line splitting
    c -:= &+mx; r -:= &+my;       // adjust plot area by margins

    if Type(ys[1]) eq Tup then
        xs := [ z[1] : z in ys ];
        ys := [ z[2] : z in ys ];
    else
        xs := [1..#ys];
    end if;

    dx := Max(1, (Max(xs) - Min(xs))) / 20; // additional internal plot margin
    dy := Max(1, (Max(ys) - Min(ys))) / 20; // ditto

    x0,x1,y0,y1 := Explode([Min(xs)-dx, Max(xs)+dx, Min(ys)-dy, Max(ys)+dy]);

    xx := func< x | mx[1] + Round((x-x0) * c / (x1-x0)) >;
    yy := func< y | my[1] + Round((y-y0) * r / (y1-y0)) >;

    /* +----------------------frame------------------+
       |............margin...........................|
       |...                                        ..|
       |...            plot area: r x c            ..|
       |...                                        ..|
       |.............................................|
       |.............................................|
       +---------------------------------------------+ */

    s := [
        [ "+", "-" ^^ (c+&+mx-2), "+" ],              // bottom frame
        [ "|", " " ^^ (c+&+mx-2), "|" ] ^^ (my[1]-1), // bottom margin
        [ "|", " " ^^ (mx[1]-1), " " ^^ c, " " ^^ (mx[2]-1), "|" ] ^^ r,
        [ "|", " " ^^ (c+&+mx-2), "|" ] ^^ (my[2]-1), // top margin
        [ "+", "-" ^^ (c+&+mx-2), "+" ]               // top frame
    ];

    for i in [1..#xs] do
        x := xs[i];
        y := ys[i];
        inc(~s[yy(y)][xx(x)]);
    end for;

    if color then
        ESC := CodeToString(27);
        RED := ESC cat "[31m";
        RST := ESC cat "[0m";
        for i in [1..#xs] do
            x := xs[i];
            y := ys[i];
            if #s[yy(y)][xx(x)] eq 1 then
                s[yy(y)][xx(x)] := RED cat s[yy(y)][xx(x)] cat RST;
            end if;
        end for;
    end if;

    old_cols := GetColumns();
    SetColumns(0); // using console codes, so can't rely on auto-line-breaking

    for row in Reverse(s) do
        printf "%o\n", &cat row;
    end for;

    SetColumns(old_cols);

end intrinsic;

intrinsic plot(i::RngIntElt)
    { Examples }
    cmd := "";
    case i:
    when 1:
        args := "[(x/10+5)*(x/10+3)*(x/10-1)*(x/10-2)*(x/10-3.) : x in [-30..30]]";
    else
        error "unknown example number in plot()";
    end case;
    print "// plot(" cat args cat ");";
    _ := eval "plot(" cat args cat " : adjust:=4); return 1;";
end intrinsic;
