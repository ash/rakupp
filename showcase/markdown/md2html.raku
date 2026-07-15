#!/usr/bin/env raku
# A Markdown-to-HTML converter. Block structure (headings, lists, code fences,
# blockquotes, rules, paragraphs) is recognised line by line; the inline layer
# (**bold**, *italic*, `code`, [links](url), ![images](url)) is a Raku grammar
# with an actions class that emits HTML directly. It reads a file (or stdin)
# and writes a complete, self-contained HTML page.
#
#   build/rakupp showcase/markdown/md2html.raku showcase/markdown/sample.md > out.html
#   build/rakupp --exe -o md2html showcase/markdown/md2html.raku
#   cat notes.md | ./md2html > notes.html
#
# The point of the showcase, like the Lisp one, is the front end: a grammar plus
# actions turning text straight into structured output.

# ---------- inline grammar: spans within a line of text ----------------
# Ordered `||` alternation with a lookahead-guarded inner and a single-char
# catch-all (`any`), so the parse always makes progress and never fails on a
# stray `*` — an unmatched marker just falls through to literal text.
grammar Inline {
    token TOP    { <node>* }
    token node   { <strong> || <em> || <code> || <image> || <link> || <run> || <any> }
    token strong { '**' [ <!before '**'> <node> ]+ '**' }
    token em     { '*'  [ <!before '*'>  <node> ]+ '*' }
    token code   { '`' $<t>=[ <-[`]>* ] '`' }
    token image  { '!' '[' $<alt>=[ <-[\]]>* ] ']' '(' $<url>=[ <-[)]>* ] ')' }
    token link   { '[' $<txt>=[ <-[\]]>* ] ']' '(' $<url>=[ <-[)]>* ] ')' }
    token run    { <-[*`\[\]!]>+ }                 # a run of ordinary characters
    token any    { . }                             # one leftover metacharacter
}

class InlineActions {
    method TOP($/)    { make $<node>».ast.join }
    method node($/)   { make ($<strong> // $<em> // $<code> // $<image> // $<link> // $<run> // $<any>).ast }
    method strong($/) { make '<strong>' ~ $<node>».ast.join ~ '</strong>' }
    method em($/)     { make '<em>' ~ $<node>».ast.join ~ '</em>' }
    method code($/)   { make '<code>' ~ esc($<t>.Str) ~ '</code>' }
    method image($/)  { make '<img src="' ~ esc-attr($<url>.Str) ~ '" alt="' ~ esc-attr($<alt>.Str) ~ '">' }
    method link($/)   { make '<a href="' ~ esc-attr($<url>.Str) ~ '">' ~ esc($<txt>.Str) ~ '</a>' }
    method run($/)    { make esc($/.Str) }
    method any($/)    { make esc($/.Str) }
}

sub esc(Str $s --> Str) {
    $s.subst('&', '&amp;', :g).subst('<', '&lt;', :g).subst('>', '&gt;', :g);
}
sub esc-attr(Str $s --> Str) { esc($s).subst('"', '&quot;', :g) }

sub inline(Str $text --> Str) {
    my $m = Inline.parse($text, actions => InlineActions.new);
    $m ?? $m.ast !! esc($text);
}

# ---------- block layer: line-oriented ---------------------------------
sub render(Str $src --> Str) {
    my @lines = $src.lines;
    my @out;
    my $i = 0;
    while $i < @lines.elems {
        my $line = @lines[$i];

        # blank line: skip
        if $line !~~ /\S/ { $i++; next; }

        # fenced code block ``` ... ```
        if $line ~~ /^ '```' / {
            my @code;
            $i++;
            while $i < @lines.elems && @lines[$i] !~~ /^ '```' / {
                @code.push(@lines[$i]); $i++;
            }
            $i++;   # closing fence
            @out.push('<pre><code>' ~ @code.map({ esc($_) }).join("\n") ~ "</code></pre>");
            next;
        }

        # heading  #.. ######
        if $line ~~ /^ ('#' ** 1..6) \s+ (.*) $/ {
            my $level = $0.Str.chars;
            @out.push("<h$level>" ~ inline($1.Str.trim) ~ "</h$level>");
            $i++; next;
        }

        # horizontal rule
        if $line ~~ /^ \s* ['---'|'***'|'___'] \s* $/ {
            @out.push('<hr>'); $i++; next;
        }

        # blockquote (one or more consecutive `> ` lines)
        if $line ~~ /^ '>' \s? / {
            my @quote;
            while $i < @lines.elems && @lines[$i] ~~ /^ '>' \s? (.*) $/ {
                @quote.push(~$0); $i++;
            }
            @out.push('<blockquote>' ~ inline(@quote.join(' ')) ~ '</blockquote>');
            next;
        }

        # unordered list
        if $line ~~ /^ \s* <[-*+]> \s+ / {
            my @items;
            while $i < @lines.elems && @lines[$i] ~~ /^ \s* <[-*+]> \s+ (.*) $/ {
                @items.push('<li>' ~ inline((~$0).trim) ~ '</li>'); $i++;
            }
            @out.push('<ul>' ~ @items.join("\n") ~ '</ul>');
            next;
        }

        # ordered list
        if $line ~~ /^ \s* \d+ '.' \s+ / {
            my @items;
            while $i < @lines.elems && @lines[$i] ~~ /^ \s* \d+ '.' \s+ (.*) $/ {
                @items.push('<li>' ~ inline((~$0).trim) ~ '</li>'); $i++;
            }
            @out.push('<ol>' ~ @items.join("\n") ~ '</ol>');
            next;
        }

        # paragraph: consecutive non-blank lines that start no other block
        my @para;
        while $i < @lines.elems && @lines[$i] ~~ /\S/
              && @lines[$i] !~~ /^ ['#'+ \s | '```' | '>' | \s* <[-*+]> \s | \s* \d+ '.' \s ]/ {
            @para.push(@lines[$i]); $i++;
        }
        @out.push('<p>' ~ inline(@para.join(' ')) ~ '</p>');
    }
    @out.join("\n");
}

# ---------- page wrapper -----------------------------------------------
my $STYLE = Q:to/CSS/;
    <style>
      body { font: 17px/1.6 Georgia, serif; max-width: 42rem; margin: 3rem auto; padding: 0 1.2rem; color: #1a1a1a; }
      h1,h2,h3 { font-family: system-ui, sans-serif; line-height: 1.25; }
      code { background: #f2f2f4; padding: 0.1em 0.35em; border-radius: 4px; font-size: 0.9em; }
      pre { background: #f2f2f4; padding: 1rem; overflow-x: auto; border-radius: 8px; }
      pre code { background: none; padding: 0; }
      blockquote { border-left: 4px solid #d0d0d8; margin: 1rem 0; padding: 0.2rem 1rem; color: #555; }
      a { color: #2563eb; } img { max-width: 100%; }
      hr { border: none; border-top: 1px solid #ddd; margin: 2rem 0; }
    </style>
    CSS

sub MAIN($file?) {
    my $src = $file.defined ?? $file.IO.slurp !! $*IN.slurp;
    my $body = render($src);
    say '<!doctype html><html><head><meta charset="utf-8">'
      ~ '<meta name="viewport" content="width=device-width, initial-scale=1">'
      ~ '<title>md2html</title>' ~ $STYLE ~ "</head><body>\n" ~ $body ~ "\n</body></html>";
}
