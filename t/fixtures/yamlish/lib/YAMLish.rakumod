use v6;

unit module YAMLish:ver<0.1.2>:auth<zef:leont>;

my $yaml-namespace = 'tag:yaml.org,2002:';

class Namespace {
	has Str %.prefixes;
	method lookup(Str $name) {
		return %!prefixes{$name} // do given $name {
			when '!' {
				'!';
			}
			when '!!' {
				$yaml-namespace;
			}
			default {
				die "No such prefix $name known: " ~ %!prefixes.keys.join(", ");
			}
		}
	}
}

role Tag {
	method full-name(Namespace $namespace) { ... }
}

class ShortHandTag does Tag {
	has Str:D $.namespace is required;
	has Str:D $.local is required;
	method full-name(Namespace $namespace) {
		return $namespace.lookup($!namespace) ~ $!local;
	}
}

class VerbatimTag does Tag {
	has Str:D $.url is required;
	method full-name(Namespace $namespace) {
		return $!url;
	}
}

class NonSpecificTag does Tag {
	has Bool:D $.resolved is required;
	method full-name(Namespace $) {
		return $!resolved ?? '!' !! '?';
	}
}

role Node {
	method concretize($, $, %, %) { ... }
}

my $resolved = NonSpecificTag.new(:resolved);
my $unresolved = NonSpecificTag.new(:!resolved);

role Element does Node {
	has Tag $.tag = Tag;
	has Str $.anchor;
	method effective-tag() {
		return $!tag // $unresolved;
	}
	method make-value($, $, %, %) { ... }
	method concretize($schema, $namespaces, %anchors, %callbacks) {
		my $value = self.make-value($schema, $namespaces, %anchors, %callbacks);
		with $!anchor {
			%anchors{$!anchor} = $value;
		}
		return $value;
	}
}

role Single does Element {
	has Str:D $.value is required;
	has Str:D $.type is required;
	method make-value($schema, $namespaces, %anchors, %callbacks) {
		my $full-name = $.effective-tag.full-name($namespaces);
		if $full-name eq '!' {
			return $!value;
		}
		elsif $full-name eq '?' {
			my $match = $schema.new.parse($!value);
			return $match ?? $match.ast !! die "Invalid value $!value";
		}
		else {
			my $callback = %callbacks{$full-name};
			return $callback ?? $callback($!value) !! $!value;
		}
	}
}

class Plain does Single {
}

class Quoted does Single {
	method effective-tag() {
		return $!tag // $resolved;
	}
}

class Mapping does Element {
	has Pair @.elems;
	submethod BUILD(:@!elems, :$!anchor = Str, :$!tag = Tag) {}
	method make-value($schema, $namespaces, %anchors, %callbacks) {
		my @pairs := @.elems.map({ .key.concretize($schema, $namespaces, %anchors, %callbacks) => .value.concretize($schema, $namespaces, %anchors, %callbacks)}).list;
		my $tag = $.effective-tag;
		if $tag ~~ NonSpecificTag {
			return @pairs.hash;
		}
		else {
			my $full-name = $tag.full-name($namespaces);
			return %callbacks{$full-name}(@pairs);
		}
	}
}

class Sequence does Element {
	has Node @.elems;
	submethod BUILD(:@!elems, :$!anchor = Str, :$!tag = Tag) {}
	method make-value($schema, $namespaces, %anchors, %callbacks) {
		my @pairs = @.elems.map(*.concretize($schema, $namespaces, %anchors, %callbacks)).list;
		my $tag = $.effective-tag;
		if $tag ~~ NonSpecificTag {
			return @pairs;
		}
		else {
			my $full-name = $tag.full-name($namespaces);
			return %callbacks{$full-name}(@pairs);
		}
	}
}

class Alias does Node {
	has Str:D $.identifier is required;
	method concretize($schema, $namespaces, %anchors, %callbacks) {
		return %anchors{$!identifier} // die "Unknown anchor " ~ $!identifier;
	}
}

my $default-version = 1.2;
class Document {
	has Rat:D $.version = $default-version;
	has Element:D $.root is required;
	has Namespace:D $.namespace = Namespace.new;
	has Bool:D $.explicit is required;
	method concretize($schema, %callbacks) {
		my %anchors;
		return $!root.concretize($schema, $!namespace, %anchors, %callbacks);
	}
}

