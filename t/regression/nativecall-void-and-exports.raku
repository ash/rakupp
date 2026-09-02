# Regression: the NativeCall names a program can actually reach (issue #57).
#
# `Pointer[void]` — the C `void *`, and the first line of any FFI binding that
# hands round an opaque handle — died with "Undefined routine 'void'": `void`
# was the ONE NativeCall type name the engine did not know. The sweep that
# followed found the rest of the reachable-name gaps:
#
#   1. `void` resolves, and parameterises Pointer/CArray
#   2. the qualified spellings (NativeCall::Types::void, ::Pointer, …) resolve
#      to the SAME types, including inside a parameterisation
#   3. the routines the export stash advertises exist: refresh,
#      explicitly-manage, and the :ALL-only guess_library_name /
#      check_routine_sanity under their qualified names
#   4. `CArray[T].of` answers the element type
#   5. a byte-backed CArray has a real ADDRESS — `nativecast(Pointer, $c)` used
#      to answer NULL, so the next native call dereferenced it and crashed
#   6. `cglobal(Str, …)` means "this program", not a library named "Str"
#
# Written to pass under Rakudo too, so the two engines are compared, not just
# this one's own reading. Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;
sub check($ok, $what) { @fail.push($what) unless $ok }

# 1. void, bare and as a type parameter
check(void.defined.not,               'void is a type object');
check(Pointer[void].gist.contains('void'), 'Pointer[void] keeps its parameter');
my $vp = Pointer[void].new(4242);
check($vp.Int == 4242,                'Pointer[void].new holds its address');
check(nativecast(Pointer[void], $vp).Int == 4242, 'nativecast to Pointer[void]');
constant void-ptr = Pointer[void];    # the issue's own line
check(void-ptr.gist.contains('void'), 'constant = Pointer[void]');

# 1b. …and that constant used AS A TYPE, which is the only reason to declare it.
# The RETURN position is the one that was broken: the marshaller matched the
# declared name against its arms literally, `void-ptr` matched none, and malloc
# handed back a bare Int — so `$p ~~ Pointer` was False and `free($p)` was given
# an integer. Both silently, which is what makes it worth a test.
sub nc_malloc(size_t             --> void-ptr) is native is symbol('malloc') {*}
sub nc_memcpy(void-ptr, void-ptr, size_t --> void-ptr) is native is symbol('memcpy') {*}
sub nc_free(void-ptr)                          is native is symbol('free')   {*}
my $bytes = CArray[uint8].new(0x41, 0x42, 0x43, 0);
my $heap  = nc_malloc($bytes.elems);
check($heap ~~ Pointer,               'a `--> void-ptr` return is a Pointer, not an Int');
check(?$heap,                         'and a non-NULL one');
nc_memcpy($heap, nativecast(void-ptr, $bytes), $bytes.elems);
check(nativecast(Str, $heap) eq 'ABC', 'C wrote through the aliased void *');
nc_free($heap);

# 2. the qualified spellings name the same types
check(NativeCall::Types::void === void,         'NativeCall::Types::void is void');
check(NativeCall::Types::Pointer === Pointer,   'NativeCall::Types::Pointer is Pointer');
check(NativeCall::Types::CArray  === CArray,    'NativeCall::Types::CArray is CArray');
check(NativeCall::Types::size_t  === size_t,    'NativeCall::Types::size_t is size_t');
check(Pointer[NativeCall::Types::void] === Pointer[void],
                                      'a qualified type PARAMETER canonicalises');

# 3. the advertised routines exist
check(NativeCall::EXPORT::ALL::{'&guess_library_name'}:exists, 'guess_library_name is in :ALL');
check(NativeCall::EXPORT::DEFAULT::{'&refresh'}:exists,        'refresh is in :DEFAULT');
check((try { refresh(Pointer.new(0)); True }) // False,        'refresh is callable');
my $s = 'managed';
check((try { explicitly-manage($s); True }) // False,          'explicitly-manage is callable');
check((try { NativeCall::guess_library_name('c').chars > 0 }) // False,
                                                               'guess_library_name answers a name');

# 4. CArray[T].of
check(CArray[int32].of === int32,     'CArray[int32].of');
check(CArray[uint8].new(1,2).of === uint8, 'an instance answers .of too');

# 5. a byte-backed CArray has an address C can use
my $buf = CArray[uint8].new(0x41, 0x42, 0x43, 0);
my $bp  = nativecast(Pointer, $buf);
check($bp.Int != 0,                   'nativecast(Pointer, $carray) is not NULL');
sub c_strlen(Pointer --> size_t) is native is symbol('strlen') {*}
check(c_strlen($bp) == 3,             'C reads the array through that pointer');
# (asked about a NATIVE routine — Rakudo's own sanity check warns loudly about
# an ordinary one, which is exactly what it is for)
check((try { NativeCall::check_routine_sanity(&c_strlen); True }) // False,
                                      'check_routine_sanity is callable');
check(nativecast(Pointer[uint8], $buf).deref == 0x41, 'Pointer[T].deref reads it back');
# …and it stays the SAME buffer after a native call has written through it
sub c_strlen2(CArray[uint8] --> size_t) is native is symbol('strlen') {*}
c_strlen2($buf);
check(nativecast(Pointer, $buf).Int != 0, 'the address survives a native call');
check(c_strlen(nativecast(Pointer, $buf)) == 3, 'and still points at the bytes');

# 6. cglobal against the running program (no library named "Str")
check((try { cglobal(Str, 'no_such_symbol_at_all_xyz', int32) }) === Nil,
      'cglobal(Str, …) looks in this program');
check(($! ~~ X::AdHoc && $!.message.contains('symbol')), 'and fails on the SYMBOL, not the library');

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
