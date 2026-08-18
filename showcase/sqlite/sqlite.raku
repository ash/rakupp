#!/usr/bin/env raku
# A SQLite client — a real C library driven entirely from Raku through
# NativeCall. Nothing here is a reimplementation: every query is executed by
# `libsqlite3` itself, reached with `is native('sqlite3')`, and the terminal
# is put into raw mode through `tcgetattr`/`tcsetattr` the same way. It is the
# "talks to the outside world" showcase — the binding, the marshalling of
# every SQLite type, an out-parameter protocol, and a full-screen TUI, with no
# C of our own anywhere.
#
#   build/rakupp showcase/sqlite/sqlite.raku demo.db 'select * from artists'
#   build/rakupp showcase/sqlite/sqlite.raku demo.db --format=csv '.tables'
#   build/rakupp showcase/sqlite/sqlite.raku demo.db - < script.sql
#   build/rakupp showcase/sqlite/sqlite.raku demo.db          # no SQL -> browser
#   build/rakupp --exe -o sqlite showcase/sqlite/sqlite.raku && ./sqlite demo.db
#
# The browser keys: arrows scroll, Tab switches pane, Enter opens a table,
# `:` types SQL, `q` quits.

use NativeCall;

# ---------- libsqlite3 -------------------------------------------------
# The C API, declared exactly as sqlite3.h has it. Anything the header
# returns as an opaque `sqlite3 *` is a Raku `Pointer`; the two places C uses
# an out-parameter (`sqlite3 **`, `sqlite3_stmt **`) are `Pointer is rw`.

constant SQLITE_OK   = 0;
constant SQLITE_ROW  = 100;
constant SQLITE_DONE = 101;

# Column storage classes, from sqlite3.h.
constant SQLITE_INTEGER = 1;
constant SQLITE_FLOAT   = 2;
constant SQLITE_TEXT    = 3;
constant SQLITE_BLOB    = 4;
constant SQLITE_NULL    = 5;

constant SQLITE_OPEN_READONLY  = 0x01;
constant SQLITE_OPEN_READWRITE = 0x02;
constant SQLITE_OPEN_CREATE    = 0x04;

# SQLITE_TRANSIENT — "copy the buffer, I may free it" — is the C cast
# ((sqlite3_destructor_type)-1), so it is literally the pointer -1.
constant TRANSIENT = Pointer.new(-1);

# Is this pointer NULL? A Pointer boolifies by its address on both engines.
sub null-ptr(Mu $p --> Bool) { !$p }

sub sqlite3_libversion(--> Str)                                is native('sqlite3') { * }
sub sqlite3_open_v2(Str, Pointer is rw, int32, Str --> int32)  is native('sqlite3') { * }
sub sqlite3_close(Pointer --> int32)                           is native('sqlite3') { * }
sub sqlite3_errmsg(Pointer --> Str)                            is native('sqlite3') { * }
sub sqlite3_extended_errcode(Pointer --> int32)                is native('sqlite3') { * }
sub sqlite3_changes(Pointer --> int32)                         is native('sqlite3') { * }
sub sqlite3_last_insert_rowid(Pointer --> int64)               is native('sqlite3') { * }

sub sqlite3_complete(Str --> int32)                             is native('sqlite3') { * }
sub sqlite3_exec(Pointer, Str, Pointer, Pointer, Pointer is rw --> int32)
                                                               is native('sqlite3') { * }
sub sqlite3_free(Pointer)                                      is native('sqlite3') { * }

sub sqlite3_prepare_v2(Pointer, Str, int32, Pointer is rw, Pointer is rw --> int32)
                                                               is native('sqlite3') { * }
sub sqlite3_step(Pointer --> int32)                            is native('sqlite3') { * }
sub sqlite3_reset(Pointer --> int32)                           is native('sqlite3') { * }
sub sqlite3_finalize(Pointer --> int32)                        is native('sqlite3') { * }

sub sqlite3_bind_parameter_count(Pointer --> int32)            is native('sqlite3') { * }
sub sqlite3_bind_int64(Pointer, int32, int64 --> int32)        is native('sqlite3') { * }
sub sqlite3_bind_double(Pointer, int32, num64 --> int32)       is native('sqlite3') { * }
sub sqlite3_bind_text(Pointer, int32, Str, int32, Pointer --> int32)
                                                               is native('sqlite3') { * }
sub sqlite3_bind_blob(Pointer, int32, CArray[uint8], int32, Pointer --> int32)
                                                               is native('sqlite3') { * }
