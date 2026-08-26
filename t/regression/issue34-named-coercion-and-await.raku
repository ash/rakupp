# Issue #34, the last two failures on the way to a working `install TAP`.
#
# 1. A COERCION TYPE on a NAMED parameter (`IO:D() :$cwd = $*CWD`) was never
#    applied — the positional arm had always coerced, the named arm had not, and
#    the default was not coerced either. In a multi it was worse: the candidate
#    was type-REJECTED outright. TAP's SourceHandler chain passes `:cwd` down
#    through three candidates that all declare it that way, so nothing matched
#    and the dispatch bounced up the role/class chain forever ("Too many levels
#    of recursion" where Rakudo reports a no-match).
#
# 2. `await` on a user class that `does Awaitable` handed back the OBJECT.
#    TAP::Parser is one, delegating `get-await-handle`/`result` to a Promise
#    attribute, so `await $parser` returned the parser and `.tests-planned` blew
#    up on it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- 1. coercion types on named parameters ----------------------------------
class C {
    method one(Str:D $n, IO:D() :$cwd = '/tmp') { $cwd.^name }
    multi method many(Str:D $n, IO:D() :$cwd = '/tmp') { $cwd.^name }
    multi method many(Int:D $n) { 'int' }
}
check C.one('x', :cwd('/usr')),  'IO::Path', 'a supplied named is coerced';
check C.one('x'),                'IO::Path', '…and so is the default';
check C.many('x', :cwd('/usr')), 'IO::Path', 'a coercion-typed named does not reject the candidate';
check C.many(1),                 'int',      '…and the sibling candidate is unaffected';

# a coercion-typed named still accepts a value that is ALREADY the target type
check C.one('x', :cwd('/usr'.IO)), 'IO::Path', 'an already-IO argument passes through';

# the positional arm, which always worked, still does
class P { method p(IO:D() $cwd) { $cwd.^name } }
check P.p('/usr'), 'IO::Path', 'a positional coercion type still coerces';

# a NON-coercion named type constraint must still reject
class R { multi method m(Int:D :$k) { 'int' }; multi method m(Str:D :$k) { 'str' } }
check R.m(:k(1)),   'int', 'a plain typed named still dispatches on its type';
check R.m(:k('a')), 'str', 'a plain typed named still dispatches on its type (2)';

# --- and the recursion that mismatch used to cause --------------------------
# `class F does R2` composes R2's candidates into F, so the two groups overlap;
# a no-match used to bounce between them until the recursion guard fired.
role R2 { multi method ms(::?CLASS:U: Str:D $n) { self.new.ms($n) } }
class F does R2 { multi method ms(::?CLASS:D: Int:D $n) { 'ok' } }
my $err = '';
try { F.ms('z'); CATCH { default { $err = .^name } } }
check ($err ne '' && !$err.contains('recursion')), True,
      'an unmatched multi across a role boundary reports no-match, not runaway recursion';

# --- 2. await on a user Awaitable -------------------------------------------
class Parser does Awaitable {
    has Promise $.promise handles <result get-await-handle> = Promise.kept(42);
}
check (await Parser.new), 42, 'await on a user Awaitable resolves its Promise';
check (await Parser.new).WHAT, Int, '…and yields the promise value, not the object';

# await on the things it always took
check (await Promise.kept(7)), 7, 'await on a Promise is unchanged';
check (await start { 5 }),     5, 'await on a start block is unchanged';
check (await Promise.kept(1), Promise.kept(2)).List, (1, 2), 'await on a list is unchanged';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
