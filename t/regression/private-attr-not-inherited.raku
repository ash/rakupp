# Regression: a private attribute is per-class and is never INHERITED, so `$!x`
# in a child naming a parent's attribute is undeclared.
#
# rakupp already refused an undeclared `$!x` in a class method (X::Attribute::
# Undeclared, since the typed-diagnostics campaign), but the walk that decided
# "declared" climbed the whole PARENT CHAIN. So a child reaching its parent's
# storage through `$!parent-attr` compiled here and failed to compile on Rakudo
# — silent, and in the direction that produces non-portable source. It also
# swallowed typos: any undeclared `$!x` in a subclass bound quietly to an
# ancestor's attribute of that name. Only the public accessor (`self.attr`)
# crosses the class boundary.
#
# The other half of the pair is the asymmetry that makes the parent walk look
# right: rakupp puts the FIRST `does` role in the parent slot (the rest become
# extra parents, and their attributes are flattened into the composer). A role's
# attributes DO belong to the composer and must stay reachable through `$!`.
# Both directions are here on purpose — a change that repairs either one by
# breaking the other is the failure mode.
#
# Every expectation was checked against Rakudo.

my $fails = 0;

# Each row compiles in its OWN process: a compile error aborts a whole program,
# so they cannot share one. `accepts` also pins the VALUE read back, so a row
# cannot pass by reaching some other class's storage.
sub accepts($src, $want, $desc) {
    my $p = run($*EXECUTABLE, '-e', $src, :out, :err);
    my $got = $p.out.slurp(:close).lines.head // '';
    my $err = $p.err.slurp(:close);
    if $p.exitcode == 0 && $got eq $want { say "ok - $desc" }
    else { $fails++; note "FAIL: $desc — exit={$p.exitcode} out='$got' want='$want' err={$err.lines.head // ''}" }
}
sub refuses($src, $desc) {
    my $p = run($*EXECUTABLE, '-e', $src, :out, :err);
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    if $p.exitcode != 0 && $err.contains('not declared in class') { say "ok - $desc" }
    else { $fails++; note "FAIL: $desc — exit={$p.exitcode} err={$err.lines.head // '(none)'}" }
}

# --- refused: a private attribute does not cross a real class boundary -------
refuses 'class P1 { has $!x = "p1"; }; class C1 is P1 { method m { $!x } }; say C1.new.m;',
        "a parent's attribute via \$! is undeclared in the child";
refuses 'class G2 { has $!x = "g2"; }; class M2 is G2 { }; class C2 is M2 { method m { $!x } }; say C2.new.m;',
        "a grandparent's attribute via \$! is undeclared";
refuses 'role R3 { }; class P3 { has $!x = "p3"; }; class C3 is P3 does R3 { method m { $!x } }; say C3.new.m;',
        "composing a role does not open the parent's attributes";
refuses 'class C4 { has $!x = "c4"; method m { $!nosuch } }; say C4.new.m;',
        'a plain undeclared $! is still refused';

# --- accepted: own, and everything a role composed in ------------------------
accepts 'class A5 { has $!x = "own5"; method m { $!x } }; say A5.new.m;',
        'own5', "a class's own attribute";
accepts 'role R6 { has $!x = "r6"; }; class C6 does R6 { method m { $!x } }; say C6.new.m;',
        'r6', "the first `does` role's attribute (it arrives as the parent)";
accepts 'role RA7 { }; role RB7 { has $!x = "r7"; }; class C7 does RA7 does RB7 { method m { $!x } }; say C7.new.m;',
        'r7', "a later `does` role's attribute (flattened into the composer)";
accepts 'role RI8 { has $!x = "r8"; }; role RO8 does RI8 { }; class C8 does RO8 { method m { $!x } }; say C8.new.m;',
        'r8', 'a role composed through another role';
accepts 'role R9 { has $!y = "r9"; }; class P9 { has $!x = "p9"; }; class C9 is P9 does R9 { method m { $!y } }; say C9.new.m;',
        'r9', "a role's attribute stays reachable when there is also a parent class";
accepts 'role R10 { has $!x; submethod BUILD { $!x = "r10" }; method m { $!x } }; class C10 does R10 { }; say C10.new.m;',
        'r10', "a role's own BUILD writes its own attribute";
accepts 'class P11 { has $.x = "p11"; }; class C11 is P11 { method m { self.x } }; say C11.new.m;',
        'p11', 'the public accessor still crosses the class boundary';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