sub sqlite3_bind_null(Pointer, int32 --> int32)                is native('sqlite3') { * }

sub sqlite3_column_count(Pointer --> int32)                    is native('sqlite3') { * }
sub sqlite3_column_name(Pointer, int32 --> Str)                is native('sqlite3') { * }
sub sqlite3_column_type(Pointer, int32 --> int32)              is native('sqlite3') { * }
sub sqlite3_column_int64(Pointer, int32 --> int64)             is native('sqlite3') { * }
sub sqlite3_column_double(Pointer, int32 --> num64)            is native('sqlite3') { * }
sub sqlite3_column_text(Pointer, int32 --> Str)                is native('sqlite3') { * }
sub sqlite3_column_blob(Pointer, int32 --> Pointer)            is native('sqlite3') { * }
sub sqlite3_column_bytes(Pointer, int32 --> int32)             is native('sqlite3') { * }

# ---------- the Raku face of it ----------------------------------------

class X::SQLite is Exception {
    has Str $.op;
    has Int $.code;
    has Str $.detail;
    method message(--> Str) { "SQLite $!op failed [$!code]: $!detail" }
}

# One result set, fully read: the column names and the rows as Raku values.
class Result {
    has @.names;
    has @.rows;
    method elems(--> Int) { @!rows.elems }
}

class Statement {
    has Pointer $.h;
    has        $.db;

    method !value(Int $i) {
        # Ask for the storage class first and fetch through the matching
        # accessor — sqlite3_column_text() would happily convert a blob or an
        # integer, and we want the type SQLite actually stored.
        given sqlite3_column_type($!h, $i) {
            when SQLITE_INTEGER { sqlite3_column_int64($!h, $i)  }
            when SQLITE_FLOAT   { sqlite3_column_double($!h, $i) }
            when SQLITE_TEXT    { sqlite3_column_text($!h, $i)   }
            when SQLITE_BLOB    {
                my $n = sqlite3_column_bytes($!h, $i);
                my $p = sqlite3_column_blob($!h, $i);
                $n > 0 && $p.defined ?? Buf.new(nativecast(CArray[uint8], $p)[^$n]) !! Buf.new;
            }
            default { Any }          # SQLITE_NULL
        }
    }

    # .eager matters: a lazy list would be reified after the statement is
    # finalized, i.e. read out of memory SQLite has already freed.
    method names(--> List) {
        (^sqlite3_column_count($!h)).map({ sqlite3_column_name($!h, $_) }).eager.List;
    }

    # Advance one row. Returns the row as a list, or Nil when the result set
    # is exhausted.
    method next-row() {
        my $rc = sqlite3_step($!h);
        return Nil if $rc == SQLITE_DONE;
        $!db.throw('step', $rc) if $rc != SQLITE_ROW;
        (^sqlite3_column_count($!h)).map({ self!value($_) }).eager.List;
    }

    method finish() { sqlite3_finalize($!h) unless null-ptr($!h) }
}

class DB {
    has Pointer $!h;
    has Str     $.path;

    method open(Str $path, Bool :$readonly = False, Bool :$create = True --> DB) {
        my Pointer $h .= new;
        my $flags = $readonly
            ?? SQLITE_OPEN_READONLY
            !! SQLITE_OPEN_READWRITE +| ($create ?? SQLITE_OPEN_CREATE !! 0);
        my $rc = sqlite3_open_v2($path, $h, $flags, Str);
        if $rc != SQLITE_OK {
            # An open that fails still hands back a handle carrying the
            # message; close it after reading, or it leaks.
            my $why = null-ptr($h) ?? 'cannot open database' !! sqlite3_errmsg($h);
            sqlite3_close($h) unless null-ptr($h);
            die X::SQLite.new(op => 'open', code => $rc, detail => $why);
        }
        self.bless(:h($h), :$path);
    }
    submethod BUILD(Pointer :$h, Str :$!path) { $!h = $h }

    method throw(Str $op, Int $rc) {
        die X::SQLite.new(op => $op, code => sqlite3_extended_errcode($!h) || $rc,
                          detail => sqlite3_errmsg($!h));
    }

    method version(--> Str) { sqlite3_libversion() }
    method changes(--> Int) { sqlite3_changes($!h) }

