# Regression: the general fixes that made `zef install <name>` work from the LIVE
# fez/cpan/p6c ecosystems under rakupp (search -> fetch -> extract -> install):
#   1. the anonymous `$` state var: `$ = EXPR` re-assigns on EVERY evaluation
#      (only the slot persists). zef's `::($ = $module)` plugin loader stuck at
#      the first module name, constructing every plugin from the wrong class.
#   2. composed subrule char-classes: `<-restricted +name-sep>` == one `name-sep`
#      match OR any char not matching `restricted` (zef's identity grammar).
#   3. `<( … )>` match-capture markers set the match's .from/.to.
#   4. a separator quantifier: `X* %% [SEP]+`.
#   5. IO::Path.basename ignores trailing slashes ("http://x/" -> "x").
#   6. IO::Path.relative($base) (prefix case).
#   7. Buf.append(Blob) appends the blob's BYTES (Proc stdout :bin chunks).
# Contract: exit 0 + last line PASS.
my @fail;

# 1. anonymous $ re-assigns per evaluation; $++ slot still persists
my @seen;
for <a b c> -> $m { @seen.push($ = $m) }
@fail.push("anon-assign ({@seen.join(',')})") unless @seen eqv [<a b c>];
my @n = (1..3).map({ $++ });
@fail.push('anon-counter') unless @n[*-1] == 2; # slot persisted across calls

# 2. composed subrule char class (zef's REQUIRE grammar shape)
grammar Ident {
    token restricted { [':' | '<' | '>' | '(' | ')'] }
    token name-sep   { < :: > }
    regex name  { <-restricted +name-sep>+ }
    token key   { <-restricted>+ }
    regex value { '<' ~ '>' [<( [[ <!before \>|\<|\\> . ]+?]* %% ['\\' . ]+ )>] }
    regex TOP   { ^^ <name> [':' <key> <value>]* $$ }
}
my $m = Ident.parse('Abbreviations:ver<2.2.1>:auth<zef:tbrowder>');
@fail.push('grammar-parse') unless $m;
@fail.push("grammar-name ({$m<name>})") unless $m && $m<name> eq 'Abbreviations';
@fail.push('grammar-keys') unless $m && $m<key>.elems == 2;

# 3. <( )> markers trim the overall match
@fail.push('cap-markers') unless ('x123y' ~~ /x <( \d+ )> y/) && ~$/ eq '123';

# 4. quantified separator
@fail.push('sep-quant') unless ('a,,b,,c' ~~ /^ [\w+]* %% [\,]+ $/).Bool;

# 5. basename with trailing slash
@fail.push('basename') unless 'http://360.zef.pm/'.IO.basename eq '360.zef.pm' && '/a/b/'.IO.basename eq 'b';

# 6. relative
@fail.push('relative') unless '/a/b/c/d.txt'.IO.relative('/a/b') eq 'c/d.txt';

# 7. Buf.append(Blob) appends bytes
my $b = Buf.new;
$b.append('AB'.encode);
@fail.push('buf-append') unless $b.elems == 2 && $b.decode eq 'AB';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
