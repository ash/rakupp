# Telemetry — an API for keeping track of a program's lifesigns: `T<max-rss>`,
# `snap`, `snapper`, `periods` and `report`, as Rakudo's core module of the
# same name has them (rakudo#1513, #3844, #5144).
#
# A port of Rakudo's lib/Telemetry.rakumod (The Artistic License 2.0, like
# Raku++ itself) to plain Raku: Raku++ has no nqp:: ops, so the numbers come
# from one built-in, rakupp-getrusage (src/BuiltinsRegister3.cpp), which
# answers getrusage(2)'s fields in MoarVM's order plus the wall-clock
# microseconds since the program began, and the native buffers are Arrays.
# The instruments, columns, formats, legends and the report are Rakudo's.
# Raku++ keeps no thread or scheduler counts, so the Thread and ThreadPool
# instruments read zeros, and a report leaves their columns out, as it does
# any column without a value; `snap(:heap)` has no heap to snapshot.

# the place where the default snaps are stored
my @snaps;

# Role for building instruments ------------------------------------------------
role Telemetry::Instrument {

    # Should return instantiated snap object
    method snap() { ... }

    # Should return a string for the preamble of the report
    method preamble($first, $last, $total, @snaps --> Str:D) { ... }

    # Should return a list of lists with:
    #  [0] name of the column
    #  [1] the printf format of the column (without '%' prefix)
    #  [2] a one-line explanation of the column
    method formats() { ... }

    # Should return the names of the columns to be shown by default
    method default-columns() { ... }

    # Returns all of the possible column names
    method columns() { self.formats.map( *[0] ).sort }
}

# Role for creating an instrument snap -----------------------------------------
role Telemetry::Instrument::Snap does Associative {
    has @.data;

    multi method new(::?CLASS:U:) { self.bless(data => self.take-sample) }
    multi method new(::?CLASS:D: @data) { self.clone(data => @data) }   # a difference
    multi method new(::?CLASS:U: *@data) { self.bless(:@data) }        # for .raku roundtripping

    multi method raku(::?CLASS:D:) { self.^name ~ '.new(' ~ @!data.join(',') ~ ')' }

    # Should return the data of a sample, as a list of integers
    method take-sample() { ... }

    # Should return the value of the given column, or Nil
    method AT-KEY($column) { ... }

    # Should return whether the column exists
    method EXISTS-KEY($column) { ... }
}

# Telemetry data from wallclock and getrusage ----------------------------------
class Telemetry::Instrument::Usage does Telemetry::Instrument {

    method formats() {
        ('cpu',       '8d',   'The total amount of CPU used (in microseconds)'),
        ('cpu-sys',   '8d',   'The amount of CPU used in system overhead (in microseconds)'),
        ('cpu-user',  '8d',   'The amount of CPU used in user code (in microseconds)'),
        ('cpus',      '5.1f', "The number of CPU's that were busy on average"),
        ('id-rss',    '8d',   'Integral unshared data size (in Kbytes)'),
        ('inb',       '4d',   'Number of block input operations'),
        ('invcsw',    '8d',   'Number of involuntary context switches'),
        ('is-rss',    '8d',   'Integral unshared stack size (in Kbytes)'),
        ('ix-rss',    '8d',   'Integral shared text memory size (in Kbytes)'),
        ('majf',      '4d',   'Number of page reclaims'),
        ('max-rss',   '8d',   'Maximum resident set size (in Kbytes)'),
        ('minf',      '4d',   'Number of page reclaims'),
        ('mrcv',      '4d',   'Number of messages received'),
        ('msnd',      '4d',   'Number of messages sent'),
        ('nsig',      '4d',   'Number of signals received'),
        ('nswp',      '4d',   'Number of swaps'),
        ('volcsw',    '6d',   'Number of voluntary context switches'),
        ('outb',      '4d',   'Number of block output operations'),
        ('util%',     '6.2f', 'Percentage of CPU utilization (0..100%)'),
        ('wallclock', '9d',   'Number of microseconds elapsed')
    }

    method default-columns() { < wallclock util% max-rss > }

    method preamble($first, $last, $total, @snaps --> Str:D) {
        my sub _($value) { $value.polymod(1000 xx *).reverse.join("_") || 0 }

        qq:to/HEADER/.chomp;
Initial/Final Size: &_($first<max-rss>) / &_($last<max-rss>) Kbytes
Total Time:      { ($total<wallclock> / 1000000).fmt('%9.2f') } seconds
Total CPU Usage: { ($total<cpu> / 1000000).fmt('%9.2f') } seconds
HEADER
    }

