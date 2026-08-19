# Regression: a multi-match form that matches nothing answers with an EMPTY LIST,
# not Nil. `("abc" ~~ m:g/z/).elems` was 1 here and 0 in Rakudo, because the
# smartmatch collapsed every falsy match result to Nil — and Nil answers 1 to
# .elems. Both are falsy, so only code that counts or iterates could see it.
# Found by the pre-3.5 review while measuring regex allocation.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $g  = ("abc" ~~ m:g/z/);
my $ex = ("abc" ~~ m:ex/z/);
check $g.^name,  'List', 'an empty :g match is a List';
check $g.elems,  0,      '…with no elements';
check $ex.^name, 'List', 'an empty :ex match is a List';
check $ex.elems, 0,      '…with no elements';
check ?$g,       False,  'and it is still falsy';
check ("a1b2" ~~ m:g/\d/).elems, 2, 'a :g match that matches still counts';

my $s = "abc";
my $sub = ($s ~~ s:g/z/x/);
check $sub.^name, 'List', 'an empty s:g/// answers with a List too';
check $sub.elems, 0,      '…with no elements';

# a plain single match is unchanged: falsy, and not a list
check (("abc" ~~ m/z/) ?? 1 !! 0), 0, 'a failed single match stays falsy';
check ("abc" ~~ m/b/).Str, 'b',       'a successful single match still yields the Match';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
