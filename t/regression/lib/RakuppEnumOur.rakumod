# For t/regression/module-enum-is-our.raku: enums a module declares outside
# any package, the way HTTP::HPACK does, and one class that uses them.
enum RakuppEnumOur::Indexing <Indexed NotIndexed NeverIndexed>;
enum RakuppEnumOurPlain <PlainA PlainB>;

class RakuppEnumOur::Header {
    has RakuppEnumOur::Indexing $.indexing = Indexed;
}
