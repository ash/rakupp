# Regression: the general fixes behind the Config + XML + Text::Utils +
# Log::Async zef-suite batch (2026-08-05, session 3):
#   1.  a `Str:D` parameter REFUSES an IO::Path (Config's read(Str:D) recursed
#       forever re-dispatching to itself); IO::Path is Cool, does IO, NOT Stringy.
#   2.  object .Str carries the object's identity (isnt $a, $a.clone) while
#       eqv/is-deeply compare objects STRUCTURALLY (a clone eqv its source).
#   3.  `$obj[$i] = v` writes the real element through `return-rw @!arr[$i]`
#       (lvalue-mode method invocation).
#   4.  s/// interpolation escapes whitespace (a ' '-valued var matched EMPTY)
#       and accepts kebab-case names ($comment-char).
#   5.  qqww quote protection + escape processing; `<<:TRACE(1) DEBUG>>`
#       colonpair words are Pairs (and seed enum numbering).
#   6.  open(:nl-in("||")) honoured by .lines.
#   7.  attr defaults evaluate in the DECLARING class's scope (class-body
#       constants visible from another compilation unit's .new).
#   8.  `my $x will leave {…} = v` runs the block at scope exit with $x as topic.
#   9.  a but-mixed Code stays callable; a method wrapped via
#       .^find_method(...).wrap runs the wrapper (self, |args) with callsame;
#       a |c parameter binds a real Capture (named parts reachable).
#   10. cmp-ok's numeric fast path only fires for numerics (Version cmp works).
#   11. DateTime.now carries fractional seconds.
#   12. Exception.throw records a backtrace; frames answer .file/.line.
#   13. "$?LINE" interpolates (with the right line number).
#   14. an enum VALUE is callable like its type: WARNING(TRACE) → TRACE.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. Str:D vs IO::Path dispatch
{
    my class P {}
    my class PN is P {}
    my class C {
        multi method read (%data) { "HASH" }
        multi method read (IO() $path, P:U $parser?) { "IO" }
        multi method read (Str:D $path, P:U $parser?) { "STR->" ~ self.read($path.IO, $parser) }
    }
    @fail.push('io-dispatch') unless C.read('x'.IO, PN) eq 'IO';
    @fail.push('str-dispatch') unless C.read('x', PN) eq 'STR->IO';
    @fail.push('iopath-not-str') if 'x'.IO ~~ Str;
    @fail.push('iopath-not-stringy') if 'x'.IO ~~ Stringy;
    @fail.push('iopath-is-io') unless 'x'.IO ~~ IO;
    @fail.push('version-not-cool') if v1.2 ~~ Cool;
}

# 2. object identity in .Str, structure in eqv
{
    my class K { has $.a }
    my $x = K.new(a => 1);
    my $y = $x.clone;
    @fail.push('clone-str-differs') unless $x.Str ne $y.Str;
    @fail.push('clone-eqv') unless $x eqv $y;
    @fail.push('eqv-attr-differs') if K.new(a => 1) eqv K.new(a => 2);
    my $o = K.new(a => 3);
    @fail.push('pair-obj-key') unless ($o => 1) eqv (K.new(a => 3) => 1);
}

# 3. return-rw element write-back
{
    my class A does Positional {
        has @.arr;
        method AT-POS($i) { return-rw @!arr[$i] }
    }
    my $p = A.new;
    $p[1] = 7;
    $p[0] = 5;
    @fail.push('return-rw-write') unless $p.arr eqv [5, 7];
    $p[1]++;
    @fail.push('return-rw-incr') unless $p.arr[1] == 8;
}

# 4. substitution interpolation: whitespace + kebab-case
{
    my constant $WS = ' ';
    my $s = "a  b   c";
    $s ~~ s:g/ $WS ** 2..* /$WS/;
    @fail.push('subst-ws-var') unless $s eq 'a b c';
    my $comment-char = '#';
    my $line = "# note";
    @fail.push('kebab-interp') unless $line ~~ /^ \h* $comment-char /;
    my $l2 = "x # y";
    $l2 ~~ s/ \h* $comment-char .* //;
    @fail.push('kebab-subst') unless $l2 eq 'x';
}

