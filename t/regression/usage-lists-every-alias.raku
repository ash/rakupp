# $*USAGE as Rakudo 2026.09 writes it (2026-11-06). Three differences, all
# found when showcase/ledger's usage message came out differently on the two
# engines:
#
#   · an option is listed under EVERY name it answers to: `:f(:$file)` is
#     `-f|--file=<Str>` and `:v(:verbose(:$loud))` `-v|--verbose|--loud`,
#     where rakupp showed only the first;
#   · a required option comes before the optional ones;
#   · with %*SUB-MAIN-OPTS<named-anywhere> the positionals lead the line —
#     options may follow them then — and without it the options do.
#
# And an UNTYPED renamed option (`:w(:$wide)`, `:q($quiet)`) lost its `#=`
# description, so it was missing from the option list and from `.WHY`: that
# path of parseSignature never claimed the trailing pod. Typed ones kept it.
#
# The expected text is Rakudo's, with the program name replaced. The JS
# backend has its own usage writer and MAIN binder; t/js's corpus checks it
# against the interpreter. Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\nGOT\n$got\nWANT\n$want") unless $got eq $want
}

sub f(
    :v(:verbose(:$loud)),       #= three names
    :w(:$wide),                 #= two names
    Int :d(:dd(:$deep)),        #= typed
    :q($quiet),                 #= renamed
) { }
check &f.signature.params.map({ .WHY // '(none)' }).join(' / '),
      'three names / two names / typed / renamed', 'every option keeps its #= description';

my $work = $*TMPDIR.add("usage-lists-every-alias-$*PID");
mkdir $work;

my $main = q:to/END/;
    sub MAIN(
        Str $command = 'balance',   #= what to do
        *@query,                    #= accounts
        Str  :f(:$file),            #= the journal
        Int  :$depth,               #= how deep
        Bool :B(:$cost),            #= at cost
        Bool :$flat,                #= flat
        :v(:verbose(:$loud)),       #= three names
        Str :$x!,                   #= required
    ) { }
    END

my $options = q:to/END/;

        [<command>]                  what to do [default: 'balance']
        [<query> ...]                accounts
        -f|--file=<Str>              the journal
        --depth[=Int]                how deep
        -B|--cost                    at cost
        --flat                       flat
        -v|--verbose|--loud[=Any]    three names
        -x=<Str>                     required
    END
my $opts = '-x=<Str> [-f|--file=<Str>] [--depth[=Int]] [-B|--cost] [--flat] [-v|--verbose|--loud[=Any]]';
my $pos  = '[<command>] [<query> ...]';

for (False, True) -> $anywhere {
    my $src = $work.add($anywhere ?? 'anywhere.raku' !! 'first.raku');
    $src.spurt: ($anywhere ?? "my %*SUB-MAIN-OPTS = :named-anywhere;\n" !! '') ~ $main;
    my $p = run($*EXECUTABLE, $src.Str, '--help', :out, :err);
    my $got = ($p.out.slurp(:close) ~ $p.err.slurp(:close)).subst($src.Str, 'PROG', :g).lines.map(*.trim-trailing).join("\n");
    my $want = ("Usage:", "  PROG " ~ ($anywhere ?? "$pos $opts" !! "$opts $pos"), $options.lines.map(*.trim-trailing)).flat.join("\n");
    check $got, $want, $anywhere ?? 'named-anywhere: positionals first' !! 'options first';
}

for $work.dir { .unlink }
$work.rmdir;
if @fail { note "FAILED: @fail.join("\n")"; say 'FAIL' } else { say 'PASS' }
