# Method calls inside a kernel (copy-and-patch), as sub calls are: the loop's
# variables are written back before the call and reloaded after it, and the
# call runs through the interpreter with the invocant under its own sigil
# (`@a.elems` stays an array's method); a pure builtin method on a non-object
# goes straight to the runtime's method dispatch. `last` from a method leaves
# the loop. The interpreter is the oracle; checked against Rakudo 2026.09.
{ my @a = 1..10; my $s = 0; my $i = 0; while $i < @a.elems { $s = $s + @a[$i]; $i = $i + 1 }; say "M1 $s" }
{ my @a; my $i = 0; while $i < 10 { @a.push($i * $i); $i = $i + 1 }; say "M2 {@a.raku}" }
{ my @a = 1..5; my $s = 0; while @a.elems > 0 { $s = $s + @a.pop }; say "M3 $s {@a.elems}" }
{ my $s = 0e0; my $i = -50; while $i < 50 { $s = $s + $i.abs + $i.abs.sqrt; $i = $i + 1 }; say "M4 $s" }
{ my $t = "hello world"; my $n = 0; my $i = 0; while $i < $t.chars { $n = $n + 1 if $t.substr($i, 1) eq "o"; $i = $i + 1 }; say "M5 $n" }
{ my @a = 1, Any, 3, Any; my $d = 0; my $i = 0; while $i < 4 { $d = $d + 1 if @a[$i].defined; $i = $i + 1 }; say "M6 $d" }
class Counter { has $.n = 0; method bump($k) { $!n = $!n + $k; self }; method stop($i) { last if $i > 5; $i } }
{ my $c = Counter.new; my $i = 0; while $i < 10 { $c.bump($i); $i = $i + 1 }; say "M7 {$c.n}" }
{ my $c = Counter.new; my $s = 0; my $i = 0; while $i < 10 { $s = $s + $c.stop($i); $i = $i + 1 }; say "M8 s=$s i=$i" }
{ my %h = a => 1, b => 2, c => 3; my $s = 0; my $i = 0; while $i < 3 { $s = $s + %h.elems; $i = $i + 1 }; say "M9 $s" }
{ my $w = "a,b,c"; my $n = 0; my $i = 0; while $i < 3 { $n = $n + $w.split(",").elems; $i = $i + 1 }; say "M10 $n" }
{ my @objs = Counter.new xx 3; my $i = 0; while $i < 3 { my $o = @objs[$i]; $o.bump($i + 1); $i = $i + 1 }; say "M11 ", @objs.map(*.n).join(",") }