    # Compile one statement and bind the positional arguments to its ? marks.
    method prepare(Str $sql, *@binds --> Statement) {
        my Pointer $st .= new;
        my Pointer $tail .= new;
        my $rc = sqlite3_prepare_v2($!h, $sql, -1, $st, $tail);
        self.throw('prepare', $rc) if $rc != SQLITE_OK;
        die X::SQLite.new(op => 'prepare', code => SQLITE_OK,
                          detail => 'statement is empty') if null-ptr($st);

        my $want = sqlite3_bind_parameter_count($st);
        if $want != @binds.elems {
            sqlite3_finalize($st);
            die X::SQLite.new(op => 'bind', code => SQLITE_OK,
                              detail => "statement takes $want parameter(s), {@binds.elems} given");
        }
        for @binds.kv -> $i, $v {
            my $n = $i + 1;                       # SQLite numbers them from 1
            my $rc = do given $v {
                when !.defined   { sqlite3_bind_null($st, $n) }
                when Int         { sqlite3_bind_int64($st, $n, $_) }
                when Rat | Num   { sqlite3_bind_double($st, $n, .Num) }
                when Blob        { sqlite3_bind_blob($st, $n, CArray[uint8].new(.list), .bytes, TRANSIENT) }
                default          { sqlite3_bind_text($st, $n, .Str, -1, TRANSIENT) }
            };
            self.throw('bind', $rc) if $rc != SQLITE_OK;
        }
        Statement.new(h => $st, db => self);
    }

    # Run a statement to completion and collect everything it returned. The
    # statement is finalized by a LEAVE, so it is released on the way out
    # whether the rows are read or a step throws.
    method query(Str $sql, *@binds --> Result) {
        # The phaser is declared BEFORE the prepare that can throw, and guarded
        # with `with`: a failed prepare leaves $st undefined, and a LEAVE that
        # called .finish on it would replace the SQL error with a method-missing
        # one (Rakudo runs a phaser whose declaration was never reached).
        my $st;
        LEAVE { .finish with $st }
        $st = self.prepare($sql, |@binds);
        my @names = $st.names;
        my @rows;
        while (my $row = $st.next-row).defined { @rows.push($row) }
        Result.new(names => @names, rows => @rows);
    }

    # Same, for statements with no result set; returns the row count changed.
    method execute(Str $sql, *@binds --> Int) {
        self.query($sql, |@binds);
        self.changes;
    }

