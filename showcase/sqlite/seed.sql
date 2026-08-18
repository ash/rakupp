-- The demo database: a tiny music catalogue, built so that every SQLite
-- storage class shows up somewhere — integers, floats that are exact in
-- binary, text with commas/quotes/newlines/non-ASCII, blobs, and NULLs.
create table artists (
    id      integer primary key,
    name    text not null,
    country text,
    founded integer
);

create table albums (
    id        integer primary key,
    artist_id integer not null references artists(id),
    title     text not null,
    year      integer,
    rating    real,
    cover     blob,
    notes     text
);

create table tracks (
    id       integer primary key,
    album_id integer not null references albums(id),
    position integer not null,
    title    text not null,
    seconds  integer
);

insert into artists (id, name, country, founded) values
    (1, 'Kraftwerk',        'DE', 1970),
    (2, 'Portishead',       'GB', 1991),
    (3, 'Sigur Rós',        'IS', 1994),
    (4, 'Émilie Simon',     'FR', 2001),
    (5, 'Unknown Artist',   NULL, NULL);

insert into albums (id, artist_id, title, year, rating, cover, notes) values
    (1, 1, 'Autobahn',            1974, 4.5,  x'89504e47', 'side one is one track'),
    (2, 1, 'Trans-Europe Express', 1977, 4.75, x'cafe01',   'title has a hyphen, and a comma here'),
    (3, 2, 'Dummy',               1994, 5.0,  NULL,        'said to be "trip hop"'),
    (4, 3, 'Ágætis byrjun',       1999, 4.25, NULL,        'a note
spanning two lines'),
    (5, 4, 'Végétal',             2006, 3.5,  NULL,        NULL),
    (6, 5, 'Untitled',            NULL, NULL, NULL,        NULL);

insert into tracks (album_id, position, title, seconds) values
    (1, 1, 'Autobahn',              1425),
    (1, 2, 'Kometenmelodie 1',       375),
    (1, 3, 'Mitternacht',            287),
    (2, 1, 'Europe Endless',         589),
    (2, 2, 'The Hall of Mirrors',    468),
    (2, 3, 'Trans-Europe Express',   397),
    (3, 1, 'Mysterons',              305),
    (3, 2, 'Sour Times',             251),
    (3, 3, 'Strangers',              238),
    (4, 1, 'Svefn-g-englar',         596),
    (4, 2, 'Starálfur',              406),
    (5, 1, 'Fleur de saison',        228),
    (6, 1, 'Untitled',               NULL);
