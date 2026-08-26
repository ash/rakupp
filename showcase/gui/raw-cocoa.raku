# A native macOS app in bare objc_msgSend — what the Wings module wraps.
# Window, button, live counter; arm64 only (NSRect passed as an HFA).
# Run:  RAKUPP_MAIN_THREAD=1 rakupp raw-cocoa.raku          (self-driving, ~4s)
#       RAKUPP_MAIN_THREAD=1 rakupp raw-cocoa.raku --stay   (click it yourself)
use NativeCall;

my constant OBJC = '/usr/lib/libobjc.A.dylib';

sub objc_getClass(Str --> Pointer)                        is native(OBJC) { * }
sub sel_registerName(Str --> Pointer)                     is native(OBJC) { * }
sub objc_allocateClassPair(Pointer, Str, uint64 --> Pointer) is native(OBJC) { * }
sub objc_registerClassPair(Pointer)                       is native(OBJC) { * }
sub class_addMethod(Pointer, Pointer, Pointer, Str --> int8) is native(OBJC) { * }

sub msg-p(Pointer, Pointer --> Pointer)                is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-p-p(Pointer, Pointer, Pointer --> Pointer)     is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-p-str(Pointer, Pointer, Str --> Pointer)       is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-p-i(Pointer, Pointer, int64 --> Pointer)       is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-p-b(Pointer, Pointer, int8 --> Pointer)        is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-p-d(Pointer, Pointer, num64 --> Pointer)       is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-b(Pointer, Pointer --> int8)                   is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-i(Pointer, Pointer --> int64)                  is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-ppp(Pointer, Pointer, Pointer, Pointer, Pointer --> Pointer)
                                                       is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-init-rect(Pointer, Pointer, num64, num64, num64, num64, uint64, uint64, int8 --> Pointer)
                                                       is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-set-rect(Pointer, Pointer, num64, num64, num64, num64, int8 --> Pointer)
                                                       is native(OBJC) is symbol('objc_msgSend') { * }
sub msg-next-event(Pointer, Pointer, uint64, Pointer, Pointer, int8 --> Pointer)
                                                       is native(OBJC) is symbol('objc_msgSend') { * }

sub NSBeep() is native('/System/Library/Frameworks/AppKit.framework/AppKit') { * }
NSBeep();

sub cls(Str $n)    { objc_getClass($n) }
sub sel(Str $n)    { sel_registerName($n) }
sub ns-str(Str $s) { msg-p-str(cls('NSString'), sel('stringWithUTF8String:'), $s) }

my $app = msg-p(cls('NSApplication'), sel('sharedApplication'));
msg-p-i($app, sel('setActivationPolicy:'), 0);

my $win = msg-init-rect(msg-p(cls('NSWindow'), sel('alloc')),
                        sel('initWithContentRect:styleMask:backing:defer:'),
                        0e0, 0e0, 480e0, 220e0, 15, 2, 0);
msg-p-p($win, sel('setTitle:'), ns-str('Raku++ ❤️ Cocoa'));
msg-p($win, sel('center'));
my $view = msg-p($win, sel('contentView'));

my $label = msg-p-p(cls('NSTextField'), sel('labelWithString:'), ns-str('clicked 0 times'));
msg-p-p($label, sel('setFont:'), msg-p-d(cls('NSFont'), sel('systemFontOfSize:'), 28e0));
msg-set-rect($label, sel('setFrame:'), 120e0, 120e0, 300e0, 40e0, 0);
msg-p-p($view, sel('addSubview:'), $label);

# --- the click handler: a Raku closure, invoked by AppKit ---
my $count = 0;
sub on-click(Pointer $self, Pointer $cmd, Pointer $sender) {
    $count++;
    msg-p-p($label, sel('setStringValue:'), ns-str("clicked $count time{$count == 1 ?? '' !! 's'}"));
    say "Raku says: click #$count";
}

# Mint an Objective-C class at runtime whose action method IS the Raku sub.
my $target-cls = objc_allocateClassPair(cls('NSObject'), 'RakuTarget', 0);
class_addMethod($target-cls, sel('clicked:'), &on-click, 'v@:@');
objc_registerClassPair($target-cls);
my $target = msg-p(msg-p($target-cls, sel('alloc')), sel('init'));

my $button = msg-ppp(cls('NSButton'), sel('buttonWithTitle:target:action:'),
                    ns-str('Click me'), $target, sel('clicked:'));
msg-set-rect($button, sel('setFrame:'), 180e0, 50e0, 120e0, 40e0, 0);
msg-p-p($view, sel('addSubview:'), $button);

msg-p-p($win, sel('makeKeyAndOrderFront:'), Pointer);
msg-p-b($app, sel('activateIgnoringOtherApps:'), 1);
say 'window up, number ', msg-i($win, sel('windowNumber')),
    ', visible: ', msg-b($win, sel('isVisible')) ?? 'YES' !! 'no';

# --- event loop, pumped from Raku ---
my $stay     = ?@*ARGS.grep('--stay');
my $mode     = ns-str('kCFRunLoopDefaultMode');
my $deadline = now + 4;
my @auto     = $stay ?? () !! (now + 1, now + 2, now + 3);
while msg-b($win, sel('isVisible')) && ($stay || now < $deadline) {
    my $until = msg-p-d(cls('NSDate'), sel('dateWithTimeIntervalSinceNow:'), 0.05e0);
    my $ev = msg-next-event($app, sel('nextEventMatchingMask:untilDate:inMode:dequeue:'),
                            0xFFFFFFFFFFFFFFFF, $until, $mode, 1);
    msg-p-p($app, sel('sendEvent:'), $ev) if $ev;
    if @auto && now > @auto[0] {
        @auto.shift;
        msg-p-p($button, sel('performClick:'), Pointer);   # synthesized click
    }
}
say "final count: $count";
