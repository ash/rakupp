# Cookbook — parsing a configuration file with a grammar

Turning an nginx-shaped configuration file — nested blocks, quoted strings,
comments — into a data structure you can query, and then telling whoever wrote
the file exactly which line is wrong.

Every program on this page was run under Raku++, and each one was run under
Rakudo as well: the output is identical on both, so nothing here depends on
which engine you use.

## The input

[grammar/sample.conf](grammar/) — braces inside braces, which is where a regex
starts to lose:

```
# a small server configuration
listen 8080;
workers 4;

server {
    name "example.com";
    root /var/www/example;

    location /api {
        proxy_pass http://127.0.0.1:9000;
        timeout 30;
    }
    ...
}
```

## The grammar

A grammar is a class whose methods are named pieces of a pattern. This one is
nine lines:

```raku
grammar Config {
    rule TOP { <.ws> <statement>* }

    token ws { <!ww> \h* [ [ '#' \N* ]? \n \s* ]* }

    proto rule statement {*}
    rule  statement:sym<block>   { <name> <arg>? '{' <statement>* '}' }
    token statement:sym<setting> { <name> \h+ [ <value>+ % \h+ ] \h* ';' <.ws> }

    token name { <[\w\-]>+ }
    token arg  { <[\w/\.\-]>+ }

    proto token value {*}
    token value:sym<string> { '"' ~ '"' $<text>=<-["]>* }
    token value:sym<bare>   { <[\w/:\.\-]>+ }
}
```

Four decisions in it are worth naming.

**`rule` versus `token`.** Both are patterns that do not backtrack. A `rule`
additionally treats whitespace in the pattern as "and here `<ws>` may appear",
which is what lets the block rule be written as if the file were a sentence. A
`token` matches exactly what is written, which is why a setting — where the
whitespace rules are strict, everything on one line — is a token.

**`<statement>*` inside `statement:sym<block>`.** That is the recursion, and it
is the reason this is a grammar and not a regular expression: a block contains
statements, one kind of which is a block.

**`token ws`.** Overriding `ws` redefines what "whitespace" means for every
`rule` in the grammar. Here it swallows comments too, so no other rule has to
mention them. `<!ww>` is the standard guard: do not treat the middle of a word
as a place where whitespace could go.

**`proto rule statement {*}` with `:sym<…>` candidates.** This is multiple
dispatch for patterns. The alternatives are tried together, the longest match
wins, and — the part that pays off later — each candidate gets its own action
method, so the code that builds a block never has to ask what kind of statement
it is looking at.

## What a match gives you

Every named pattern becomes a named capture, and the whole tree is available
before any actions are involved:

```raku
my $m = Config.parse('proxy_pass http://127.0.0.1:9000;', :rule<statement>);
say 'matched  : ', ~$m;
say 'name     : ', ~$m<name>;
say 'values   : ', $m<value>.elems;
say 'value[0] : ', ~$m<value>[0];
```

```output
matched  : proxy_pass http://127.0.0.1:9000;
name     : proxy_pass
values   : 1
value[0] : http://127.0.0.1:9000
```

`:rule<statement>` starts at a rule other than `TOP`, which is how you test one
piece of a grammar without feeding it a whole file. `~$m` is the matched text,
`$m<name>` a named capture, and a quantified capture like `<value>+` is a list.

## Actions: one method per rule

`.parse` on its own gives a match tree. An actions class turns it into your own
data as the parse happens — each method `make`s the value its rule stands for,
and a rule higher up reads it back with `.made`:

```raku
class Conf {
    method TOP($/)                    { make $<statement>».made }
    method statement:sym<setting>($/) { make %( name => ~$<name>, values => $<value>».made ) }
    method statement:sym<block>($/)   { make %( name => ~$<name>,
                                                arg  => ($<arg> ?? ~$<arg> !! ''),
                                                body => $<statement>».made ) }
    method value:sym<string>($/)      { make ~$<text> }
    method value:sym<bare>($/)        { make ~$/ }
}
```

Five lines of dispatch that never appear: the `:sym<…>` candidate that matched
picks the method. `».made` collects what the children made, so `TOP` receives
a finished tree rather than a `Match`.

## The whole program

[grammar/config.raku](grammar/config.raku) parses the file, prints an outline,
and then answers a question about it — which is the part a match tree alone
would leave you to write by hand:

```raku
my @tree = Config.parse(slurp($file), :actions(Conf)).made;

for blocks(@tree, 'server') -> %server {
    my @paths = blocks(%server<body>, 'location').map(*.<arg>);
    say setting(%server<body>, 'name'),
        ' root=', setting(%server<body>, 'root'),
        ' locations=', (@paths ?? @paths.join(',') !! '(none)');
}
```

```sh
rakupp config.raku
```

```output
--- outline ---
listen = 8080
workers = 4
server
  name = example.com
  root = /var/www/example
  location /api
    proxy_pass = http://127.0.0.1:9000
    timeout = 30
  location /static
    root = /var/www/example/static
    cache = 1d
server
  name = internal.example.com
  root = /var/www/internal

--- servers ---
example.com root=/var/www/example locations=/api,/static
internal.example.com root=/var/www/internal locations=(none)

listen port: 8080
```

