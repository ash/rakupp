# Regression: five gaps found by the first freshness sweep (docs/dev/findings/
# FRESH100-2026-08-20.md), each proven against Rakudo before it was fixed.
#
# 1. `Kernel.hostname` — Sys::Hostname's entire body is
#    `sub hostname() { Kernel.hostname }`, called on the TYPE OBJECT. We had no
#    hostname at all, and the lenient Distro/Kernel/VM accessor answered the
#    kernel's NAME instead, so every machine was called "darwin"/"linux". A
#    silent wrong answer, not an error.
# 2. `my ($key, *@path) = …` — the slurpy marker in a DECLARATION list. It marks
#    what a trailing @ already does there; we refused to parse it (CompUnit::Util).
# 3. `method finalize(\SELF:)` — a sigilless capture as the INVOCANT. The bare
#    colon marks it exactly as it does after `$self:` (FINALIZER).
# 4. `if EXPR -> str $name { }` — a typed pointy parameter on a statement
#    condition. Nothing constrains the binding, but the type has to be consumed
#    or the block never starts (Rakudo-Type-Introspection).
# 5. Statement modifiers CHAIN: only `if`/`unless` re-entered the modifier
#    parser, so `$_ .= Str when Str for @list` (Testo) died. And `f with $x`
#    read `with` as an ARGUMENT to a parenless call, so it called a routine
#    named `with` (Compress::LZString's `output-w with $w-first`).

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# 1 — hostname, on the type object and on $*KERNEL, and not the kernel's name
ok(Kernel.hostname ~~ Str && Kernel.hostname.chars > 0, 'Kernel.hostname answers a name');
ok(Kernel.hostname eq $*KERNEL.hostname, 'the type object and $*KERNEL agree');
unless $*DISTRO.is-win {
    my $uname = run('uname', '-n', :out).out.slurp(:close).trim;
    ok(Kernel.hostname eq $uname, "hostname is uname -n ($uname)");
}

# 2 — a named slurpy in a declaration list takes the rest
my ($key, *@path) = 'a::b::c'.split('::');
ok($key eq 'a' && @path.join(',') eq 'b,c', 'my ($k, *@rest) binds the rest');

# 3 — a sigilless capture as the invocant
class Fin {
    has $.tag = 'x';
    method finalize(\SELF:) { SELF.tag }
}
ok(Fin.new(tag => 'done').finalize eq 'done', '\SELF: is an invocant');

# 4 — a natively typed pointy parameter on a statement condition
my $seen = '';
if 'abc' -> str $s { $seen = $s }
ok($seen eq 'abc', 'if EXPR -> str $s binds');
my $branch = '';
if 0 { } elsif 'e' -> str $s { $branch = $s }
ok($branch eq 'e', 'elsif EXPR -> str $s binds');

# 5 — modifiers chain, and `with` after a parenless call is the modifier
my ($a, $b) = 1, 'x';
$_ .= Str when Str for $a, $b;
ok($a === 1 && $b eq 'x', 'when chains into for');
my $ran = 0;
sub bump() { $ran++ }
my $def = 5;
my $undef;
bump with $def;
bump with $undef;
ok($ran == 1, '`bump with $x` is the modifier, not an argument');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