    # actual snapping logic
    class Snap does Telemetry::Instrument::Snap {
        my $cores   = $*KERNEL.cpu-cores;
        my $utilize = 100 / $cores;

        # the getrusage fields, by their index in the data
        my %dispatch;
        for < max-rss ix-rss id-rss is-rss minf majf nswp inb
              outb msnd mrcv nsig volcsw invcsw wallclock >.kv -> $i, $name {
            my $index = $i + 4;   # (the first four are the CPU times)
            %dispatch{$name} = -> @data { @data[$index] }
        }
        %dispatch<cpu-user>  = -> @data { @data[0] * 1000000 + @data[1] }
        %dispatch<cpu-sys>   = -> @data { @data[2] * 1000000 + @data[3] }
        %dispatch<cpu>       = -> @data { %dispatch<cpu-user>(@data) + %dispatch<cpu-sys>(@data) }
        %dispatch<cpus>      = -> @data {
            (my $wallclock = @data[18]) ?? %dispatch<cpu>(@data) / $wallclock !! $cores
        }
        %dispatch<util%>     = -> @data { $utilize * %dispatch<cpus>(@data) }

        method AT-KEY($key) { (%dispatch{$key} // return Nil)(self.data) }
        method EXISTS-KEY($key) { %dispatch{$key}:exists }
        method take-sample() { rakupp-getrusage().Array }
    }

    method snap(--> Snap:D) { Snap.new }
}

# Telemetry data of starting Threads -------------------------------------------
class Telemetry::Instrument::Thread does Telemetry::Instrument {

    method formats() {
        ('tad',  '3d', 'Number of threads that ended with an exception (aborted)'),
        ('tcd',  '3d', 'Number of threads that completed without any problem'),
        ('thid', '4d', 'Highest OS thread ID seen'),
        ('tjd',  '3d', 'Number of threads that were joined'),
        ('tsd',  '3d', 'Number of threads that were started'),
        ('tys',  '4d', 'Number of times a thread was yielded')
    }

    method default-columns() { < tsd tcd tad thid > }

    method preamble($first, $last, $total, @snaps --> Str:D) {
        qq:to/HEADER/.chomp;
OS threads started: { ($last<thid> - $first<thid>).fmt('%4d') }{ " ($first<thid> started earlier)" if $first<thid> }
HEADER
    }

    # actual snapping logic
    class Snap does Telemetry::Instrument::Snap {
        my %dispatch;
        for <tsd tad tcd tjd tys thid>.kv -> $index, $name {
            %dispatch{$name} = -> @data { @data[$index] }
        }

        method AT-KEY($key) { (%dispatch{$key} // return Nil)(self.data) }
        method EXISTS-KEY($key) { %dispatch{$key}:exists }
        method take-sample() { Thread.^can('usage') ?? Thread.usage.Array !! [0 xx 6] }
    }

    method snap(--> Snap:D) { Snap.new }
}

# Telemetry data from the ThreadPoolScheduler ----------------------------------
class Telemetry::Instrument::ThreadPool does Telemetry::Instrument {

    method formats() {
        ('atc', '8d', 'The number of tasks completed in affinity threads'),
        ('atq', '3d', 'The number of tasks queued for execution in affinity threads'),
        ('aw',  '3d', 'The number of affinity threads'),
        ('gtc', '8d', 'The number of tasks completed in general worker threads'),
        ('gtq', '3d', 'The number of tasks queued for execution in general worker threads'),
        ('gw',  '3d', 'The number of general worker threads'),
        ('s',   '1d', 'The number of supervisors'),
        ('ttc', '8d', 'The number of tasks completed in timer threads'),
        ('ttq', '3d', 'The number of tasks queued for execution in timer threads'),
        ('tw',  '3d', 'The number of timer threads')
    }

    method default-columns() { < gw gtc tw ttc aw atc > }

    method preamble($first, $last, $total, @snaps --> Str:D) {
        if $first<s> {
            "Supervisor thread ran the whole time"
        }
        elsif !$last<s> {
            "No supervisor thread has been running"
        }
        else {
            my $started = @snaps.first: *.<s>;
            "Supervisor thread ran for {
              (100 * ($last<wallclock> - $started<wallclock>)
                / $total<wallclock>).fmt("%5.2f")
            }% of the time"
        }
    }

    # actual snapping logic
    class Snap does Telemetry::Instrument::Snap {
        my %dispatch;
        for <s gw gtq gtc tw ttq ttc aw atq atc>.kv -> $index, $name {
            %dispatch{$name} = -> @data { @data[$index] }
        }

