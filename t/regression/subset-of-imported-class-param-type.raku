# Regression: a subset of a class imported by its short name (#278).
#
# `use Login; subset NotLoggedIn of UserSession where …` recorded its base
# as the text `UserSession`, the name the import binds, while the class is
# registered as Login::UserSession. A parameter of that subset then had a
# `.type` naming no class, so `.type ~~ SomeRole` was False even though
# UserSession does the role. Cro finds a route's auth parameter exactly so
# (`$param.type ~~ Cro::HTTP::Auth`), and refused the route: "Parameter type
# Login::UserSession not allowed on a request unpack parameter". Writing
# `of Login::UserSession` worked. The base is now resolved where the subset
# is declared.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
my $dir = $*TMPDIR.add("subset-import-{$*PID}");
$dir.add('lib').mkdir;
$dir.add('lib/Authy.rakumod').spurt: "unit module Authy;\nrole Auth is export \{ }\n";
$dir.add('lib/Login.rakumod').spurt: q:to/END/;
    unit module Login;
    use Authy;
    class UserSession does Auth is export { has Bool $.is-logged-in is rw }
    END
my $prog = $dir.add('prog.raku');
$prog.spurt: q:to/END/;
    use Authy;
    use Login;
    subset NotLoggedIn of UserSession where *.is-logged-in.not;
    sub f(NotLoggedIn $s) { 'bound' }
    my $t = &f.signature.params[0].type;
    say $t ~~ Auth;
    say $t.^name;
    say NotLoggedIn ~~ Auth;
    say f(UserSession.new(:!is-logged-in));
    say (try f(UserSession.new(:is-logged-in))) // 'refused';
    END
my $p = run($*EXECUTABLE, '-I', ~$dir.add('lib'), ~$prog, :out, :err);
my @lines = $p.out.slurp(:close).lines;
my $err = $p.err.slurp(:close);
check @lines.List, ('True', 'Login::UserSession', 'True', 'bound', 'refused'), "the subset's parameter type ($err)";
sub rm-tree(IO::Path $p) { if $p.d { rm-tree($_) for $p.dir; $p.rmdir } else { $p.unlink } }
rm-tree($dir);   # (the modules' .precomp too)

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