grammar Grammar {
	method indent-panic($/, $indent, $what) {
		my ($line-num, $column) := self.line-column($/);
		die "Problem with indentatiton in $what at {$line-num}:{$column}."
	}

	method line-column($/) {
		my $c = $/;
		my @lines-so-far = $/.orig.substr(0, $c.from).lines;
		my $line-num = +@lines-so-far;
		my $column   = @lines-so-far.tail.chars;
		return ($line-num, $column);
	}

	token TOP {
		<.document-prefix>?
		[
		| <document=directive-document>
		| <document=explicit-document>
		| <document=simple-document>
		]
		[
		| <.document-suffix>+ <.document-prefix>* <document=any-document>?
		| <.document-prefix>* <document=explicit-document>
		]*
	}
	token document-prefix {
		<.bom>? <.comment-line>* <?{ $/.chars > 0 }>
	}
	token bom {
		"\x[FEFF]"
	}
	token comment-line {
		<.space>* <.comment> <.line-end>
	}
	token line-end {
		<line-break> | $
	}
	token document-suffix {
		<.document-end> <.comment>? <.line-end> <.comment-line>*
	}
	token any-document {
		| <directive-document>
		| <explicit-document>
		| <bare-document>
	}
	token directive-document {
		<directives>
		{  }
		<explicit-document>
	}
	token directives {
		[ '%' [ <version=yaml-directive> | <tags=tag-directive> ] <.space>* <.line-break> ]+
	}
	token yaml-directive {
		'YAML' <.space>+ $<version>=[ <[0..9]>+ \. <[0..9]>+ ]
	}
	token tag-directive {
		'TAG' <.space>+ <tag-handle> <.space>+ <tag-prefix>
	}
	token tag-prefix {
		| '!' <.uri-char>+
		| <.tag-char> <.uri-char>*
	}
	token explicit-document	{
		<.directives-end>
		[
		| <document=bare-document>
		| <document=empty-document>
		]
	}
	token empty-document {
		<.comment-line>* <?before <document-suffix> | <document-prefix> | $>
	}
	token directives-end {
		'---'
	}
	token document-end {
		'...'
	}
	token bare-document {
		<!before '---' | '...'>
		[
		| <block('', 0)>
		| <.begin-space> <inline>
		| <.begin-space> <block-string('')>
		|| <.begin-space> <!before '---' | '...'> <plain('')>
		]
		[ <.newline> | <.space>* <.comment> ]
	}
	token simple-document {
		<!before '---' | '...'>
		[
		| <block('', 0)>
		| <root-block>
		| <inline>
		| <block-string('')>
		|| <plain('')>
		]
		[ <.newline> | <.space>* <.comment> ]?
	}
	token begin-space {
		<?before <break>> <.ws>
	}

	token ws {
		<.space>*
		[ [ <!after <.alnum>> <.comment> ]? <.line-break> <.space>* ]*
	}
	token block-ws(Str $indent) {
		<.space>*
		[ <!after <.alnum>> <.comment> <.line-break> $indent <.space>* ]*
	}
	token newline {
		[ <.space>* <.comment>? <.line-break> ]+
	}
	token space {
		<[\ \t]>
	}
	token comment {
		'#' <-line-break>*
	}
	token line-break {
		<[ \c[LF] \r \r\c[LF]] >
	}
	token break {
		<.line-break> | <.space>
	}

	token nb {
		<[\x09\x20..\x10FFFF]>
	}

	token block(Str $indent, Int $minimum-indent) {
		<properties>?
		<.newline>
		:my $new-indent;
		<?before $indent $<sp>=[' ' ** { $minimum-indent..* } ] { $new-indent = $indent ~ $<sp> }>
		$new-indent
		[ <value=sequence($new-indent)> | <value=map($new-indent)> ]
	}

	token root-block {
		:my $new-indent;
		<?before $<sp>=[' ' ** { 0..* } ] { $new-indent = ~$<sp> }>
		$new-indent
		[ <value=sequence($new-indent)> | <value=map($new-indent)> ]
	}

	token map(Str $indent) {
		<map-entry($indent)>+ % [ <.newline> $indent ]
		[ <.newline> $indent \s+ <wrongkey=key> <.space>* ':' {} <.indent-panic: $<wrongkey>, $indent, "map"> ]?
	}
	token map-entry(Str $indent) {
		  <key> <.space>* ':' <?break> <element($indent, 0)>
		| '?' <.block-ws($indent)> <key=.element($indent, 0)> <.newline> $indent
		  <.space>* ':' <.space>+ <element($indent, 0)>
	}

	token sequence(Str $indent) {
		<list-entry($indent)>+ % [ <.newline> $indent ]
		[ <.newline> $indent \s+ $<wrongdash>='-' <?break> {} <.indent-panic: $<wrongdash>, $indent, "list"> ]?
	}
	token list-entry(Str $indent) {
		'-' <?break>
		[
		  || <element=cuddly-list-entry($indent)>
		  || <element($indent, 1)> <.comment>?
		]
	}
	token cuddly-list-entry(Str $indent) {
		:my $new-indent;
		$<sp>=' '+ { $new-indent = $indent ~ ' ' ~ $<sp> }
		[ <element=map($new-indent)> | <element=sequence($new-indent)> ]
	}

	token key {
		| <inline-key-plain>
		| <single-key>
		| <double-key>
	}
	token plainfirst {
		<-[\-\?\:\,\[\]\{\}\#\&\*\!\|\>\'\"\%\@\`\ \t\x0a\x0d]>
		| <[\?\:\-]> <!before <.space> | <.line-break>>
	}
	regex plain(Str $indent) {
		[ <properties> <.space>+ ]?
		$<value> = [ <.plainfirst> [ <-[\x0a\x0d\:\#]> | ':' <!break> | <!after <.space>> '#' ]* ]
		[ <.comment>? <value=foldable-plain> $indent ' ' $<value>=[ <-[\x0a\x0d\#]>  | <!after <.space>> '#' ]* ]*
		<.comment>?
	}
	token foldable-plain {
		<line-break>+
	}
	regex inline-flow-plain {
		[ <properties> <.space>+ ]?
		$<value> = [
			<.plainfirst> :
			[ <-[\x0a\x0d\:\#\,\[\]\{\}]> | ':' <!break> | <!after <.space>> '#' ]*
			<!after <.space>> :
		]
		<.space>*
	}
	regex inline-key-plain {
		[ <properties> <.space>+ ]?
		$<value> = [
			<.plainfirst> :
			[ <-[\x0a\x0d\:\#]> | ':' <!break> | <!after <.space>> '#' ]*
			<!after <.space>> :
		]
		<.space>*
	}
	token single-key {
		[ <properties> <.space>+ ]?
		"'" ~ "'" [ <str=single-bare> | <str=single-quotes> | <str=space> ]*
	}
	token double-key {
		[ <properties> <.space>+ ]?
		\" ~ \" [ <str=.quoted-bare> | \\ <str=.quoted-escape> | <str=.space> ]*
	}

	regex single-quoted {
		[ <properties> <.space>+ ]?
		"'" ~ "'" [ <str=single-bare> | <str=single-quotes> | <str=foldable-whitespace> | <str=space> ]*
	}
	token single-bare {
		<-['\x0a\x0D\ ]>+
	}
	token single-quotes {
		"''"
	}
	token double-quoted {
		[ <properties> <.space>+ ]?
		\" ~ \" [ <str=.quoted-bare> | \\ <str=.quoted-escape> | <str=foldable-whitespace> | <str=space> ]*
	}
	token quoted-bare {
		<-space-[\"\\\n]>+
	}
	token quoted-escape {
		<["\\/abefnrvtzNLP_\ ]> | x <xdigit>**2 | u <xdigit>**4 | U<xdigit>**8
	}
	token foldable-whitespace {
		<.space>* [ <breaks=line-break> <.space>* ]+
	}
	token block-string(Str $indent) {
		[ <properties> <.space>+ ]?
		$<kind>=<[\|\>]> $<chomp>=<[+-]>? <.space>*
		<.comment>? <.line-break>
		:my $new-indent;
		<?before
			[ $indent <.space>* <.line-break> ]*
			$indent $<sp>=' '+ { $new-indent = $<sp> }
		>
		[ $indent <.space>* $<content>=[ <.after .> ] <.line-break> ]*
		[
			[ $indent $new-indent $<content>=[ \N* ] ]+ % <.line-break>
		]+ % [ <.line-break>+ % [ $<content>= [ <.after .> ] ]  ]
	}

	token inline-map {
		[ <properties> <.space>+ ]?
		'{' <.ws> <pairlist> <.ws> '}'
	}
	rule pairlist {
		<pair>* %% \,
	}
	rule pair {
		<key> ':' [ <inline> || <inline=inline-flow-plain> ]
	}

	token inline-list {
		[ <properties> <.space>+ ]?
		'[' <.ws> <inline-list-inside> <.ws> ']'
	}
	rule inline-list-inside {
		[ <inline> || <inline=inline-flow-plain> ]* %% \,
	}

	token identifier-char {
		<[\x21..\x7E\x85\xA0..\xD7FF\xE000..\xFFFD\x10000..\x10FFFF]-[\,\[\]\{\}]>+
	}
	token identifier {
		<identifier-char>+ <!before <identifier-char> >
	}

	token inline {
		| <value=alias>
		| <value=inline-map>
		| <value=inline-list>
		| <value=single-quoted>
		| <value=double-quoted>
	}

	token properties {
		| <anchor> [ <.space>+ <tag> ]?
		| <tag> [ <.space>+ <anchor> ]?
	}

	token alias {
		'*' <identifier>
	}

	token element(Str $indent, Int $minimum-indent) {
		[ <.space>* [ <value=block($indent, $minimum-indent)> | <value=block-string($indent)> ]
		|  <.block-ws($indent)> <value=inline> <.comment>?
		|| <.block-ws($indent)> <value=plain($indent)> <.comment>?
		]
	}
	token anchor {
		'&' <identifier>
	}

	token tag {
		| <value=verbatim-tag>
		| <value=shorthand-tag>
		| <value=non-specific-tag>
	}

	token verbatim-tag {
		'!<' <uri-char>+ '>'
	}
	token uri-char {
		<char=uri-escaped-char> | <char=uri-real-char>
	}
	token uri-escaped-char {
		:i '%' $<hex>=<[ 0..9 A..F ]>**2
	}
	token uri-real-char {
		<[ 0..9 A..Z a..z \-#;/?:@&=+$,_.!~*'()\[\] ]>
	}

	token shorthand-tag {
		<tag-handle> <tag-char>+
	}
	token tag-handle {
		'!' [ <[ A..Z a..z 0..9 ]>* '!' ]?
	}
	token tag-real-char {
		<[ 0..9 A..Z a..z \-#;/?:@&=+$_.~*'() ]>
	}
	token tag-char {
		[ <char=uri-escaped-char> | <char=tag-real-char> ]
	}

	token non-specific-tag {
		'!'
	}

	class Actions {
		method TOP($/) {
			make ( @<document>».ast );
		}
		method !first($/) {
			make $/.values.[0].ast;
		}
		method any-document($/) {
			self!first($/);
		}
		method directive-document($/) {
			my $version = $<directives>.ast<version> // $default-version;
			my %prefixes = $<directives>.ast<tags>;
			my $namespace = Namespace.new(:%prefixes);
			my $root = $<explicit-document>.ast.root;
			make Document.new(:$root, :$namespace, :$version, :explicit);
		}
		method directives($/) {
			my %directives;
			%directives<tags> = @<tag-directive>».ast.list;
			%directives<version> = @<yaml-directive>[0].ast.Rat if @<yaml-directives> == 0;
			make %directives;
		}
		method tag-directive($/) {
			make ~$<tag-handle> => ~$<tag-prefix>
		}
		method explicit-document($/) {
			make Document.new(:root($<document>.ast.root), :explicit);
		}
		method bare-document($/) {
			my $root = $/.values.[0].ast;
			make Document.new(:$root, :!explicit);
		}
		method simple-document($/) {
			my $root = $/.values.[0].ast;
			make Document.new(:$root, :!explicit);
		}
		method empty-document($/) {
			make Nil;
		}
		method map($/) {
			make [ Mapping, @<map-entry>».ast ];
		}
		method map-entry($/) {
			make $<key>.ast => $<element>.ast
		}
		method key($/) {
			self!first($/);
		}
		method sequence($/) {
			make [ Sequence, @<list-entry>».ast ];
		}
		method list-entry($/) {
			make $<element>.ast;
		}
		method cuddly-list-entry($/) {
			my ($class, $elems) = @($<element>.ast);
			make $class.new(:$elems);
		}
		method space($/) {
			make ~$/;
		}
		my sub flow-fold(Str:D $str) {
			return $str.comb("\n") > 1 ?? "\n" !! ' ';
		}
		method single-quoted($/) {
			my $value = @<str>».ast.join;
			my Tag $tag = $<properties><tag>.ast;
			my Str $anchor = $<properties><anchor>.ast;
			make Quoted.new(:$value, :$tag, :$anchor, :type("'"));
		}
		method single-bare($/) {
			make ~$/;
		}
		method single-quotes($/) {
			make "'";
		}
		method single-key($/) {
			self.single-quoted($/);
		}
		method double-quoted($/) {
			my $value = @<str>».ast.join;
			my Tag $tag = $<properties><tag>.ast;
			my Str $anchor = $<properties><anchor>.ast;
			make Quoted.new(:$value, :$tag, :$anchor, :type('"'));
		}
		method double-key($/) {
			self.double-quoted($/);
		}
		method foldable-whitespace($/) {
			my $num = +@<breaks>;
			make ($num > 1 ?? "\n" xx $num - 1 !! ' ');
		}
		method foldable-plain($/) {
			make +@<line-break>;
		}
		method plain($/) {
			my @values = @<value>.map: { .ast.defined ?? .ast !! .Str };
			my $value = @values.shift;
			for @values -> $joiner, $text is copy {
				if $joiner > 1 {
					$value ~= "\n" x $joiner - 1;
				}
				else {
					$text .= subst(/ ^ ' '*/, ' ');
				}
				$value ~= $text;
			}
			$value.=subst(/<[\ \t]>+$/, '');
			my Tag $tag = $<properties><tag>.ast;
			my Str $anchor = $<properties><anchor>.ast;
			make Plain.new(:$value, :$tag, :$anchor, :type(':'));
		}
		method inline-key-plain($/) {
			my Tag $tag = $<properties><tag>.ast;
			my Str $anchor = $<properties><anchor>.ast;
			make Plain.new(value => ~$<value>, :$tag, :$anchor, :type(":"));
		}
		method inline-flow-plain($/) {
			self.inline-key-plain($/);
		}
		method block-string($/) {
			my @lines = @<content>.map(*.Str);
			my Str $value = '';
			if $<kind> eq '>' {
				my Int $folded = 1;
				my Bool $indented = False;
				$value = '';
				my $/;
				for @lines -> $line {
					if $line ~~ / ^ \s* $ / {
						$value ~= "\n" if $indented;
						$value ~= "\n";
						$folded++;
						$indented = False;
					}
					elsif $line ~~ /^\s+/ {
						$value ~= "\n" if $value && $folded < 2;
						$value ~= $line;
						$folded = 1;
						$indented = True;
					}
					elsif $indented {
						$value ~= "\n";
						$folded = 2;
						$indented = False;
					}
					else {
						$value ~= ' ' if !$folded;
						$value ~= $line;
						$folded = False;
					}
				}
			}
			else {
				$value = @lines.map(* ~ "\n").join('');
			}
			if $<chomp> ne '+' {
				$value .= subst(/ \n* $ /, $<chomp> eq '-' ?? '' !! "\n")
			}
			my Tag $tag = $<properties><tag>.ast;
			my Str $anchor = $<properties><anchor>.ast;
			make Plain.new(:$value, :$tag, :$anchor, :type(~$<kind>));
		}

		method element($/) {
			make $<value>.ast;
		}

		method inline-map($/) {
			my Tag $tag = $<properties><tag>.ast;
			my Str $anchor = $<properties><anchor>.ast;
			make Mapping.new(:elems($<pairlist>.ast), :$tag, :$anchor);
		}
		method pairlist($/) {
			make @<pair>».ast.list;
		}
		method pair($/) {
			make $<key>.ast => $<inline>.ast;
		}
		method identifier($/) {
			make ~$/;
		}
		method inline-list($/) {
			my Tag $tag = $<properties><tag>.ast;
			my Str $anchor = $<properties><anchor>;
			make Sequence.new(:elems($<inline-list-inside>.ast), :$tag, :$anchor);
		}
		method inline-list-inside($/) {
			make @<inline>».ast.list;
		}
		method inline-atom($/) {
			make $<value>.ast;
		}
		method inline($/) {
			make $<value>.ast;
		}

		method tag($/) {
			make $<value>.ast;
		}
		method verbatim-tag($/) {
			my $url = @<uri-char>».ast.join('');
			make VerbatimTag.new(:$url);
		}
		method uri-char($/) {
			make $<char>;
		}
		method uri-real-char($/) {
			make ~$/;
		}
		method uri-escaped-char($/) {
			make :16(~$<hex>).chr;
		}
		method shorthand-tag($/) {
			make ShortHandTag.new(:namespace($<tag-handle>.ast), :local(@<tag-char>».ast.join('')));
		}
		method tag-char($/) {
			make $<char>.ast // ~$<char>;
		}
		method tag-handle($/) {
			make ~$/;
		}
		method non-specific-tag($/) {
			make $resolved;
		}

		method alias($/) {
			make Alias.new(:identifier($<identifier>.ast));
		}

		method block($/) {
			my ($class, $elems) = @($<value>.ast);
			my Tag $tag = $<properties><tag>.ast;
			my Str $anchor = $<properties><anchor>.ast;
			make $class.new(:$elems, :$tag, :$anchor);
		}

		method root-block($/) {
			my ($class, $elems) = @($<value>.ast);
			make $class.new(:$elems);
		}

		method anchor($/) {
			make $<identifier>.ast;
		}

		method quoted-bare ($/) { make ~$/ }

		my %h = '\\' => "\\",
				'/' => "/",
				'a' => "\a",
				'b' => "\b",
				'e' => "\e",
				'n' => "\n",
				't' => "\t",
				'f' => "\f",
				'r' => "\r",
				'v' => "\x0b",
				'z' => "\0",
				'"' => "\"",
				' ' => ' ',
				"\n"=> "\n",
				'N' => "\x85",
				'_' => "\xA0",
				'L' => "\x2028",
				'P' => "\x2029";
		method quoted-escape($/) {
			if $<xdigit> {
				make chr(:16($<xdigit>.join));
			} else {
				make %h{~$/};
			}
		}
	}

	method parse($string, :%tags, *%args) {
		my %*yaml-anchors;
		nextwith($string, :actions(Actions), |%args);
	}
	method subparse($string, :%tags, *%args) {
		my %*yaml-anchors;
		nextwith($string, :actions(Actions), |%args);
	}
}

grammar Schema::JSON {
	regex TOP {
		[ <element> <.ws> || <plain> ]
		{ make $/.values[0].ast; }
	}

	proto token element { * }
	token element:<null> {
		'null'
		{ make Any }
	}
	token element:<int> {
		<[+-]>?
		[ 0 | <[1..9]> <[0..9]>* ]
		<|w>
		{ make $/.Str.Int }
	}
	token element:<rat> {
		<[+-]>?
		[ 0 | <[1..9]> <[0..9]>* ]
		\. <[0..9]>+
		<|w>
		{ make $/.Rat }
	}
	token element:<float> {
		<[+-]>?
		[ 0 | <[1..9]> <[0..9]>* ]
		[ \. <[0..9]>+ ]?
		[ <[eE]> [\+|\-]? <[0..9]>+ ]?
		<|w>
		{ make +$/.Str }
	}
	token element:<inf> {
		$<sign=[+-]>?
		'.inf'
		{ make $<sign> eq '-' ?? -Inf !! Inf }
	}
	token element:<nan> {
		'.nan'
		{ make NaN }
	}
	token element:<yes> {
		true <|w>
		{ make True }
	}
	token element:<no> {
		false <|w>
		{ make False }
	}

	token plain {
		<?{ False }>
	}
}

grammar Schema::Core is Schema::JSON {
	token element:<hex> {
		:i
		'-'?
		'0x'
		$<value>=[ <[0..9A..F]>+ ]
		<|w>
		{ make :16(~$<value>) }
	}
	token element:<oct> {
		:i
		'-'?
		'0o'
		$<value>=[ <[0..7]>+ ]
		<|w>
		{ make :8(~$<value>) }
	}
	token element:<inf> {
		:i
		$<sign>='-'?
		'.inf'
		{ make $<sign> ?? -Inf !! Inf }
	}
	token element:<nan> {
		:i '.nan'
		{ make NaN }
	}
	token element:<null> {
		[ :i 'null' | '~' ]
		{ make Any }
	}
	token element:<yes> {
		[ :i y | yes | true | on ] <|w>
		{ make True }
	}
	token element:<no> {
		[ :i n | no | false | off ] <|w>
		{ make False }
	}

	token plain {
		^ $<value>=.* $
		{ make ~$<value> }
	}
}

grammar Schema::Extra is Schema::Core {
	token element:<datetime> {
		$<year>=<[0..9]>**4 '-' $<month>=<[0..9]>**2 '-' $<day>=<[0..9]>**2
		[ ' ' | 'T' ]
		$<hour>=<[0..9]>**2 '-' $<minute>=<[0..9]>**2 '-' $<seconds>=<[0..9]>**2
		$<offset>=[ <[+-]> <[0..9]>**1..2]
		{ make DateTime.new(|$/.hash».Int) }
	}
	token element:<date> {
		$<year>=<[0..9]>**4 '-' $<month>=<[0..9]>**2 '-' $<day>=<[0..9]>**2
		{ make Date.new(|$/.hash».Int) }
	}
}

my %yaml-tags = (
	$yaml-namespace => {
		str => sub ($value) {
			return $value if $value ~~ Str;
			die "Couldn't resolve { $value.WHAT } to string";
		},
		int => sub ($value) {
			return $value.Int if $value ~~ Str;
			die "Couldn't resolve a { $value.WHAT } to integer";
		},
		float => sub ($value) {
			return $value.Rat if $value ~~ Str;
			die "Couldn't resolve a { $value.WHAT } to float";
		},
		null => sub ($) {
			return Any;
		},
		binary => sub ($value) {
			require MIME::Base64;
			return MIME::Base64.decode($value.subst(/<[\ \t\n]>/, '', :g)) if $value ~~ Str;
			die "Binary has to be a string";
		},
		seq => sub ($value) {
			return $value if $value ~~ Iterable;
			die "Could not convert { $value.WHAT } to a sequence";
		},
		map => sub ($value) {
			return $value.hash if $value ~~ Iterable && all(@$value) ~~ Pair;
			die "Could not convert { $value.WHAT } to am Associative";
		},
		set => sub ($value) {
			return $value.map(*.key).Set if $value ~~ Iterable && all(@$value) ~~ Pair;
			die "Could not convert { $value.WHAT } to a set";
		},
		omap => sub ($value) {
			die "Ordered maps not implemented yet"
		},
	},
);
my sub flatten-tags(%tags) {
	return %tags.kv.map({ |$^value.kv.map($^namespace ~ * => *) } );
}
my %default-tags = flatten-tags(%yaml-tags);

our sub load-yaml(Str $input, ::Grammar:U :$schema = ::Schema::Core, :%tags) is export {
	my $match = Grammar.parse($input);
	CATCH {
		fail "Couldn't parse YAML: $_";
	}
	my Callable %callbacks = |%default-tags, |flatten-tags(%tags);
	return $match ?? $match.ast[0].concretize($schema, %callbacks) !! fail "Couldn't parse YAML";
}
our sub load-yamls(Str $input, ::Grammar:U :$schema = ::Schema::Core, :%tags) is export {
	my $match = Grammar.parse($input);
	CATCH {
		fail "Couldn't parse YAML: $_";
	}
	my Callable %callbacks = |%default-tags, |flatten-tags(%tags);
	return $match ?? $match.ast.map(*.concretize($schema, %callbacks)) !! fail "Couldn't parse YAML";
}

my proto emit-yaml($, $) {*}

my sub escape-chars(Str:D $str) {
	return $str.trans(['\\',   "\b", "\f", "\n", "\r", "\t", "\r\n"]
				=> ['\\\\', '\b', '\f', '\n', '\r', '\t', '\r\n'])\
				.subst(/<-[\c9\xA\xD\x20..\x7E\xA0..\xD7FF\xE000..\xFFFD\x10000..\x10FFFF]>/, {
					given .ord { $_ > 0xFFFF ?? .fmt("\\U%08x") !! $_ > 0xFF ?? .fmt("\\u%04x") !! .fmt("\\x%02x") }
				}, :g);
}

multi emit-yaml(Single $single, Namespace $namespace) {
	my @parts = '=VAL';
	@parts.push: '&' ~ $single.anchor with $single.anchor;
	@parts.push: '<' ~ $single.tag.full-name($namespace) ~ '>' with $single.tag;
	@parts.push: $single.type ~ escape-chars($single.value);
	take @parts.join(' ');
}

multi emit-yaml(Mapping $mapping, Namespace $namespace) {
	take '+MAP';
	for $mapping.elems -> $pair {
		emit-yaml($pair.key, $namespace);
		emit-yaml($pair.value, $namespace);
	}
	take '-MAP';
}

multi emit-yaml(Sequence $sequence, Namespace $namespace) {
	take '+SEQ';
	for $sequence.elems -> $elem {
		emit-yaml($elem, $namespace);
	}
	take '-SEQ';
}

multi emit-yaml(Alias $alias, Namespace $namespace) {
	take '=ALI *' ~ $alias.identifier
}

our sub stream-yaml(Str $input) is export {
	gather {
		if Grammar.parse($input) -> $match {
			take '+STR';
			for $match.ast -> $document {
				take '+DOC' ~ ($document.explicit ?? ' ---' !! '');
				emit-yaml($document.root, $document.namespace);
				take '-DOC';
			}
			take '-STR';
		}
	}
}

proto to-yaml($;$ = Str, :$sorted) {*}

multi to-yaml(Real:D $d; $ = Str, :$sorted) { ~$d }
multi to-yaml(Bool:D $d; $ = Str, :$sorted) { $d ?? 'true' !! 'false'; }
multi to-yaml(Str:D  $d where /^ <!Schema::Core::element> <[\w.-]>+ $/; $ = Str) {
	return $d;
}
multi to-yaml(Str:D  $d; $, :$sorted) {
	return '"' ~ escape-chars($d).trans(['"'] => ['\"']) ~ '"'
}
multi to-yaml(Positional:D $d, Str $indent, :$sorted) {
	return ' []' unless ?$d;
	return "\n" ~ $d.map({ "$indent\- " ~ to-yaml($_, $indent ~ '  ', :$sorted) }).join("\n");
}
multi to-yaml(Associative:D $d, Str $indent, :$sorted) {
	return ' {}' unless ?$d;
	my @elems = $sorted ?? $d.sort !! $d.pairs;
	return "\n" ~ @elems.map({ $indent ~ to-yaml(.key, $indent, :$sorted) ~ ': ' ~ to-yaml(.value, $indent ~ '  ', :$sorted) }).join("\n")
}

multi to-yaml(Mu:U $, $, :$sorted) { '~' }
multi to-yaml(Mu:D $s, $, :$sorted) {
	die "Can't serialize an object of type " ~ $s.WHAT.perl
}

subset Collection where Positional|Associative;

proto to-yaml-doc($, :$sorted) { * }
multi to-yaml-doc(Collection $document, :$sorted) {
	return '---' ~ to-yaml($document, '', :$sorted) ~ "\n";
}
multi to-yaml-doc(Any $document, :$sorted) {
	return '--- ' ~ to-yaml($document, '', :$sorted) ~ "\n";
}

sub save-yaml($document, :$sorted = True --> Str) is export {
	to-yaml-doc($document, :$sorted) ~ "...";
}
sub save-yamls(**@documents, :$sorted = True --> Str) is export {
	@documents.map({ to-yaml-doc($_, :$sorted) }).join('') ~ "...";
}

=begin pod

=head1 NAME

YAMLish - a YAML parser/emitter written in pure raku

=head1 DESCRIPTION

This is a YAML parser written in pure-raku.
It aims at being feature complete (though there still a few features left to implement). Patches are welcome.

=head1 INSTALLATION

=begin code :lang<console>

$ zef install YAMLish

=end code

=head1 EXPORTED SUBS

=item C<load-yaml(Str $input, ::Grammar:U :$schema = ::Schema::Core, :%tags)>
=item C<load-yamls(Str $input, ::Grammar:U :$schema = ::Schema::Core, :%tags)>
=item C<save-yaml($document, :$sorted = True)>
=item C<save-yamls(**@documents, :$sorted = True)>

=head1 Example use for a configuration file

This configuration file begins with a comment
followed by three lines of key/value:

=begin code
# key: value
lang: en
lat: 46.12345
lon: -82.6231
=end code

Access the file in your code like this:

=begin code
use YAMLish;
my $str = "config.yml".IO.slurp:
my %conf = load-yaml $str;
say %conf<lang>; # OUTPUT: «en␤»
say %conf<lat>;  # OUTPUT: «46.12345␤»
say %conf<lon>;  # OUTPUT: «-82.6231␤»
=end code

=head1 TODO

Please have a look at L<./TODO.md>

=head1 AUTHOR

Leon Timmermans

=head1 LICENSE

Artistic License 2.0

=end pod
