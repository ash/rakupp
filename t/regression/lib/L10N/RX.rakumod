# For t/regression/imported-operator-spellings.raku: a language of three
# keywords, in the shape of an L10N dist (see t/regression/rakuast-l10n.raku).
role L10N::RX {
    use experimental :rakuast;
    token block-if { wenn }
    token block-else { sonst }
    method core2ast {
        my $ast := self.ast;
        my $name := $ast ?? $ast.simple-identifier !! self.Str;
        $name eq 'sag' ?? RakuAST::Name.from-identifier('say') !! $ast // RakuAST::Name.from-identifier($name)
    }
}
sub EXPORT(|) {
    $*LANG.define_slang('MAIN', $*LANG.slang_grammar('MAIN'));
    {}
}
