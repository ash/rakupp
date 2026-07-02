# Minimal DBIish shim for rakupp: drives the `mysql` CLI via run().
# Pragmatic — connects to the local covid test instance over a unix socket.
# Covers the API covid.observer uses: connect / prepare / execute / allrows / finish.

my $SOCKET = '/tmp/covid.sock';
my $MYSQL  = '/usr/local/bin/mysql';

class DBIish::StatementHandle {
    has $.dbh;
    has $.sql;
    has @.cols;
    has @.rows;

    method execute(*@args) {
        my $q = $!dbh.interpolate($!sql, @args);
        my $out = $!dbh.run-raw($q);
        my @lines = $out.lines;
        @!cols = ();
        @!rows = ();
        if @lines.elems {
            @!cols = @lines[0].split("\t");
            for @lines[1 .. *] -> $line { @!rows.push($line.split("\t")); }
        }
        return self;
    }
    method allrows(:$array-of-hash) {
        return @!rows unless $array-of-hash;
        my @out;
        for @!rows -> @r {
            my %h;
            for 0 ..^ @!cols.elems -> $i { %h{@!cols[$i]} = @r[$i]; }
            @out.push(%h);
        }
        return @out;
    }
    method finish() { return True; }
}

class DBIish::Connection {
    has $.database;

    method run-raw($sql) {
        my $res = run($MYSQL, "--socket=$SOCKET", "-uroot", "--batch", "--raw",
                      $!database, "-e", $sql, :out);
        return $res.out.slurp(:close);
    }
    method quote($v) {
        my $s = ~$v;
        $s = $s.subst('\\', '\\\\', :g);
        $s = $s.subst("'", "\\'", :g);
        return "'$s'";
    }
    method interpolate($sql, @args) {
        my $out = '';
        my $i = 0;
        for $sql.comb -> $c {
            if $c eq '?' { $out = $out ~ self.quote(@args[$i]); $i = $i + 1; }
            else { $out = $out ~ $c; }
        }
        return $out;
    }
    method execute($sql, *@args) {
        my $q = self.interpolate($sql, @args);
        self.run-raw($q);
        return self;
    }
    method prepare($sql) { return DBIish::StatementHandle.new(dbh => self, sql => $sql); }
    method do($sql) { self.run-raw($sql); return self; }
    method dispose() { return True; }
}

class DBIish {
    method connect($driver, *%opts) {
        return DBIish::Connection.new(database => (%opts<database> // 'covid'));
    }
}
