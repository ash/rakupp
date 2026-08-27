# Two scoping rules, both found in Path::Finder.
#
#   A private method call needs `self` in scope — OR a routine declared inside
#   the class. `my multi rulify(Path::Finder:D $rule) { $rule!rules }` sits in
#   the class body, has no invocant of its own, and reaches another instance's
#   private method; Rakudo allows a private call anywhere lexically inside the
#   declaring class. Calling one from genuinely outside is still refused.
#
#   A module's exports are published as globals here, so a `use`d module could
#   overwrite a routine the PROGRAM declared itself. Rakudo never does: an
#   import and a declaration of one name is a redeclaration error there, and a
#   name the module did not actually export (a tag the `use` did not ask for) is
#   simply the program's own. Path::Finder's t/lib/PFTest exports a two-argument
#   `unixify` that t/relative.t does not import — and then declares its own
#   one-argument one.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- the private call ---------------------------------------------------------
class Box {
    has @.items;
    method !secret() { return @!items }
    my sub peek(Box:D $b) { return $b!secret }              # a `my sub` in the body
    my multi peek-multi(Box:D $b) { return $b!secret }      # …and a `my multi`
    method via-sub()    { return peek(self) }
    method via-multi()  { return peek-multi(self) }
    method via-other($o) { return $o!secret }               # another instance
    method via-self()   { return self!secret }
}
my $b = Box.new(:items(1, 2));
check Box.new(:items(1,2)).via-sub.List,   (1, 2), 'a private call from a `my sub` in the class body';
check Box.new(:items(1,2)).via-multi.List, (1, 2), '…and from a `my multi`';
check $b.via-other(Box.new(:items(3))).List, (3,), '…and on another instance from a method';
check $b.via-self.List, (1, 2), 'the ordinary self!private call still works';

# …and from OUTSIDE the class it is still refused (EVAL'd: Rakudo refuses it at
# COMPILE time, so it cannot sit in this file's own text)
my $refused = False;
try { EVAL 'class Shut { method !s() { 1 } }; Shut.new!s'; CATCH { default { $refused = True } } }
check $refused, True, 'a private call from outside the class is still refused';

# --- a module export must not clobber the program's own routine --------------
# Needs a real module on disk and a real `use`, so it runs as a child process.
# The module's export sits under a tag the `use` does not ask for, which is the
# PFTest shape: Rakudo therefore never imports it, and the program's own routine
# is a plain declaration rather than a redeclaration.
{
    my $dir = $*TMPDIR.add("rakupp-import-shadow-{$*PID}");
    $dir.mkdir;
    # best-effort: Rakudo leaves a .precomp DIRECTORY in a lib dir it has used
    LEAVE {
        sub nuke($p) { if $p.d { nuke($_) for $p.dir }; try $p.d ?? $p.rmdir !! $p.unlink }
        try nuke($dir);
    }
    $dir.add('ShadowMod.rakumod').spurt(q:to/MOD/);
        unit package ShadowMod;
        sub helper($x) is export(:helper) { "MODULE:$x" }   # NOT in the default group
        MOD
    $dir.add('prog.raku').spurt(q:to/PROG/);
        use ShadowMod;
        sub helper($x) { "MINE:$x" }
        print helper('z');
        PROG
    my $p = run($*EXECUTABLE, '-I', $dir.Str, $dir.add('prog.raku').Str, :out, :err);
    my $got = $p.out.slurp(:close); $p.err.slurp(:close);
    check $got, 'MINE:z', "a module export does not overwrite the program's own routine";
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