        method AT-KEY($key) { (%dispatch{$key} // return Nil)(self.data) }
        method EXISTS-KEY($key) { %dispatch{$key}:exists }
        method take-sample() {
            my $scheduler = $*SCHEDULER;
            $scheduler.^can('usage') ?? $scheduler.usage.Array !! [0 xx 10]
        }
    }

    method snap(--> Snap:D) { Snap.new }
}

# Telemetry::Instrument::AdHoc -------------------------------------------------
class Telemetry::Instrument::AdHoc does Telemetry::Instrument {
    has @!formats;
    has @!columns;
    has @!containers;
    has %!dispatch;

    method new(*@vars is raw) { self.bless!SET-SELF(@vars) }

    # each a variable, or a legend => variable Pair, whose container is read
    # at every snap: its name is the column's (without the sigil, in a Pair)
    method !SET-SELF(@vars) {
        for @vars -> $spec is raw {
            my $index = @!containers.elems;
            my \variable = $spec ~~ Pair ?? $spec.value !! $spec;
            die "Must specify a container" unless variable.VAR ~~ Scalar;
            my $name = $spec ~~ Pair ?? variable.VAR.name.substr(1) !! variable.VAR.name;
            @!formats.push: [$name, "{4 max $name.chars}d", $spec ~~ Pair ?? $spec.key !! ""];
            @!columns.push: $name;
            @!containers.push: variable.VAR;
            %!dispatch{$name} = -> @data { @data[$index] };
        }
        self
    }

    method dispatch() { %!dispatch }
    method containers() { @!containers }

    method preamble($first, $, $, @ --> Str:D) {
        @!columns.map(-> $name {
            "Initial $name.tc(): ".fmt('%-17s') ~ $first{$name}.fmt('%9d')
        }).join("\n")
    }

    # actual snapping logic
    class Snap does Telemetry::Instrument::Snap {
        has $.instrument;

        multi method new(::?CLASS:U: Telemetry::Instrument::AdHoc:D $instrument) {
            self.bless(:$instrument, data => $instrument.containers.map(*.Int).Array)
        }

        method AT-KEY($key) { ($!instrument.dispatch{$key} // return Nil)(self.data) }
        method EXISTS-KEY($key) { $!instrument.dispatch{$key}:exists }
        method take-sample() { $!instrument.containers.map(*.Int).Array }
    }

    method formats() { @!formats }
    method default-columns() { @!columns }
    method snap(--> Snap:D) { Snap.new(self) }
}

# Telemetry::Sampler -----------------------------------------------------------
class Telemetry::Sampler {
    has @.instruments;
    has %.dispatcher;
    has %!formats;

    # helper sub for handling instruments specified with a Str
    my sub Str-instrument($name) {
        my $class := ::("Telemetry::Instrument::$name");
        $class ~~ Failure
          ?? die "Could not find Telemetry::Instrument::$name class"
          !! $class
    }

    method !set-up-instrument($instrument --> Nil) {
        my $class = $instrument ~~ Str ?? Str-instrument($instrument) !! $instrument;
        my $index = @!instruments.elems;
        @!instruments.push($class);
        for $class.formats -> @info {
            my $key = @info[0];
            %!dispatcher{$key} = -> @samples { @samples[$index].AT-KEY($key) };
            %!formats{$key} = @info;
        }
    }

    multi method new(Telemetry::Sampler:) { self.new([]) }
    multi method new(Telemetry::Sampler: @spec) {
        my $self = self.bless;

        # handle instrument specification
        if @spec {
            $self!set-up-instrument($_) for @spec;
        }

        # none specified, but we do have a default in the environment
        elsif %*ENV<RAKUDO_TELEMETRY_INSTRUMENTS> -> $rri {
            $self!set-up-instrument($_) for $rri.comb( /<[\w-]>+/ );
        }

        # no instruments to be found anywhere, use the default default
        else {
            $self!set-up-instrument($_) for
              Telemetry::Instrument::Usage,
              Telemetry::Instrument::ThreadPool;
        }

        $self
    }
    multi method new(Telemetry::Sampler: Mu $instrument) { self.new([$instrument]) }

    method set-instruments(Telemetry::Sampler:D: *@instruments --> Nil) {
        @!instruments = ();
        %!dispatcher = ();
        %!formats = ();
        self!set-up-instrument($_) for @instruments;
        @snaps = ();
    }

    multi method raku(Telemetry::Sampler:D: --> Str:D) {
        self.^name ~ '.new(' ~ @!instruments.map(*.^name).join(",") ~ ')'
    }