# 5. qqww + colonpair words
{
    my @le = qqww{ "\n" || 'a b' };
    @fail.push('qqww') unless @le eqv ["\n", "||", "a b"];
    enum LV <<:TRACE(1) DEBUG INFO>>;
    @fail.push('enum-cp-value') unless TRACE.Int == 1 && DEBUG.Int == 2;
    @fail.push('enum-cp-str') unless TRACE.Str eq 'TRACE';
    my @lst = <<:x(5) plain>>;
    @fail.push('anglepair') unless @lst[0] ~~ Pair && @lst[0].value == 5 && @lst[1] eq 'plain';
}

# 6. :nl-in
{
    my $f = $*TMPDIR.add("rakupp-nlin-$*PID.txt");
    $f.spurt("a||b||c");
    my $fh = open $f.Str, :r, :nl-in("||");
    my @l = $fh.lines;
    $fh.close;
    $f.unlink;
    @fail.push('nl-in') unless @l eqv ["a", "b", "c"];
}

# 7. class-body constant visible to attr defaults cross-unit is covered by the
#    module battery; same-unit shape still guards the env chain:
{
    my class G {
        my %H = a => 1;
        has $.g = %H;
    }
    @fail.push('attr-default-scope') unless G.new.g<a> == 1;
}

# 8. will leave
{
    my @ran;
    sub f() {
        my $x will leave { @ran.push($_) } = 42;
        $x;
    }
    f();
    @fail.push('will-leave') unless @ran eqv [42];
}

# 9. wrap + but-Code + capture param
{
    my $fmt = -> $m { "fmt:$m" };
    my $wrapped = $fmt but role { method is-hidden-from-backtrace { True } };
    @fail.push('but-code-call') unless $wrapped("x") eq 'fmt:x';
    my class W { method add($blk, :$opt) { "got(" ~ $blk() ~ ")" } }
    my $cap;
    W.^find_method('add').wrap: -> \s, |q { $cap = q; callsame };
    my $r = W.new.add({ 9 }, opt => 3);
    @fail.push('wrap-ran') unless $r eq 'got(9)';
    @fail.push('wrap-capture-named') unless $cap<opt> == 3;
}

# 10. cmp-ok on Versions (Test-free check: the operator applied by name)
{
    @fail.push('version-gt') unless v0.0.7 > v0.0.0;
    @fail.push('version-cmp') unless (v0.0.7 cmp v0.0.0) eq 'More';
}

# 11. DateTime.now fractional seconds (two samples so a .000000 boundary
#     can't flake: at least one must carry a fraction)
{
    my $a = DateTime.now.Str;
    my $b = DateTime.now.Str;
    @fail.push('dt-frac') unless $a ~~ /'.' \d+/ or $b ~~ /'.' \d+/;
}

# 12. Exception backtrace frames
{
    my $e = Exception.new;
    try $e.throw;
    my @bt = $e.backtrace.list;
    @fail.push('bt-nonempty') unless @bt.elems >= 1;
    @fail.push('bt-file') unless @bt[0].file.Str.chars > 0;
    @fail.push('bt-line') unless @bt[0].line > 0;
}

# 13. $?LINE interpolation
{
    my $s = "at $?LINE";
    my $expect = $?LINE - 1;
    @fail.push("qline ($s)") unless $s eq "at $expect";
}

# 14. enum value callable as its type
{
    enum L2 <A2 B2 C2>;
    my $v = B2;
    @fail.push('enum-call-enum') unless $v(C2) === C2;
    @fail.push('enum-call-int') unless $v(2) === C2;
}

if @fail {
    say "FAILED: @fail[]";
    say "FAIL";
    exit 1;
}
say "PASS";
