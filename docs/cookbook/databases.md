# Cookbook — one program, three databases

Reading and writing a table with [DBIish](https://github.com/raku-community-modules/DBIish),
the ecosystem's database interface, against SQLite, MySQL and PostgreSQL.

Every program on this page was run under Raku++ against a real server of each
kind. **All three print the same four lines**, and the differences between them
come to two: the arguments to `connect`, and the two words that spell an
auto-incrementing primary key.

## Getting DBIish

One distribution serves all three engines:

```sh
rakupp install DBIish
```

The client library is a separate matter — DBIish calls it through NativeCall
and does not ship it. `libsqlite3` is already on macOS and on most Linux
systems; MySQL and PostgreSQL want `libmysqlclient` and `libpq`, which arrive
with the server packages or with a client-only package.

## SQLite

SQLite needs no server and no credentials. The database is a file, and the
driver creates it:

```raku
use DBIish;

my $dbfile = 'test_names.sqlite3';

my $dbh = DBIish.connect('SQLite', :database($dbfile));

$dbh.execute('drop table if exists names');
$dbh.execute(q:to/SQL/);
    create table names (
        id   integer primary key autoincrement,
        name varchar(50)
    )
    SQL

my $ins = $dbh.prepare('insert into names (name) values (?)');
$ins.execute("My name is nr. $_") for 1 .. 3;

my $sth = $dbh.prepare('select id, name from names order by id');
$sth.execute;

my @rows = $sth.allrows;
say "{@rows.elems} rows found";
say "id: {$_[0]}, name: {$_[1]}" for @rows;

$sth.dispose;
$ins.dispose;
$dbh.dispose;
$dbfile.IO.unlink;
```

```output
3 rows found
id: 1, name: My name is nr. 1
id: 2, name: My name is nr. 2
id: 3, name: My name is nr. 3
```

The whole file is [databases/names-sqlite.raku](databases/names-sqlite.raku).

## MySQL

Same program. The connection now needs a server, and `int auto_increment`
replaces `integer … autoincrement`:

```raku
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
```

Everything from `prepare` onwards is unchanged, and so is the output. The full
program is [databases/names-mysql.raku](databases/names-mysql.raku); the same
driver serves MariaDB.

## PostgreSQL

The driver is `Pg`, and the auto-incrementing key is a `serial`:

```raku
my $dbh = DBIish.connect('Pg',
    :host<localhost>, :port(5432),
    :database<raku_recipes>, :user<raku_user>, :password<raku_pass>);

$dbh.execute('drop table if exists names');
$dbh.execute(q:to/SQL/);
    create table names (
        id   serial primary key,
        name varchar(50)
    )
    SQL
```

Again the rest is identical: [databases/names-pg.raku](databases/names-pg.raku).

One thing to expect from PostgreSQL and not from the other two — dropping a
table that is not there is a notice, and libpq writes notices to **stderr**:

```output
NOTICE:  table "names" does not exist, skipping
```

The program's stdout is unaffected, which is why all three still produce
identical output.

## What actually differs

| | SQLite | MySQL | PostgreSQL |
|---|---|---|---|
| driver name | `SQLite` | `mysql` | `Pg` |
| server | none — a file | host, port, user, password | host, port, user, password |
| auto-increment key | `integer primary key autoincrement` | `int auto_increment primary key` | `serial primary key` |
| placeholder written | `?` | `?` | `?` |
| placeholder sent | `?` | `?` | `$1`, `$2` … |
| `$sth.rows` after SELECT | `0`, and a warning | `3` | `3` |
| notices | none | none | on stderr |

The placeholder row is the useful one: PostgreSQL's wire protocol numbers its
parameters, but the driver rewrites `?` on the way to `PQprepare`, so one
statement string works on all three.

## Getting rows out

The four shapes DBIish offers are the same on every driver. Against the table
above they give:

```raku
$sth.allrows;                  # [[1, "nr. 1"], [2, "nr. 2"], [3, "nr. 3"]]
$sth.allrows(:array-of-hash);  # ({:id(1), :name("nr. 1")}, …)  — a Seq
$sth.row;                      # [1, "nr. 1"]                   — one row
$sth.row(:hash);               # {:id(1), :name("nr. 1")}
```

`allrows` returns an Array and reads the whole result set; `row` returns one
row at a time and an empty list when there are none left, so it is the one to
use on a result set you would rather not hold in memory. Re-run `$sth.execute`
before fetching again.

## Three things that bite

**`:database<$file>` does not interpolate.** Angle brackets are quote-words, so

```raku
my $dbfile = 'test_names.sqlite3';
DBIish.connect('SQLite', :database<$dbfile>);   # wrong
```

opens a database literally called `$dbfile` — and, since that name has no
extension, the SQLite driver appends one, leaving a `$dbfile.sqlite3` in the
directory. Write `:database($dbfile)`. Nothing complains, which is what makes
this one worth knowing.

**`$sth.rows` is not the row count of a SELECT.** SQLite cannot supply one, and
says so on stderr:

```output
SQLite rows() result may not be accurate. See SQLite rows section of README for details.
```

It then answers `0` while the rows themselves come back perfectly well. MySQL
and PostgreSQL do answer `3`, so a program that reads `.rows` works on two
engines out of three — the failure is silent apart from that warning. Use
`@rows.elems` after `allrows`.

It is only the SELECT that SQLite cannot count. After an INSERT, UPDATE or
DELETE, `.rows` is the number of rows affected on all three: an `update`
touching three of four rows answers `3` under every driver.

**Do not interpolate values into SQL.** `"… values ('$name')"` builds a
different statement for every value, breaks on an apostrophe, and is the
injection hole. A `?` and a value passed to `execute` avoid all three, and let
the server reuse the prepared plan.

## When a driver cannot find its library

DBIish loads the client library through NativeCall at connect time, so this is
where most first runs stop. The message has three shapes and they mean
different things.

**A name and a list of paths tried** — the library exists but is not where the
loader looks. Homebrew's `postgresql@17` is keg-only, so libpq stays inside the
keg and never reaches `/usr/local/lib`:

```output
DBIish: DBDish::Pg needs 'pq', not found.
	Detail: Cannot locate native library 'pq': dlopen(pq, 0x0009): tried: 'pq' (no such file), '/System/Volumes/Preboot/Cryptexes/OSpq' (no such file), '/usr/lib/pq' (no such file, not in dyld cache), 'pq' (no such file)
```

There is no `DBIISH_PG_LIB` to point at it — `NativeLibs::Searcher.at-runtime`
takes a name, not a path — so the library has to become findable instead. For
one run, name the directory:

```sh
DYLD_LIBRARY_PATH=/usr/local/lib/postgresql@17 rakupp names-pg.raku   # LD_LIBRARY_PATH on Linux
```

Permanently, put it where the loader already looks: link the keg, or symlink
`libpq.5.dylib` into a directory on the default search path.

**An empty name** — the driver probed and nothing matched. This is MySQL's,
and only MySQL's: its driver takes no library path at all but goes by soname,
`mariadb` versions 0 to 4 and then `mysqlclient` 16 to 21. Homebrew's MySQL 9.7
installs `libmysqlclient.24.dylib`, outside the range, so there is no candidate
and nothing to name:

```output
DBIish: DBDish::mysql needs '', not found.
	Detail: Cannot locate symbol 'mysql_init' in native library ''
```

Here there *is* an environment variable, and naming the file stops the guessing:

```sh
DBIISH_MYSQL_LIB=/usr/local/opt/mysql/lib/libmysqlclient.24.dylib rakupp names-mysql.raku
```

**`incompatible architecture`** — found, and refused. The interpreter and the
client library have to match. On a Mac with an Intel Homebrew under
`/usr/local`, `libmysqlclient` and `libpq` are x86_64, so an arm64 `rakupp`
cannot open either:

```output
DBIish: DBDish::Pg needs 'pq', not found.
	Detail: … (mach-o file, but is an incompatible architecture (have 'x86_64', need 'arm64e' or 'arm64'))
```

Use the interpreter build that matches the library.

The catch is that the MySQL driver cannot tell you which of these it hit. A
candidate that will not load is simply a candidate that did not match, so an
architecture mismatch there prints the same empty-name line as a soname out of
range. The PostgreSQL driver quotes dlopen and names the architecture; the
MySQL one reports only that the search came up empty, so rule the two out in
order — set the variable first, then check the architecture.

## Setting up a server to run these against

The MySQL and PostgreSQL programs expect a database `raku_recipes` reachable by
user `raku_user` with password `raku_pass`. To create exactly that:

```sql
-- PostgreSQL, as a superuser
CREATE ROLE raku_user LOGIN PASSWORD 'raku_pass';
CREATE DATABASE raku_recipes OWNER raku_user ENCODING 'UTF8' TEMPLATE template0;
```

```sql
-- MySQL, as root
CREATE DATABASE raku_recipes CHARACTER SET utf8mb4;
CREATE USER 'raku_user'@'%' IDENTIFIED BY 'raku_pass';
GRANT ALL PRIVILEGES ON raku_recipes.* TO 'raku_user'@'%';
```

`TEMPLATE template0` on the PostgreSQL side is not decoration: a cluster
initialised with a C locale will otherwise hand the new database that locale,
and text outside ASCII stops round-tripping.

SQLite needs none of this — the file is the database.

## Closing things

`dispose` releases a statement handle or a connection at a point you choose,
rather than whenever the last reference goes away:

```raku
$sth.dispose;
$dbh.dispose;
```

DBIish connects with `:RaiseError` on by default, so a failed statement throws
rather than returning a false value that the next line ignores.
