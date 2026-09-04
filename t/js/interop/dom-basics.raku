# --target=js interop golden: the DOM through `use JS`. Runs under node with
# t/js/interop/dom-stub.js preloaded; the .out beside it is the golden.
use JS;
my $doc = JS.document;
my $div = $doc.createElement('div');
$div.setAttribute('id', 'greeting');
$div<textContent> = 'hello';
$doc.body.appendChild($div);
say $doc.getElementById('greeting')<textContent>;
say $div.getAttribute('id');
say $div<tagName>;
my @items = <apple banana cherry>;
for @items -> $fruit {
    my $li = $doc.createElement('li');
    $li<textContent> = $fruit.uc;
    $div.appendChild($li);
}
say $div.querySelectorAll('li').elems;
say $div.querySelector('li')<textContent>;
say $div<innerHTML>;
# a Raku closure as an event handler
my $clicks = 0;
$div.addEventListener('click', -> $ev { $clicks++; say "clicked: {$ev<type>}" });
$div.dispatchEvent(JS.Event.new('click'));
$div.dispatchEvent(JS.Event.new('click'));
say "clicks: $clicks";
say $doc<title>;
