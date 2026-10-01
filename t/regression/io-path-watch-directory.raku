# Regression: `IO::Path.watch` on a DIRECTORY died "Watching a directory is not
# yet implemented", and a file's events were plain Hashes. `cro run` watches every
# directory of a project (Cro::Tools::Services' watch-recursive), reads `.path`
# from each event and builds IO::Notification::Change objects of its own — issue
# #115. A directory's events are now its entries' changes, as
# IO::Notification::Change with an absolute path and a FileChangeEvent.
# Contract: exit 0 + last line PASS.
my @fail;

my $dir = $*TMPDIR.add("rakupp-watch-dir-$*PID");
$dir.mkdir;
LEAVE { .unlink for $dir.dir; $dir.rmdir }

my @events;
my $tap = $dir.watch.tap({ @events.push($_) });
$dir.add("new.txt").spurt("one");
sleep 0.5;
$tap.close;

for @events {
    @fail.push("event is a {.^name}") unless $_ ~~ IO::Notification::Change;
    @fail.push("event kind {.event.raku}") unless .event ~~ FileChangeEvent;
    @fail.push("path {.path} is not absolute") unless .path.IO.is-absolute;
}
# (Rakudo's macOS notifier may also report the directory's own creation)
@fail.push("no event for the new file") unless @events.grep(*.path.IO.basename eq 'new.txt');

# a content change, as each platform's notifier reports it through Rakudo:
# macOS's FSEvents (via libuv) calls everything in a directory FileRenamed,
# Linux's inotify tells a modification apart
my @edits;
my $etap = $dir.watch.tap({ @edits.push($_) if .path.IO.basename eq 'new.txt' });
sleep 0.3;
$dir.add("new.txt").spurt("two");
sleep 0.5;
$etap.close;
my $want = $*KERNEL.name eq 'darwin' ?? FileRenamed !! FileChanged;
@fail.push("edit reported as {@edits».event}") unless @edits && @edits.all.event == $want;

# the pieces Cro builds itself
my $c = IO::Notification::Change.new(path => "p", event => FileChanged);
@fail.push("gist {$c.gist}") unless $c.gist eq 'p: FileChanged';
@fail.push("enum list") unless FileChangeEvent.^enum_value_list eqv (FileChanged, FileRenamed);

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
