#!/usr/bin/env raku
# One program, three databases — the PostgreSQL version.
#   rakupp docs/recipes/databases/names-pg.raku
#
# Change the five connection values below to match your server. The SQL that
# creates the database and role this expects is in the recipe.
#
# If this reports `DBDish::Pg needs 'pq', not found` with a list of paths that
# were tried, libpq is not on the loader's search path — Homebrew's
# postgresql@17 is keg-only, so it never reaches /usr/local/lib. There is no
# environment variable for the library itself; name the directory instead:
#
#   DYLD_LIBRARY_PATH=/usr/local/lib/postgresql@17 rakupp names-pg.raku
#
# (LD_LIBRARY_PATH on Linux.)

use DBIish;

my $dbh = DBIish.connect('Pg',
    :host<localhost>, :port(5432),
    :database<raku_recipes>, :user<raku_user>, :password<raku_pass>);

# PostgreSQL writes a NOTICE to stderr when there is no such table to drop.
# It is a notice, not an error: stdout is unaffected.
$dbh.execute('drop table if exists names');
$dbh.execute(q:to/SQL/);
    create table names (
        id   serial primary key,
        name varchar(50)
    )
    SQL

# A placeholder per value, rather than interpolating into the SQL. PostgreSQL
# spells placeholders $1, $2 …; the driver rewrites the ? for us, so the same
# statement works on all three engines.
my $ins = $dbh.prepare('insert into names (name) values (?)');
$ins.execute("My name is nr. $_") for 1 .. 3;

my $sth = $dbh.prepare('select id, name from names order by id');
$sth.execute;

my @rows = $sth.allrows;
say "{@rows.elems} rows found";
say "id: {$_[0]}, name: {$_[1]}" for @rows;

$sth.dispose;
$ins.dispose;
$dbh.execute('drop table names');
$dbh.dispose;
