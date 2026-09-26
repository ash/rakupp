// IO::CatHandle, written in Raku and compiled the first time the type is
// used: the class is a port of Rakudo's (src/core.c/IO/CatHandle.rakumod) that
// needs no nqp. It reads a regular file itself, a chunk at a time, so that
// mid-stream nl-in / chomp / encoding changes and mixed read/get/readchars
// behave as they do in Rakudo.
namespace rakupp {
extern const char* const kCatHandleSrc;
const char* const kCatHandleSrc = R"RAKUCAT(
class IO::CatHandle is IO::Handle {
    has @!handles;
    has $!iterator;
    has $!active-handle;
    has $!active-path;      # what the active handle was opened on

    has Str  $.encoding;
    has      &.on-switch is rw;
    has Bool $.chomp     is rw = True;
    has      $.nl-in           = ["\n", "\r\n"];
    has Str  $.nl-out    is rw = "\n";

    # How the active handle is read. One on a regular file we decode
    # ourselves, a chunk at a time, into @!pieces: the unconsumed text as one
    # Str, as its graphemes, or as lines alternating with their separators,
    # as $!pkey says. One that is not a regular file, or that user code was
    # given (.handles), is read through its own methods instead.
    has Bool $!buffered;
    has Bool $!delegate;
    has Bool $!touched;     # we read from it (a fresh handle is not at EOF)
    has Bool $!hdone;       # it gave us its last byte
    has      $!tail;        # bytes of a character the last chunk cut in two
    has      @!pieces;
    has Int  $!pidx;
    has Str  $!pkey;        # '', 'text', 'chars' or a separators key
    has      $!nl-seen;     # the nl-in that @!seps and $!nl-key come from
    has      @!seps;
    has Str  $!nl-key;
    has Bool $!crlf;        # the buffered text has a "\r\n" to translate

    method new(*@handles, *%opts) { self.bless(:@handles, |%opts) }
    submethod TWEAK(:@handles, :$bin) {
        if $bin {
            X::IO::BinaryAndEncoding.new.throw if $!encoding.defined;
        }
        else {
            $!encoding //= 'utf8';
        }
        @!handles  = @handles;
        $!iterator = @!handles.iterator;
        self.next-handle;
    }

    method raku(IO::CatHandle: --> Str:D) {
        return self.^name unless self.defined;
        my @parts = self!list-raku(@!handles);
        @parts.push: ':!chomp' unless $!chomp;
        @parts.push: ":nl-in({$!nl-in.list.raku})" unless $!nl-in eqv ["\n", "\r\n"];
        @parts.push: $!encoding.defined ?? ":encoding({$!encoding.raku})" !! ':bin';
        @parts.push: ':&.on-switch({;})' if &!on-switch;  # can't .raku Callables
        self.^name ~ '.new(' ~ @parts.join(', ') ~ ')'
    }
    # handles are spelled out so that the .raku EVALs back
    method !list-raku(@items) {
        my @r = @items.map: {
            $_ ~~ IO::Handle
              ?? "IO::Handle.new(path => {.path.raku}, chomp => {.chomp.raku}, "
                   ~ "nl-in => {.nl-in.List.raku}, nl-out => {.nl-out.raku}, "
                   ~ "encoding => {.encoding.raku})"
              !! .raku
        }
        '(' ~ @r.join(', ') ~ (@r == 1 ?? ',' !! '') ~ ')'
    }

