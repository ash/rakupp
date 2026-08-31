#!/usr/bin/env raku
# One program, three databases — the SQLite version.
#   rakupp docs/recipes/databases/names-sqlite.raku
#
# SQLite needs no server: the database is the file named below, and the driver
# creates it. The MySQL and PostgreSQL versions beside this one differ only in
# the connect call and in the two words that spell an auto-incrementing key.

use DBIish;

my $dbfile = 'test_names.sqlite3';

# :database($dbfile), NOT :database<$dbfile> — angle brackets are quote-words
# and do not interpolate, so the second form opens a database whose name is
# literally '$dbfile'.
my $dbh = DBIish.connect('SQLite', :database($dbfile));

$dbh.execute('drop table if exists names');
$dbh.execute(q:to/SQL/);
    create table names (
        id   integer primary key autoincrement,
        name varchar(50)
    )
    SQL

# A placeholder per value, rather than interpolating into the SQL. The driver
# sends the value separately, so quoting is its problem and not ours.
my $ins = $dbh.prepare('insert into names (name) values (?)');
$ins.execute("My name is nr. $_") for 1 .. 3;

my $sth = $dbh.prepare('select id, name from names order by id');
$sth.execute;

# .elems on what came back, not $sth.rows: SQLite cannot report a row count for
# a SELECT and answers 0 with a warning.
my @rows = $sth.allrows;
say "{@rows.elems} rows found";
say "id: {$_[0]}, name: {$_[1]}" for @rows;

$sth.dispose;
$ins.dispose;
$dbh.dispose;
$dbfile.IO.unlink;
