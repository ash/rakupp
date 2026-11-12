# Regression (#254): a character class assertion ends at its `>`, and only a
# `+`/`-` member or a comment may come between the class and that `>`.
# `m/<-[W]*/` — the `>` left out — matched "Hello " as if it were there, where
# Rakudo refuses the pattern at compile time ("couldn't find final '>'"); so
# did `m/<[a]/`, `rx/…/`, `m{…}`, a `regex` declaration and `s/…/…/`. A bare
# `/<[a]/` was refused, but as a `/` never found: the open `<` hid it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub refused(Str $code, $type, $msg) {
    try EVAL $code;
    if !$! { @fail.push("{$code.raku} compiled") }
    else {
        @fail.push("{$code.raku}: {$!.^name}") unless $!.^name eq $type;
        @fail.push("{$code.raku}: {$!.message.lines.head}") unless $!.message.starts-with($msg);
    }
}
sub compiles(Str $code, $want) {
    my $got = try EVAL $code;
    @fail.push("{$code.raku}: {$! ?? $!.message.lines.head !! $got.raku}, want {$want.raku}")
        unless !$! && $got eqv $want;
}
my $goal = q[Unable to parse expression in metachar:sym<assert>; couldn't find final '>'];

refused q[my $s = "Hello World"; $s ~~ m/<-[W]*/], 'X::Comp::FailGoal', $goal;
refused q["ab" ~~ /<-[W]*/],          'X::Comp::FailGoal', $goal;
refused q["ab" ~~ m/<[a]/],           'X::Comp::FailGoal', $goal;
refused q["ab" ~~ /<[a]/],            'X::Comp::FailGoal', $goal;
refused q["ab" ~~ rx/<[a]*/],         'X::Comp::FailGoal', $goal;
refused q["ab" ~~ m{<[a]*}],          'X::Comp::FailGoal', $goal;
refused q[my regex r { <[a]* }; 1],   'X::Comp::FailGoal', $goal;
refused q["ab".subst(/<[a]*/, "x")],  'X::Comp::FailGoal', $goal;
refused q[my $s = "ab"; $s ~~ s/<[a]*/x/], 'X::Comp::FailGoal', $goal;
refused q["ab" ~~ m/<[a]-[b]*/],      'X::Comp::FailGoal', $goal;
refused q["ab" ~~ m/<[a] b/],         'X::Comp::AdHoc', 'Missing + or - between character class elements';

compiles q[~("ab" ~~ m/<[a] + [b]>+/)], 'ab';
compiles q[~("ab" ~~ m/<[a] + alpha>+/)], 'ab';
compiles qq[~("ab" ~~ m/<[a]\n>+/)], 'a';
compiles qq[~("ab" ~~ /<[a] # a comment\n >+/)], 'a';
compiles q[~("ab" ~~ /<!before <[cd]>>a/)], 'a';
compiles q[~("ab" ~~ /<?[a]>./)], 'a';
compiles q[so "a+b" ~~ /<before a+[b] \+ >/], False;
compiles q[my $s = "ab"; $s ~~ s:g/<[a]>/x/; $s], 'xb';
compiles qq{my \$s = "ab"; \$s ~~ s/a # <[ not a class\n/x/; \$s}, 'xb';
compiles q{my $s = "a<b"; $s ~~ s/"<[" | b/x/; $s}, 'a<x';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
