# For t/regression/module-enum-is-our.raku: enums a module declares outside
# any package, the way HTTP::HPACK and Cro::HTTP2 do, and one class that
# uses them.
enum RakuppEnumOur::Indexing <Indexed NotIndexed NeverIndexed>;
enum RakuppEnumOurPlain <PlainA PlainB>;
enum <RakuppAnonX RakuppAnonY>;
my enum RakuppEnumOurMine <MineA MineB>;

class RakuppEnumOur::Header {
    has RakuppEnumOur::Indexing $.indexing = Indexed;
}