    method formats(Telemetry::Sampler:D:) { %!formats.Map }
}

# Make sure we always have a Sampler
PROCESS::<$SAMPLER> := Telemetry::Sampler.new without $*SAMPLER;

# Telemetry --------------------------------------------------------------------
class Telemetry does Associative {
    has $.sampler;
    has @.samples;
    has Str $.message is rw;

    multi method new(Telemetry:) {
        my $sampler = $*SAMPLER;
        self.bless(:$sampler, samples => $sampler.instruments.map(*.snap))
    }
    multi method new(Telemetry: *@samples) {   # needed for .raku roundtripping
        self.bless(sampler => $*SAMPLER, :@samples)
    }

    multi method raku(Telemetry:D: --> Str:D) {
        self.^name ~ ".new" ~ @!samples.List.raku
    }

    method AT-KEY($key) { ($!sampler.dispatcher{$key} // return Nil)(@!samples) }
    method EXISTS-KEY($key) { $!sampler.dispatcher{$key}:exists }

    method FALLBACK(Telemetry:D: $method) {
        self.EXISTS-KEY($method)
          ?? self.AT-KEY($method)
          !! X::Method::NotFound.new(:$method, :typename(self.^name)).throw
    }
}

# Telemetry::Period ------------------------------------------------------------
class Telemetry::Period is Telemetry {
    # Same as Telemetry, but contains differences instead of absolute values
}

# Creating Telemetry::Period objects -------------------------------------------
multi sub infix:<->(Telemetry:U $a, Telemetry:U $b) is export {
    die "Cannot subtract Telemetry type objects";
}
multi sub infix:<->(Telemetry:D $a, Telemetry:U $b --> Telemetry::Period:D) is export {
    $a - $b.new
}
multi sub infix:<->(Telemetry:U $a, Telemetry:D $b --> Telemetry::Period:D) is export {
    $a.new - $b
}
multi sub infix:<->(Telemetry:D $a, Telemetry:D $b --> Telemetry::Period:D) is export {
    my @a = $a.samples;
    my @b = $b.samples;
    die "Different number of samples" if @a.elems != @b.elems;

    # each sample's difference, of the same instrument's Snap
    my @samples = (^@a).map: -> $i {
        @a[$i].new((@a[$i].data Z- @b[$i].data).Array)
    };

    my $period = Telemetry::Period.bless(sampler => $a.sampler, :@samples);
    $period.message = $_ with $a.message;
    $period
}

# Making a Telemetry object procedurally ---------------------------------------
proto sub snap(|) is export {*}
multi sub snap(--> Nil) {
    @snaps.push(Telemetry.new);
}
multi sub snap(Str:D $message --> Nil) {
    my $T = Telemetry.new;
    $T.message = $message;
    @snaps.push($T);
}
multi sub snap(Str $message = "taking heap snapshot...", :$heap!) {
    unless $heap {
        $message eq "taking heap snapshot..." ?? snap() !! snap($message);
        return
    }
    die "Raku++ cannot take a heap snapshot (snap(:heap) needs MoarVM's profiler)";
}
multi sub snap(@s --> Nil) {
    @s.push(Telemetry.new);
}

# Starting the snapper / changing the period size
my $snapper-running = 0;
my $snapper-wait;
sub snapper($sleep = 0.1, :$stop, :$reset --> Nil) is export {

    $snapper-wait = $sleep;
    @snaps = () if $reset;

    if $snapper-running {
        $snapper-running = 0 if $stop;
    }
    elsif !$stop {
        $snapper-running = 1;
        Thread.start(:app_lifetime, :name<Snapper>, {
            snap;
            while $snapper-running {
                sleep $snapper-wait;
                snap if $snapper-running;
            }
        });
    }
}

# Telemetry::Period objects from a list of Telemetry objects -------------------
proto sub periods(|) is export {*}
multi sub periods() {
    my @new = @snaps;
    @snaps = ();
    @new.push(Telemetry.new) if @new.elems == 1;
    periods(@new)
}
multi sub periods(@s) { (1..^@s).map: { @s[$_] - @s[$_ - 1] } }

# Telemetry reporting features -------------------------------------------------
proto sub report(|) is export {*}
multi sub report(*%_ --> Str:D) {
    report(@snaps.List, |%_)
}

# some constants for the %format list
my constant NAME    = 0; # short name
my constant FORMAT  = 1; # format (without % prefixed)
my constant LEGEND  = 2; # legend
my constant HEADER  = 3; # generated: column header
my constant FOOTER  = 4; # generated: column footer
my constant DISPLAY = 5; # generated: code to execute to display

my sub prepare-format(@raw, %format --> Nil) {
    for @raw -> @in {
        my @info   = @in;
        my $name   = @info[NAME];
        my $format = @info[FORMAT];
        my $width  = +$format.match(/^ \d+ /);   # the width before any precision
        my $empty  = " " x $width;

        @info[HEADER]  = $name.fmt("%{$width}s");
        @info[FOOTER]  = "-" x $width;
        @info[DISPLAY] = -> \value { value ?? value.fmt("%$format") !! $empty }

        %format{$name} = @info;
    }
}

multi sub report(
  @s,
  :@columns       is copy,
  :$header-repeat is copy,
  :$legend        is copy,
  :$csv           is copy,
  :@format,
  --> Str:D
) {

    # set up basic header
    my @text = qq:to/HEADER/.chomp;
Telemetry Report of Process #$*PID ({Instant.from-posix(time).DateTime})
Number of Snapshots: {+@s}
HEADER

    # return that if there's nothing to tell otherwise
    return @text[0] unless @s;

    # get the sampler that was used
    my $sampler = @s[0].sampler;

    # determine columns to be displayed
    unless @columns {
        if %*ENV<RAKUDO_REPORT_COLUMNS> -> $rrc {
            @columns = $rrc.comb( /<[\w%-]>+/ );
        }
        else {
            @columns.append(.default-columns) for $sampler.instruments;
        }
    }

    # set header repeat flag
    without $header-repeat {
        $header-repeat = $_.Int with %*ENV<RAKUDO_REPORT_HEADER_REPEAT> // 32;
    }

    # set legend flag
    without $legend {
        $legend = $_.Int with %*ENV<RAKUDO_REPORT_LEGEND> // 1;
    }

    # set csv flag
    without $csv {
        $csv = $_.Int with %*ENV<RAKUDO_REPORT_CSV> // 0;
    }

    # get / calculate the format info we need
    my %format;
    if @format {
        prepare-format(@format, %format)
    }
    else {
        prepare-format(.formats, %format) for $sampler.instruments;
    }

    # some initializations
    my @periods = periods(@s);

    # only want CSV ready output
    if $csv {
        my @formats = %format{@columns};
        @text.push: @formats.map(*[NAME]).join(' ');
        for @periods -> $period {
            @text.push: @formats.map( -> @info { $period{@info[NAME]} }).join(' ');
        }
    }

    # standard text output
    else {
        my $first = @s[0];
        my $last  = @s[*-1];
        my $total = $last - $first;

        # remove the columns that don't have any values
        @columns = @columns.grep: -> $column {
            (%format{$column}
              or note "WARNING: Unknown Telemetry column `$column`."
                ~ " Perhaps you need to adjust used instruments"
                ~ " (see RAKUDO_TELEMETRY_INSTRUMENTS)\n"
              and 0
            ) and @periods.first: { .{%format{$column}[NAME]} }
        };
        my @formats = %format{@columns};
        my $header  = "\n" ~ @formats.map(*[HEADER]).join(' ');

        for $sampler.instruments -> $instrument {
            @text.push($_) with $instrument.preamble($first, $last, $total, @s);
        }

        my sub push-period($period --> Nil) {
            with $period.message -> $message {
                my $line = "#-- $message ";
                @text.push: $line ~ "-" x 80 - $line.chars;
            }
            @text.push: @formats.map( -> @info {
                @info[DISPLAY]($period{@info[NAME]})
            }).join(' ').trim-trailing;
        }

        @text.push($header) unless $header-repeat;

        for @periods.kv -> $index, $period {
            @text.push($header) if $header-repeat && $index %% $header-repeat;
            push-period($period)
        }

        @text.push: @formats.map(*[FOOTER]).join(' ');

        push-period($total);

        if $legend {
            @text.push: '';
            @text.push: 'Legend:';
            for @formats -> $col {
                @text.push: "$col[NAME].fmt("%9s")  $col[LEGEND]";
            }
        }
    }

    @text.join("\n")
}

# Allow for safe CTRL-c exit, always giving a report ---------------------------
my $has-safe-ctrl-c = 0;
sub safe-ctrl-c(--> Nil) is export {
    unless $has-safe-ctrl-c {
        signal(SIGINT).tap: &exit;
        $has-safe-ctrl-c = 1;
    }
}

# The special T<foo bar> functionality -----------------------------------------
sub T (--> Telemetry:D) is export { Telemetry.new }

# Make sure we tell the world if we're implicitely told to do so ---------------
END {
    $snapper-running = 0;  # stop any snapper
    if @snaps.elems {
        snap;
        note report;
    }
}

# vim: expandtab shiftwidth=4
