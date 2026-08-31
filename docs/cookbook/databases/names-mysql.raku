#!/usr/bin/env raku
# One program, three databases — the MySQL version (MariaDB uses this driver too).
#   rakupp docs/recipes/databases/names-mysql.raku
#
# Change the five connection values below to match your server. The SQL that
# creates the database and user this expects is in the recipe.

use DBIish;

my $dbh = DBIish.connect('mysql',
    :host<127.0.0.1>, :port(3306),
    :database<raku_recipes>, :user<raku_user>, :password<raku_pass>);

$dbh.execute('drop table if exists names');
$dbh.execute(q:to/SQL/);
    create table names (
        id   int auto_increment primary key,
        name varchar(50)
    )
    SQL

# A placeholder per value, rather than interpolating into the SQL. The driver
# sends the value separately, so quoting is its problem and not ours.
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
