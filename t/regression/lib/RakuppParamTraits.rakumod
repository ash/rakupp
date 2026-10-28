# Cro::HTTP::Router's shape: a module whose exported `multi trait_mod:<is>`
# candidates take a Parameter. Companion to
# t/regression/issue-211-trait-is-candidates.raku.
module RakuppParamTraits {
    role Authed { }
    multi trait_mod:<is>(Parameter:D $param, :$authed! --> Nil) is export {
        $param does RakuppParamTraits::Authed;
    }
    multi trait_mod:<is>(Parameter:D $param, :$queried! --> Nil) is export { }
}