    # Close the active handle and make the next one active, opening it if
    # it is not open yet
    method next-handle(::?CLASS:D:) {
        my $old = $!active-handle;
        my $ = $old.close if $old.defined;
        self!reset;

        my $item = $!iterator.pull-one;
        if $item =:= IterationEnd {
            $!active-handle = Nil;
        }
        elsif $item ~~ IO::Handle {
            $!active-handle = $item.opened ?? self!adopt($item) !! self!open($item);
        }
        elsif $item eq '-' {
            $!active-handle = self!adopt($*IN);
        }
        else {
            $!active-handle = self!open($item.IO);
        }

        if $!active-handle.defined {
            $!buffered = so try $!active-handle.path.f;
            $!active-handle.read(0) if $!buffered;   # seek/tell/eof in bytes
        }
        if &!on-switch {
            my $c = self!count(&!on-switch);
            $c == 0 ?? &!on-switch()
              !! $c == 1 ?? &!on-switch($!active-handle // Nil)
              !! $c == 2 || $c == Inf
                ?? &!on-switch($!active-handle // Nil, $old // Nil)
                !! die ':&on-switch must have .count 0, 1, 2, or Inf';
        }
        $!active-handle // Nil
    }
    method !open($what) {
        my $h = $!encoding.defined
          ?? $what.open(:r, :chomp($!chomp), :nl-in($!nl-in), :enc($!encoding))
          !! $what.open(:r, :chomp($!chomp), :nl-in($!nl-in), :bin);
        $h.throw if $h ~~ Failure;
        $!active-path = $what ~~ IO::Path ?? $what !! $h.path;
        $h
    }
    method !adopt($h) {
        $!active-path = $h.path;
        $h.encoding($!encoding // 'bin');
        $h.nl-in = $!nl-in;
        $h.chomp = $!chomp;
        $h
    }
    method !count(&cb) {
        my $c = &cb.count;
        # a bare block's implicit $_ may report a count of 0: it takes both
        $c == 0 && &cb.signature.params ?? Inf !! $c
    }
    method !reset() {
        $!buffered = $!delegate = $!touched = $!hdone = False;
        $!tail   = buf8.new;
        @!pieces = ();
        $!pidx   = 0;
        $!pkey   = '';
    }

    method handles(::?CLASS:D: --> Seq:D) {
        my $gave = False;
        my $h;
        Seq.from-loop: { $h }, {
            if $gave {
                $h = self.next-handle;
            }
            else {
                $gave = True;
                $h = $!active-handle;
            }
            self!expose if $h.defined;
            $h.defined
        }
    }
    # User code gets the active handle: give back what we buffered, and read
    # through the handle from now on
    method !expose() {
        return if $!delegate;
        self!unload;
        $!delegate = True;
        return unless $!buffered && $!encoding.defined;
        # the handle keeps a separate position for characters; move it too
        my $h   = $!active-handle;
        my $pos = $h.tell;
        if $pos > 0 {
            $h.seek(0, SeekFromBeginning);
            my $pre = $h.read($pos);
            $h.readchars: self!utf8
              ?? +$pre.grep({ $_ +& 0xC0 != 0x80 })
              !! $pre.decode($!encoding).codes;
        }
    }

    method chomp(::?CLASS:D:) is rw {
        Proxy.new:
          FETCH => -> $ { $!chomp },
          STORE => -> $, $chomp {
              $!active-handle.chomp = $chomp if $!active-handle.defined;
              $!chomp = $chomp
          }
    }
    method nl-in(::?CLASS:D:) is rw {
        Proxy.new:
          FETCH => -> $ { $!nl-in },
          STORE => -> $, $nl-in {
              $!active-handle.nl-in = $nl-in if $!active-handle.defined;
              $!nl-in = $nl-in
          }
    }

    method encoding(::?CLASS:D: |c) {
        return $!encoding || Nil unless c.elems;
        my $enc = c[0];
        if $!active-handle.defined {
            self!unload;
            $!encoding = $!active-handle.encoding($enc);
        }
        else {
            $!encoding = !$enc.defined || $enc eq 'bin'
              ?? Nil !! Encoding::Registry.find(~$enc).name;
        }
        $!encoding || Nil
    }

    # ---- reading the active handle ------------------------------------

    sub lf(Str:D $s) { $s.contains("\r\n") ?? $s.subst("\r\n", "\n", :g) !! $s }

    method !plain() { $!delegate || !$!buffered }
    method !utf8()  { $!encoding.lc.subst('-', '', :g).starts-with('utf8') }

    # the next chunk of the active handle, decoded; a character cut in two
    # at its end waits in $!tail for the next chunk
    method !chunk(--> Str:D) {
        my $h   = $!active-handle;
        my $raw = $!tail;
        $raw.append: $h.read(0x10000);
        $!hdone = $h.eof;
        my $cut = $!hdone ?? $raw.elems !! self!cut($raw);
        $!tail  = $raw.subbuf($cut);
        $raw.subbuf(0, $cut).decode($!encoding)
    }
    method !cut($raw) {
        my $n = $raw.elems;
        if self!utf8 {
            my $i = $n;
            $i-- while $i > 0 && $i > $n - 4 && $raw[$i - 1] +& 0xC0 == 0x80;
            return $n unless $i;
            my $lead = $raw[$i - 1];
            my $len  = $lead >= 0xF0 ?? 4 !! $lead >= 0xE0 ?? 3 !! $lead >= 0xC0 ?? 2 !! 1;
            $i - 1 + $len > $n ?? $i - 1 !! $n
        }
        elsif $!encoding.contains('16') { $n - $n % 2 }
        elsif $!encoding.contains('32') { $n - $n % 4 }
        else { $n }
    }
    method !unconsumed(--> Str:D) {
        $!pkey eq ''       ?? ''
          !! $!pkey eq 'text' ?? @!pieces[0]
          !! @!pieces.skip($!pidx).join
    }
    method !shape(Str:D $key, Str:D $text) {
        @!pieces = $key eq 'text'  ?? $text
                !! $key eq 'chars' ?? $text.comb
                !! $text.split(@!seps, :v);
        $!pidx = 0;
        $!pkey = $key;
        $!crlf = $text.contains("\r\n");
    }
    method !mode(Str:D $key) {
        self!shape($key, self!unconsumed) unless $!pkey eq $key;
    }
    method !more(--> Bool:D) {
        return False if $!hdone;
        my $text = self!chunk;
        self!shape($!pkey || 'text', self!unconsumed ~ $text);
        True
    }
    method !all(--> Str:D) {
        my @text = self!unconsumed;
        @text.push: self!chunk until $!hdone;
        @!pieces = ();
        $!pidx   = 0;
        $!pkey   = '';
        lf(@text.join)
    }
    method !empty(--> Bool:D) {
        return False if $!tail.elems;
        $!pkey eq ''       ?? True
          !! $!pkey eq 'text' ?? @!pieces[0] eq ''
          !! $!pidx >= @!pieces.elems
             || $!pkey ne 'chars' && $!pidx == @!pieces.end && @!pieces[$!pidx] eq ''
    }
    method !buffered-bytes(--> Int:D) {
        my $text = self!unconsumed;
        ($text ?? $text.encode($!encoding).bytes !! 0) + $!tail.elems
    }
    # give back what we buffered: the handle's position is ours again
    method !unload() {
        my $back = self!buffered-bytes;
        $!tail   = buf8.new;
        @!pieces = ();
        $!pidx   = 0;
        $!pkey   = '';
        $!hdone  = False;
        $!active-handle.seek(-$back, SeekFromCurrent) if $back;
    }
    # the separators nl-in stands for; we see "\r\n" as "\n", as Rakudo does
    method !seps($nl) {
        my @s;
        for $nl.list -> $s {
            @s.push: ~$s;
            @s.push: $s.subst("\n", "\r\n", :g)
              if $s.contains("\n") && !$s.contains("\r\n");
        }
        @!seps    = @s.unique;
        $!nl-key  = "\x[1]" ~ @!seps.join("\x[0]");
        $!nl-seen = $nl;
    }

    method !active-eof(--> Bool:D) {
        my $h = $!active-handle;
        return $h.eof if self!plain;
        return False unless $!touched && self!empty;
        $!pkey eq '' ?? $h.eof !! $!hdone
    }
    method !get1() {
        $!touched = True;
        my $h = $!active-handle;
        return $h.get if $!delegate || !$!buffered;
        my $nl = $h.nl-in;
        self!seps($nl) unless $nl eqv $!nl-seen;
        self!shape($!nl-key, self!unconsumed) unless $!pkey eq $!nl-key;
        while $!pidx >= @!pieces.end {   # the last line may be incomplete
            last unless self!more;
        }
        my $line = @!pieces[$!pidx];
        if $!pidx < @!pieces.end {
            $line ~= @!pieces[$!pidx + 1] unless $h.chomp;
            $!pidx += 2;
        }
        else {
            return Nil if $!pidx++ > @!pieces.end || $line eq '';
        }
        $!crlf ?? lf($line) !! $line
    }
    method !getc1() {
        $!touched = True;
        return $!active-handle.getc if self!plain;
        self!mode('chars');
        while $!pidx >= @!pieces.end {   # a mark may yet join the last one
            last unless self!more;
        }
        $!pidx < @!pieces.elems ?? lf(@!pieces[$!pidx++]) !! Nil
    }
    method !readchars1(Int:D $n) {
        $!touched = True;
        return $!active-handle.readchars($n) if self!plain;
        self!mode('text');
        while @!pieces[0].chars <= $n {
            last unless self!more;
        }
        my $text = @!pieces[0];
        if $n >= $text.chars {
            @!pieces[0] = '';
            return lf($text);
        }
        @!pieces[0] = $text.substr($n);
        lf($text.substr(0, $n))
    }
    method !read1(Int:D $n) {
        $!touched = True;
        self!unload;
        $!active-handle.read($n)
    }
    method !slurp1($bin) {
        $!touched = True;
        my $h = $!active-handle;
        my $res;
        if $bin || !$!encoding.defined {
            self!unload;
            $res = buf8.new;
            loop {
                my $b = $h.read(0x100000);
                last unless $b.elems;
                $res.append: $b;
            }
        }
        else {
            $res = self!plain ?? $h.slurp !! self!all;
        }
        my $ = $h.close;
        $res
    }
    method !words1() {
        $!touched = True;
        self!plain ?? $!active-handle.words.list !! self!all.words.list
    }
    method !text-only($what) {
        X::IO::BinaryMode.new(:trying($what)).throw unless $!encoding.defined;
    }

    # ---- the reading API -----------------------------------------------

    method get(::?CLASS:D:) {
        return Nil unless $!active-handle.defined;
        self!text-only('get');
        my $res;
        Nil until ($res = self!get1).defined || !self.next-handle.defined;
        $res // Nil
    }
    method getc(::?CLASS:D:) {
        return Nil unless $!active-handle.defined;
        self!text-only('getc');
        my $res;
        Nil until ($res = self!getc1).defined || !self.next-handle.defined;
        $res // Nil
    }
    method read(::?CLASS:D: $bytes = $*DEFAULT-READ-ELEMS) {
        # A short read that is not at EOF (a TTY, a pipe) is returned as is,
        # rather than switching handles too early
        my $want = $bytes.Int;
        my $ret  = buf8.new;
        return $ret unless $!active-handle.defined;
        loop {
            my $chunk = self!read1($want - $ret.elems);
            $ret.append: $chunk;
            last if $ret.elems >= $want;
            if self!active-eof || !$chunk.elems {
                last unless self.next-handle.defined;
            }
            else {
                last;
            }
        }
        $ret
    }
    method readchars(::?CLASS:D: $chars = $*DEFAULT-READ-ELEMS) {
        return '' unless $!active-handle.defined;
        self!text-only('readchars');
        my $want = $chars.Int;
        my $ret  = self!readchars1($want);
        $ret ~= self!readchars1($want - $ret.chars)
            while $ret.chars < $want && self.next-handle.defined;
        $ret
    }
    method slurp(::?CLASS:D: :$bin) {
        # no :close: slurping exhausts, and so closes, every handle
        return Nil unless $!active-handle.defined;
        my @parts = self!slurp1($bin);
        @parts.push: self!slurp1($bin) while self.next-handle.defined;
        if $bin || !$!encoding.defined {
            my $res = buf8.new;
            $res.append: $_ for @parts;
            return $res;
        }
        @parts.join
    }
    method slurp-rest(|) {
        X::Obsolete.new(:old<slurp-rest>, :replacement<slurp>,
          :when('with IO::CatHandle')).throw
    }

    method comb (::?CLASS:D: |c) { self.slurp.comb:  |c }
    method split(::?CLASS:D: |c) { self.slurp.split: |c }

    # a lazy Seq of what &produce gives until it gives Nil, at most $n of
    # them unless $n < 0; with $close, the cat handle is closed at its end
    method !lazy(&produce, Int:D $n, $close) {
        my $i    = 0;
        my $done = False;
        my $v;
        Seq.from-loop: { $v }, {
            if !$done && ($n < 0 || $i++ < $n) && ($v = produce()).defined {
                True
            }
            else {
                self.close if $close && !$done;
                $done = True;
                False
            }
        }
    }
    method !limit($limit) {
        $limit ~~ Whatever || $limit == Inf ?? -1 !! $limit.Int
    }
    method lines(::?CLASS:D: $limit = Inf, :$close --> Seq:D) {
        self!lazy({ self.get }, self!limit($limit), $close)
    }
    method words(::?CLASS:D: $limit = Inf, :$close --> Seq:D) {
        my @buf;
        my $started = False;
        self!lazy({
            until @buf || !$!active-handle.defined {
                if $started {
                    last unless self.next-handle.defined;
                }
                $started = True;
                @buf = self!words1;
            }
            @buf ?? @buf.shift !! Nil
        }, self!limit($limit), $close)
    }

    method Supply(::?CLASS:D: :$size = $*DEFAULT-READ-ELEMS --> Supply:D) {
        $!encoding.defined
          ?? supply {
                 my $str = self.readchars($size);
                 while $str.chars {
                     emit $str;
                     $str = self.readchars($size);
                 }
                 done;
             }
          !! supply {
                 my $buf = self.read($size);
                 while $buf.elems {
                     emit $buf;
                     $buf = self.read($size);
                 }
                 done;
             }
    }

    method eof(::?CLASS:D: --> Bool:D) {
        Nil while $!active-handle.defined && self!active-eof
            && self.next-handle.defined;
        !$!active-handle.defined
    }

    method close(::?CLASS:D:) {
        # an IO::Pipe's .close gives a Proc, which must not be sunk
        my $unsink = $!active-handle.close if $!active-handle.defined;
        until (my $pulled = $!iterator.pull-one) =:= IterationEnd {
            $unsink = $pulled.close if $pulled ~~ IO::Handle;
        }
        $!active-handle = Nil;
        self!reset;
        True
    }
    method DESTROY { self.close }

    method gist(IO::CatHandle: --> Str:D) {
        return '(' ~ self.^name.split('::').tail ~ ')' unless self.defined;
        self.^name ~ '(' ~ (self.opened ?? "opened on {self.path.gist}" !! 'closed') ~ ')'
    }
    method Str(::?CLASS:D:) {
        $!active-handle.defined ?? self.path.Str !! '<closed IO::CatHandle>'
    }
    method IO(::?CLASS:D:) {
        $!active-handle.defined ?? $!active-path !! Nil
    }
    method path(::?CLASS:D:) {
        $!active-handle.defined ?? $!active-path !! Nil
    }
    method opened(::?CLASS:D: --> Bool:D) { $!active-handle.defined }
    method lock(::?CLASS:D: |c) {
        $!active-handle.defined ?? $!active-handle.lock(|c) !! Nil
    }
    method unlock(::?CLASS:D:) {
        $!active-handle.defined ?? $!active-handle.unlock !! Nil
    }
    method seek(::?CLASS:D: |c) {
        return Nil unless $!active-handle.defined;
        my $h = $!active-handle;
        self!unload;
        my ($offset, $whence) = c.list;
        if $offset.defined {
            my $base = !$whence.defined || $whence == SeekFromBeginning ?? 0
              !! $whence == SeekFromCurrent ?? $h.tell
              !! (try $h.path.s) // 0;
            die "Failed to seek in filehandle: 22" if $base + $offset < 0;
        }
        $h.seek(|c)
    }
    method tell(::?CLASS:D:) {
        $!active-handle.defined
          ?? $!active-handle.tell - self!buffered-bytes
          !! Nil
    }
    method t(::?CLASS:D: --> Bool:D) {
        $!active-handle.defined ?? $!active-handle.t !! False
    }
    method native-descriptor(::?CLASS:D:) {
        $!active-handle.defined ?? $!active-handle.native-descriptor !! Nil
    }
    # code that does not know it has a cat handle may try to open it
    method open(::?CLASS:D: |) { self }

    # the writing half is not implemented, as in Rakudo
    method flush(|)      { X::NYI.new(:feature<flush>).throw      }
    method out-buffer(|) { X::NYI.new(:feature<out-buffer>).throw }
    method print(|)      { X::NYI.new(:feature<print>).throw      }
    method printf(|)     { X::NYI.new(:feature<printf>).throw     }
    method print-nl(|)   { X::NYI.new(:feature<print-nl>).throw   }
    method put(|)        { X::NYI.new(:feature<put>).throw        }
    method say(|)        { X::NYI.new(:feature<say>).throw        }
    method write(|)      { X::NYI.new(:feature<write>).throw      }
    method WRITE(|)      { X::NYI.new(:feature<WRITE>).throw      }
    method READ(|)       { X::NYI.new(:feature<READ>).throw       }
    method EOF(|)        { X::NYI.new(:feature<EOF>).throw        }
}
)RAKUCAT";
}