Settings come out as a list rather than a hash on purpose: a configuration file
may name the same key twice, and a hash would silently keep one of them.

## When the file is wrong

`.parse` answers `Nil` and says nothing about why. That is fine for a program
reading its own generated data and useless for a program reading a file a
person wrote. There are three levels of answer, and they cost more as they get
better.

**Level one: how far a prefix parse got.** `.subparse` matches a prefix instead
of the whole input, so the position it reaches is the end of the last thing
that made sense:

```raku
my $partial = Config.subparse($text);
my $pos     = $partial ?? $partial.to !! 0;
```

**Level two: the furthest position any rule reached.** `ws` runs between every
pair of atoms, which makes it the cheapest possible place to keep a high-water
mark:

```raku
token ws {
    <!ww> \h* [ [ '#' \N* ]? \n \s* ]*
    { $*FURTHEST = $/.to if $/.to > $*FURTHEST }
}
```

[grammar/config-where.raku](grammar/config-where.raku) prints both for
`broken.conf`, a file whose line 5 is missing its semicolon:

```output
subparse stopped at: line 3, column 1
    server {
    ^
furthest rule reached: line 7, column 5
        location /api {
        ^
```

Neither is the mistake. The prefix parse blames line 3, because the `server`
block is the whole of what failed; the high-water mark blames line 7, two lines
past the missing semicolon, because `<value>+` cheerfully read `location` and
`/api` as two more values of the `root` setting and only stopped at the `{`.
The mark is where the parser *died*, not where the file went wrong, and for a
nested format the distance between those is the size of a block.

**Level three: panic where the grammar is committed.** Once a rule has read
enough to know what it is looking at, there is no alternative left to try, and
failing loudly beats failing quietly:

```raku
rule statement:sym<block> {
    <name> <arg>? '{' <statement>*
    [ '}' || <.panic("expected '}' to close this block")> ]
}

method panic($reason) {
    my $before = self.orig.substr(0, self.pos);
    my $line   = $before.comb("\n").elems + 1;
    my $col    = self.pos - ($before.rindex("\n") // -1);
    die "line $line, column $col: $reason";
}
```

`self.pos` is where the cursor stands and `self.orig` is the whole input, so
the position costs one substring. [grammar/config-errors.raku](grammar/config-errors.raku)
is that grammar:

```sh
rakupp config-errors.raku
```

```output
sample.conf: parsed, 4 top-level statements
broken.conf: line 5, column 26: expected ';' at the end of the setting
```

Line 5, column 26, which is where the semicolon should have been.

## Four things that bite

**A `rule` does not skip what comes before it.** Sigspace inserts `<ws>` *after*
each atom, never before the first one, so a file that opens with a comment or a
blank line does not parse and the grammar looks broken. `rule TOP { <.ws> <statement>* }`
— the explicit `<.ws>` at the front — is the fix, and it is needed only there.

**A panic in a candidate that was only being tried kills a valid parse.** The
first version of the setting rule ended with `[ ';' || <.panic(...)> ]` and
nothing else. It reported an error on `sample.conf`, a file that is perfectly
good: `location /api {` is offered to the setting candidate too, which reads
`location` as a name and `/api` as a value, finds no `;`, and panics — before
the block candidate ever gets its turn. A panic is a promise that no other rule
could match here, so it belongs after something that makes the choice
unambiguous. One negative lookahead was enough:

```raku
token statement:sym<setting> {
    <name> \h+ [ <value>+ % \h+ ] \h* <!before '{'>
    [ ';' || <.panic("expected ';' at the end of the setting")> ]
    <.ws>
}
```

**`|` and `||` are different alternations.** `|` is longest-token matching: all
branches are considered and the longest match wins. `||` is sequential: the
first branch that matches wins, even if a later one would have matched more.

```raku
token word { 'foo' | 'foobar' }    # on "foobar": matches foobar
token word { 'foo' || 'foobar' }   # on "foobar": matches foo, so TOP fails
```

Write `|` for a set of alternatives and `||` when order is the point — which is
exactly what the panic idiom needs, since `[ ';' || <.panic(…)> ]` must try the
semicolon first.

**A `token` never gives back what it has eaten.** `token t { \w+ 'b' }` does not
match `aab`: `\w+` takes all three characters and, unlike in a `regex`, does not
backtrack to let `'b'` match.

```output
token \w+ "b" on "aab" : no match
regex \w+ "b" on "aab" : match
```

This is a feature — it is what makes grammars predictable and fast — but it is
the reason a pattern that works as a regular expression can fail when pasted
into a `token`. Use `regex` for the handful of rules that genuinely need to
backtrack.

## When a grammar is worth it

A regular expression is the right tool for a flat, line-shaped format. Reach
for a grammar when one of these is true, and this file hits all four:

- the format nests, so no single pattern can describe it;
- the pieces are reusable — `value` appears in every setting at every depth;
- you want the result as data, not as a list of captures, which is what an
  actions class gives you;
- the input is written by people, so the parser has to be able to say where it
  went wrong.