    # A whole script in one call. sqlite3_exec() is the one entry point that
    # takes several statements at once, and it reports failure through a
    # char** that the caller has to free.
    method run-script(Str $sql --> Int) {
        my Pointer $err .= new;
        my $rc = sqlite3_exec($!h, $sql, Pointer, Pointer, $err);
        if $rc != SQLITE_OK {
            my $detail = null-ptr($err) ?? sqlite3_errmsg($!h) !! nativecast(Str, $err);
            my $x = X::SQLite.new(op => 'exec', code => $rc, detail => $detail // 'unknown error');
            sqlite3_free($err) unless null-ptr($err);
            die $x;
        }
        self.changes;
    }

    method tables(--> List) {
        self.query(q:to/SQL/).rows.map(*.[0]).List;
            select name from sqlite_master
             where type = 'table' and name not like 'sqlite_%'
             order by name
            SQL
    }

    method schema-of(Str $table --> Str) {
        my $r = self.query('select sql from sqlite_master where name = ?', $table);
        $r.rows ?? ($r.rows[0][0] // '') !! '';
    }

    method close() {
        unless null-ptr($!h) {
            sqlite3_close($!h);
            $!h = Pointer;
        }
    }
}

# ---------- rendering values -------------------------------------------

# SQLite prints doubles with its own printf: 15 significant digits, and a
# mantissa that always carries a '.', so 1e300 reads as 1.0e+300 and a whole
# number as 2.0 rather than 2. Reproduce that, because the point of the
# compare script is byte-identical output against the real client.
sub fmt-double(Num() $n --> Str) {
    return 'Inf'  if $n == Inf;
    return '-Inf' if $n == -Inf;
    return 'NaN'  if $n.isNaN;
    my $s = sprintf('%.15g', $n);
    return $s if $s.contains('.');
    if $s.contains('e') {
        my ($m, $e) = $s.split('e', 2);
        return "$m.0e$e";
    }
    "$s.0";
}

# The text of a value for the aligned grid. Blobs are shown as SQL hex
# literals here — the byte-exact rendering the csv and json emitters use is
# unreadable in a column.
sub cell(Mu $v --> Str) {
    return ''             unless $v.defined;
    return fmt-double($v) if $v ~~ Num;
    return "x'" ~ $v.list.map({ .fmt('%02x') }).join ~ "'" if $v ~~ Blob;
    $v.Str;
}

# --- csv, byte for byte as `sqlite3 -csv -header` writes it ---------------
# Its quoting table marks everything up to and including a space, the double
# quote itself, and every byte from 0x80 up — which is why a value with a
# space or an accent comes out quoted and `dash-dash` does not. A blob is
# written as its raw bytes, so fields are assembled as bytes, not text.

sub csv-bytes(Mu $v --> Blob) {
    my $bytes = do given $v {
        when !.defined { Buf.new                     }
        when Blob      { Buf.new(.list)              }
        when Num       { fmt-double($_).encode('utf8') }
        default        { .Str.encode('utf8')         }
    };
    return $bytes unless $bytes.list.first({ $_ <= 0x20 || $_ == 0x22 || $_ == 0x2c || $_ >= 0x80 }).defined;
    my @out = 0x22;
    for $bytes.list -> $b {
        @out.push(0x22) if $b == 0x22;      # an embedded quote is doubled
        @out.push($b);
    }
    @out.push(0x22);
    Buf.new(@out);
}

sub csv-line(@values --> Blob) {
    my @out;
    for @values.kv -> $i, $v {
        @out.push(0x2c) if $i;
        @out.append(csv-bytes($v).list);
    }
    @out.push(0x0a);
    Buf.new(@out);
}

# --- json, byte for byte as `sqlite3 -json` writes it ---------------------
# Text passes through as UTF-8 and only the control characters are escaped;
# blob bytes have no encoding to trust, so every byte outside printable ASCII
# becomes \u00xx.

sub json-string(Str $s --> Str) {
    my $out = '"';
    for $s.comb -> $c {
        given $c {
            when '"'  { $out ~= '\"'  }
            when '\\' { $out ~= '\\\\' }
            when "\n" { $out ~= '\n'  }
            when "\r" { $out ~= '\r'  }
            when "\t" { $out ~= '\t'  }
            when "\b" { $out ~= '\b'  }
            when "\f" { $out ~= '\f'  }
            default   { $out ~= $c.ord < 0x20 ?? sprintf('\u%04x', $c.ord) !! $c }
        }
    }
    $out ~ '"';
}

sub json-blob(Blob $b --> Str) {
    my $out = '"';
    for $b.list -> $byte {
        $out ~= do given $byte {
            when 0x22 { '\"'  }
            when 0x5c { '\\\\' }
            when 0x20 ..^ 0x7f { .chr }
            default { sprintf('\u%04x', $byte) }
        };
    }
    $out ~ '"';
}

sub json-scalar(Mu $v --> Str) {
    return 'null'         unless $v.defined;
    return $v.Str         if $v ~~ Int;
    return fmt-double($v) if $v ~~ Num;
    return json-blob($v)  if $v ~~ Blob;
    json-string($v.Str);
}

# ---------- output formats ---------------------------------------------
# csv and json are byte-compatible with `sqlite3 -csv -header` and
# `sqlite3 -json`, which is what showcase/sqlite/compare.sh checks. An empty
# result set prints nothing at all in both — header included.

sub emit-csv(Result $r) {
    return unless $r.rows;
    # .write, not .print: a blob column is bytes, and encoding them as text
    # would rewrite every byte from 0x80 up.
    $*OUT.write(csv-line($r.names));
    $*OUT.write(csv-line($_)) for $r.rows;
}

sub emit-json(Result $r) {
    return unless $r.rows;
    my @lines = $r.rows.map(-> @row {
        '{' ~ $r.names.kv.map(-> $i, $n {
            json-string($n) ~ ':' ~ json-scalar(@row[$i])
        }).join(',') ~ '}'
    });
    say '[' ~ @lines.join(",\n") ~ ']';
}

# One row per line, pipe separated — the shape scripts like to read.
sub emit-list(Result $r) {
    say .map({ cell($_) }).join('|') for $r.rows;
}

# Our own aligned grid. Numbers go right, everything else left, and a NULL is
# spelled out rather than left blank so it cannot be confused with ''.
sub emit-table(Result $r) {
    return unless $r.names;          # DDL and INSERT return no result set
    unless $r.rows {
        say '(no rows)';
        return;
    }
    my @names = $r.names;
    # Control characters are shown escaped here — a raw newline inside a value
    # would otherwise tear the grid apart. csv and json keep them verbatim.
    my @shown = $r.rows.map(-> @row {
        @row.kv.map(-> $i, $v {
            $v.defined
                ?? cell($v).subst("\n", '\n', :g).subst("\r", '\r', :g).subst("\t", '\t', :g)
                !! 'NULL'
        }).List
    });
    my @right = (^@names).map(-> $i {
        so ?$r.rows.grep({ .[$i].defined })
           && all($r.rows.map({ .[$i] }).grep(*.defined).map({ $_ ~~ Int | Num }));
    });
    my @w = (^@names).map(-> $i {
        max @names[$i].chars, |@shown.map({ .[$i].chars });
    });

    my sub rule(Str $l, Str $m, Str $r) {
        say $l ~ @w.map({ '─' x ($_ + 2) }).join($m) ~ $r;
    }
    my sub line(@cells) {
        say '│ ' ~ (^@names).map(-> $i {
            @right[$i] ?? @cells[$i].Str.fmt("%{@w[$i]}s")
                       !! @cells[$i].Str.fmt("%-{@w[$i]}s")
        }).join(' │ ') ~ ' │';
    }

    rule('┌', '┬', '┐');
    line(@names);
    rule('├', '┼', '┤');
    line($_) for @shown;
    rule('└', '┴', '┘');
    say "({$r.elems} row{$r.elems == 1 ?? '' !! 's'})";
}

sub emit(Result $r, Str $format) {
    given $format {
        when 'csv'   { emit-csv($r)   }
        when 'json'  { emit-json($r)  }
        when 'list'  { emit-list($r)  }
        when 'table' { emit-table($r) }
        default { note "unknown format '$format' (want table, csv, json or list)"; exit 2 }
    }
}

# ---------- dot commands ------------------------------------------------
# A small slice of the real client's `.`-commands, enough to look around a
# database without knowing its schema.

sub dot-command(DB $db, Str $line, Str $format --> Bool) {
    my @w = $line.words;
    given @w[0] {
        when '.tables' {
            my @t = $db.tables;
            if $format eq 'table' {
                say @t.join("\n");
            }
            else {
                emit(Result.new(names => ('name',), rows => @t.map({ ($_,) })), $format);
            }
            return True;
        }
        when '.schema' {
            my @t = @w[1] ?? (@w[1],) !! $db.tables;
            for @t -> $t {
                my $sql = $db.schema-of($t);
                if $sql {
                    say "$sql;";
                }
                else {
                    note "no such table: $t";
                }
            }
            return True;
        }
        when '.read' {
            unless @w[1] {
                note '.read needs a file name';
                return True;
            }
            my $path = @w[1];
            unless $path.IO.e {
                note "no such file: $path";
                return True;
            }
            my $changed = $db.run-script($path.IO.slurp);
            note "$path: ok" if %*ENV<SQLITE_VERBOSE>;
            return True;
        }
        when '.version' {
            say "SQLite {$db.version}";
            return True;
        }
        when '.help' {
            say q:to/HELP/.trim-trailing;
                .read <file>       run a whole SQL script
                .tables            list the tables in this database
                .schema [table]    show the CREATE statements
                .version           the libsqlite3 version in use
                .help              this text
                HELP
            return True;
        }
    }
    return False;
}

# ---------- the terminal, also over NativeCall --------------------------
# Raw mode is `tcgetattr` / `cfmakeraw` / `tcsetattr` on fd 0. The termios
# struct is never described here: it is carried in an opaque byte buffer and
# only ever handed back to libc, which is what lets the same code work on a
# 72-byte macOS struct and a 60-byte Linux one. cfmakeraw() does the flag
# clearing that a hand-rolled version would need the layout for.

sub tcgetattr(int32, CArray[uint8] --> int32)        is native { * }
sub tcsetattr(int32, int32, CArray[uint8] --> int32) is native { * }
sub cfmakeraw(CArray[uint8])                         is native { * }
sub isatty(int32 --> int32)                          is native { * }
sub ioctl(int32, uint64, CArray[uint16] --> int32)   is native { * }

constant TCSAFLUSH = 2;
constant TERMIOS_SIZE = 128;      # comfortably over both layouts

# TIOCGWINSZ is an encoded ioctl number, and the encoding is per-kernel.
sub winsz-request(--> Int) {
    $*KERNEL.name eq 'darwin' ?? 0x40087468 !! 0x5413;
}

sub term-size(--> List) {
    my $ws = CArray[uint16].new(0 xx 8);
    if isatty(1) == 1 && ioctl(1, winsz-request(), $ws) == 0 && $ws[0] > 0 {
        return ($ws[0].Int, $ws[1].Int);
    }
    # No tty, or a pty nobody sized: fall back to the environment.
    ((%*ENV<LINES> // 24).Int, (%*ENV<COLUMNS> // 80).Int);
}

my $saved-termios;

sub raw-mode-on() {
    my $t = CArray[uint8].new(0 xx TERMIOS_SIZE);
    return False if tcgetattr(0, $t) != 0;
    $saved-termios = CArray[uint8].new((^TERMIOS_SIZE).map({ $t[$_] }));
    cfmakeraw($t);
    tcsetattr(0, TCSAFLUSH, $t) == 0;
}

sub raw-mode-off() {
    tcsetattr(0, TCSAFLUSH, $saved-termios) if $saved-termios.defined;
    $saved-termios = Nil;
}

# ---------- ANSI ---------------------------------------------------------

constant ESC = "\e";
sub alt-screen(Bool $on) {
    return unless isatty(1) == 1;      # never spray escapes into a pipe
    print $on ?? "{ESC}[?1049h{ESC}[?25l" !! "{ESC}[?25h{ESC}[?1049l";
}
sub at(Int $row, Int $col) { print "{ESC}[{$row};{$col}H" }
sub clear-line() { print "{ESC}[K" }
sub reverse-on()  { print "{ESC}[7m" }
sub dim-on()      { print "{ESC}[2m" }
sub bold-on()     { print "{ESC}[1m" }
sub attr-off()    { print "{ESC}[0m" }

# Cut a string to a column budget and pad it out, so every pane keeps its
# width no matter what the data does.
sub fit(Str() $s, Int $w --> Str) {
    return ' ' x $w if $w <= 0;
    my $flat = $s.subst(/\n/, ' ', :g);
    $flat.chars > $w ?? ($w > 1 ?? $flat.substr(0, $w - 1) ~ '…' !! '…')
                     !! $flat ~ ' ' x ($w - $flat.chars);
}

# One keystroke, with the arrow-key escape sequences folded into names. Keys
# are read a BYTE at a time, because an arrow key arrives as a three-byte
# escape sequence that has to be recognised before it can be decoded.

sub next-byte(--> Int) {
    my $b = $*IN.read(1);
    $b && $b.bytes ?? $b[0].Int !! -1;
}

# A typed character may be several bytes of UTF-8; gather the continuations.
sub finish-utf8(Int $lead --> Str) {
    my $extra = do given $lead {
        when 0xc0 .. 0xdf { 1 }
        when 0xe0 .. 0xef { 2 }
        when 0xf0 .. 0xf7 { 3 }
        default           { 0 }
    };
    my @bytes = $lead;
    for ^$extra {
        my $b = next-byte();
        last if $b < 0;
        @bytes.push($b);
    }
    my $s = try Buf.new(@bytes).decode('utf8');
    $s // '';
}

sub read-key(--> Str) {
    my $c = next-byte();
    return 'q' if $c < 0;                     # stdin closed: treat as quit
    if $c == 0x1b {
        my $n = next-byte();
        return 'esc' if $n < 0;
        if $n == 0x5b || $n == 0x4f {         # '[' or 'O'
            my $f = next-byte();
            return 'esc' if $f < 0;
            given $f.chr {
                when 'A' { return 'up'    }
                when 'B' { return 'down'  }
                when 'C' { return 'right' }
                when 'D' { return 'left'  }
                when 'H' { return 'home'  }
                when 'F' { return 'end'   }
                when '5' { next-byte(); return 'pgup' }   # swallow the '~'
                when '6' { next-byte(); return 'pgdn' }
                default  { return 'esc' }
            }
        }
        return 'esc';
    }
    return 'enter'     if $c == 13 || $c == 10;
    return 'backspace' if $c == 127 || $c == 8;
    return 'tab'       if $c == 9;
    return 'ctrl-c'    if $c == 3;
    return finish-utf8($c) if $c >= 0x80;
    $c.chr;
}

# ---------- the browser --------------------------------------------------
# Two panes: the table list on the left, a scrolling grid on the right. A
# table is paged straight out of SQLite with LIMIT/OFFSET, so opening a
# million-row table costs one screenful; an ad-hoc query is read once and
# paged in memory.

sub prompt-line(Int $row, Str $label --> Str) {
    my $buf = '';
    loop {
        at($row, 1);
        clear-line();
        print $label ~ $buf;
        $*OUT.flush;
        given read-key() {
            when 'enter'            { return $buf }
            when 'esc' | 'ctrl-c'   { return Str  }
            when 'backspace'        { $buf = $buf.chars ?? $buf.substr(0, *-1) !! $buf }
            default                 { $buf ~= $_ if .chars == 1 }
        }
    }
}

sub browse(DB $db) {
    my @tables = $db.tables;
    unless @tables {
        note "{$db.path}: no tables to browse";
        return;
    }

    my $sel     = 0;          # highlighted entry in the table list
    my $pane    = 'list';     # which pane has the keyboard
    my $source  = @tables[0]; # table name, or the SQL of an ad-hoc query
    my $is-sql  = False;
    my $offset  = 0;
    my $coff    = 0;          # first visible column
    my @all;                  # ad-hoc query results live here
    my $status  = 'Enter opens a table · Tab switches pane · : runs SQL · q quits';
    my ($rows, $cols) = term-size();
    my $page = max 1, $rows - 5;

    my $names := my @names;
    my @page-rows;
    my $total = 0;

    my sub reload() {
        if $is-sql {
            $total = @all.elems;
            @page-rows = @all[$offset ..^ min($offset + $page, $total)];
        }
        else {
            $total = $db.query(qq{select count(*) from "$source"}).rows[0][0];
            my $r = $db.query(qq{select * from "$source" limit $page offset $offset});
            @names = $r.names;
            @page-rows = $r.rows;
        }
    }

    my sub open-table(Str $t) {
        $source = $t; $is-sql = False; $offset = 0; $coff = 0;
        reload();
        $status = "table $t";
        CATCH { default { $status = .message } }
    }

    my sub run-sql(Str $sql) {
        my $r = $db.query($sql);
        @names = $r.names;
        @all = $r.rows;
        $source = $sql; $is-sql = True; $offset = 0; $coff = 0;
        reload();
        $status = @names ?? "query · {$total} row{$total == 1 ?? '' !! 's'}"
                         !! "ok · {$db.changes} row(s) changed";
        @tables = $db.tables;      # the SQL may have created or dropped one
        CATCH { default { $status = .message } }
    }

    my sub draw() {
        ($rows, $cols) = term-size();
        $page = max 1, $rows - 5;
        my $listw = min 22, max 12, ($cols / 4).Int;
        my $gridw = $cols - $listw - 3;

        print "{ESC}[2J";
        # header
        at(1, 1);
        reverse-on();
        print fit(" sqlite · {$db.path} · SQLite {$db.version}", $cols);
        attr-off();

        # left pane: the tables
        for ^($rows - 3) -> $i {
            at($i + 2, 1);
            my $t = @tables[$i];
            if $t.defined {
                my $marker = (!$is-sql && $t eq $source) ?? '▸' !! ' ';
                reverse-on() if $pane eq 'list' && $i == $sel;
                print fit(" $marker$t", $listw);
                attr-off();
            }
            else {
                print ' ' x $listw;
            }
            print ' │ ';
        }

        # right pane: the grid, with widths measured on what is on screen
        my @w = @names.kv.map(-> $i, $n {
            min 28, max $n.chars, |@page-rows.map({ (.[$i].defined ?? cell(.[$i]) !! 'NULL').chars });
        });
        my sub layout(@cells, @vals?) {
            my $line = '';
            my $used = 0;
            for $coff ..^ @names.elems -> $i {
                my $w = @w[$i];
                last if $used + $w + 2 > $gridw;
                $line ~= fit(@cells[$i] // '', $w) ~ '  ';
                $used += $w + 2;
            }
            $line;
        }

        at(2, $listw + 4);
        bold-on();
        print fit(layout(@names), $gridw);
        attr-off();

        for ^($rows - 4) -> $i {
            at($i + 3, $listw + 4);
            # An array element is a container, so the row arrives itemized:
            # .list is how you ask for the values inside it.
            my @row = (@page-rows[$i] // ()).list;
            if @row {
                my @cells = @row.map({ $_.defined ?? cell($_) !! 'NULL' });
                reverse-on() if $pane eq 'grid' && $i == 0;
                print fit(layout(@cells), $gridw);
                attr-off();
            }
            else {
                print ' ' x $gridw;
            }
        }

        # status
        at($rows, 1);
        reverse-on();
        my $where = $total
            ?? "{$offset + 1}-{min($offset + @page-rows.elems, $total)} of $total"
            !! 'empty';
        print fit(" $where · $status", $cols);
        attr-off();
        $*OUT.flush;
    }

    open-table(@tables[0]);
    loop {
        draw();
        my $k = read-key();
        given $k {
            when 'q' | 'ctrl-c' { last }
            when 'tab'   { $pane = $pane eq 'list' ?? 'grid' !! 'list' }
            when 'up' {
                if $pane eq 'list' {
                    $sel = max 0, $sel - 1;
                }
                else {
                    $offset = max 0, $offset - 1;
                    reload();
                }
            }
            when 'down' {
                if $pane eq 'list' {
                    $sel = min @tables.end, $sel + 1;
                }
                else {
                    $offset += 1 if $offset + $page < $total;
                    reload();
                }
            }
            when 'pgup'  { $offset = max 0, $offset - $page; reload() }
            when 'pgdn'  { $offset += $page if $offset + $page < $total; reload() }
            when 'home'  { $offset = 0; reload() }
            when 'end'   { $offset = max 0, $total - $page; reload() }
            when 'left'  { $coff = max 0, $coff - 1 }
            when 'right' { $coff = min max(0, @names.end), $coff + 1 }
            when 'enter' {
                if $pane eq 'list' {
                    open-table(@tables[$sel]);
                    $pane = 'grid';
                }
            }
            when ':' {
                my $sql = prompt-line($rows, 'sql> ');
                run-sql($sql) if $sql.defined && $sql.trim;
            }
            when 's' {
                unless $is-sql {
                    $status = $db.schema-of($source).subst(/\s+/, ' ', :g);
                }
            }
        }
        CATCH { default { $status = .message } }
    }
}

# ---------- entry point --------------------------------------------------

sub MAIN(
    Str  $database,                      #= path to the .db file (or :memory:)
    *@sql,                               #= SQL to run, or a .command; omit for the browser
    Str  :$format = 'table',             #= table, csv, json or list
    Bool :$readonly = False,             #= open the database read-only
    Bool :$create = True,                #= create the file if it is missing
    Bool :$echo = False,                 #= with `-`, print each statement first
) {
    my $db = DB.open($database, :$readonly, :$create);
    LEAVE { .close with $db }

    unless @sql {
        unless isatty(0) == 1 && isatty(1) == 1 {
            note "usage: sqlite.raku <database> <sql|.command>   (a terminal is needed for the browser)";
            exit 2;
        }
        alt-screen(True);
        raw-mode-on();
        LEAVE { raw-mode-off(); alt-screen(False) }
        browse($db);
        return;
    }

    # `-` means: read a script from stdin, the way `sqlite3 db < script.sql`
    # does. Lines are gathered until sqlite3_complete() says the statement is
    # terminated, so a statement may span as many lines as it likes and a
    # semicolon inside a string or a comment does not end it early. A dot
    # command is a line of its own. Failures are reported and
    # the run continues, so one bad statement does not abandon the rest.
    if @sql.elems == 1 && @sql[0] eq '-' {
        my $buffer = '';

        my sub run-one(Str $sql) {
            say "-- $sql" if $echo;
            # Flush every statement's output before the next one runs: stderr
            # is unbuffered, so without this a later error can overtake an
            # earlier result and the two engines interleave differently.
            $*OUT.flush;
            my $err;
            {
                dot-command($db, $sql, $format) || emit($db.query($sql), $format);
                CATCH { default { $err = $_ } }
            }
            if $err {
                note .message with $err;
                $*ERR.flush;      # Rakudo holds stderr to the end otherwise
            }
        }

        for $*IN.lines -> $line {
            my $trimmed = $line.trim;
            if !$buffer {
                next unless $trimmed;
                next if $trimmed.starts-with('--');
                if $trimmed.starts-with('.') {
                    run-one($trimmed);
                    next;
                }
            }
            $buffer ~= ($buffer ?? "\n" !! '') ~ $line;
            # sqlite3_complete() does not parse: it reports whether the text
            # ends in a semicolon that is not inside a string, a comment or a
            # CREATE TRIGGER body. So a statement runs when it is terminated,
            # exactly as in the real shell, and whatever is left when the
            # input ends is run too — which is what makes `echo 'select 1' |
            # sqlite.raku db -` work without a semicolon.
            next unless sqlite3_complete($buffer) == 1;
            run-one($buffer.trim);
            $buffer = '';
        }
        run-one($buffer.trim) if $buffer.trim;
        return;
    }

    # A slurpy positional swallows anything that follows it, so an option
    # written after the database arrives here as SQL — and since it starts
    # with `--`, SQLite reads it as a comment and reports an empty statement.
    # Say what actually went wrong instead.
    if @sql[0].starts-with('--') {
        note "options go before the database: sqlite.raku {@sql[0]} <database> <sql>";
        exit 2;
    }

    my $statement = @sql.join(' ');
    return if dot-command($db, $statement, $format);
    emit($db.query($statement), $format);

    CATCH {
        when X::SQLite {
            note .message;
            exit 1;
        }
    }
}
