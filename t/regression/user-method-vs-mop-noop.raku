# Regression: a user-defined method whose name collides with a metamodel no-op
# (compose / publish_method_cache / set_rw / …) must NOT be shadowed by rakupp's
# `.^compose`-style no-op handler. This broke Cro.compose (returned the invocant
# `Cro` instead of a Cro::CompositeConnector), silently killing Cro::HTTP::Client
# at connection setup. `.^compose` (the real metamodel call) must still no-op.
# Contract: exit 0 + last line PASS.
my @fail;

# a plain `method compose` runs and returns its own value, not the invocant
class Pipe {
    has $.tag;
    method compose(*@p) { Pipe.new(tag => @p.join(',')) }
}
my $p = Pipe.compose(1, 2, 3);
@fail.push("compose-defined") unless $p.defined;
@fail.push("compose-tag ({$p.tag // 'undef'})") unless $p.defined && $p.tag eq '1,2,3';

# other MOP no-op names as user methods
class Widget {
    method publish_method_cache { 'published' }
    method set_rw($x) { "rw:$x" }
    method compose_attributes { 'composed-attrs' }
}
@fail.push('publish_method_cache') unless Widget.publish_method_cache eq 'published';
@fail.push('set_rw') unless Widget.set_rw(5) eq 'rw:5';
@fail.push('compose_attributes') unless Widget.compose_attributes eq 'composed-attrs';

# instance too
class C2 { method compose { 'inst-compose' } }
@fail.push('instance-compose') unless C2.new.compose eq 'inst-compose';

# .^compose stays a no-op returning the type (metamodel finalize)
@fail.push('meta-compose') unless Widget.^compose === Widget;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
