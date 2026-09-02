# Regression: five NativeCall defects, all found making OpenSSL's suite pass.
#
# 1. A Blob argument was COPIED into a per-call temporary and the callee got a
#    pointer to it, so the pointer dangled the moment the call returned. Most C
#    functions read during the call and never noticed; the ones that RETAIN the
#    pointer got freed memory. BIO_new_mem_buf is the canonical retainer — it
#    deliberately does not copy — so OpenSSL's PEM decoder read garbage, said
#    `DECODER routines::unsupported`, and RSAKey.new stored the resulting null
#    (its guard is `unless defined($rsa)`, and `defined(0)` is True) until
#    RSA_size segfaulted on it. putenv is the same contract in libc.
#
# 2. `is symbol(EXPR)` only understood a literal. OpenSSL::Stack computes the
#    name — `sk_num` before OpenSSL 1.1, `OPENSSL_sk_num` from 1.1 on — by
#    asking the library its version, and every such sub resolved to its own
#    Raku name instead. It has to be evaluated at FIRST CALL, not with the
#    declaration: at declaration time the library is not loadable yet, the
#    version probe answers 0, and the helper caches that 0 in a `state`.
#
# 3. `.does` walked the class ancestry, so it was False for every buffer role —
#    `$buf.does(Blob)`, `.does(buf8)`. `~~` already knew. That is what failed
#    `isa-ok md5(Blob.new), buf8`, because Test's isa-ok is `.isa || .does` and
#    .isa is False for a role on both engines.
#
# 4. A native function returning NULL where a repr('CStruct') class is declared
#    was boxed as an INSTANCE holding address 0 — defined, and true. NULL is the
#    type object. `unless my $s = SSL_load_client_CA_file(…)` never fired.
#
# 5. Nil for a `Str` parameter was marshalled as "" — a one-byte buffer —
#    where Nil binds as the type object and the type object is NULL.
#    `ERR_error_string($e, Nil)` asks OpenSSL for its static buffer with that
#    NULL; given "" instead, OpenSSL wrote a 256-byte message into one byte of
#    heap, and the corruption surfaced later as a crash in whatever libcrypto
#    allocated next — OpenSSL's 10-client-ca-file test, two runs in three.
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;
sub ok($desc, $got, $want = True) { @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# ---- 1. a retained buffer outlives the call --------------------------------
# putenv KEEPS the pointer it is given; getenv reads it much later. The name is
# long on purpose: short buffers are held inline, and it is the shared body of a
# promoted one that gives the pointer the Raku object's lifetime.
sub putenv(Blob --> int32) is native { * }
sub getenv(Str --> Str)   is native { * }
my $var = 'RAKUPP_NATIVE_BUFFER_LIFETIME_REGRESSION_VARIABLE';
my $val = 'the-value-that-must-survive-the-call';
# The caller keeps the buffer alive, which is the whole contract a retaining API
# asks for — OpenSSL::RSATools says so in as many words about the PEM it hands
# BIO_new_mem_buf. What was broken is the other half: the pointer the callee got
# was never the buffer's own storage.
my $entry = "$var=$val\0".encode;
putenv($entry);
# allocate and churn in between, so a freed temporary is likely to be reused
my @churn = (^200).map({ ('x' x 200).encode });
ok('putenv kept our bytes', getenv($var), $val);

# ---- 2. a computed `is symbol(…)` -------------------------------------------
my sub pick(Str $stem --> Str) { 'str' ~ $stem }
sub my-length(Str --> int64) is native is symbol(pick('len')) { * }
ok('computed symbol resolves', my-length('hello'), 5);
# the literal form still works, and so does no-symbol-at-all
sub strlen(Str --> int64) is native { * }
ok('the sub name is the default', strlen('worlds'), 6);

# ---- 3. a buffer does the buffer roles --------------------------------------
my $buf = buf8.allocate(4);
ok('buf8 does buf8',   $buf.does(buf8));
ok('buf8 does Blob',   $buf.does(Blob));
ok('utf8 does Blob',   'abc'.encode.does(Blob));
ok('a buffer is not Cool', $buf.does(Cool), False);
ok('.does agrees with ~~', $buf.does(buf8), so $buf ~~ buf8);

# ---- 4. NULL is the type object ---------------------------------------------
# getenv returns NULL for a name that is not set. Declared as a CStruct class,
# that must come back undefined — the caller's whole test for "no result".
class Handle is repr('CStruct') { has int32 $.x }
sub getenv-struct(Str --> Handle) is native is symbol('getenv') { * }
my $missing = getenv-struct('RAKUPP_THIS_NAME_IS_NOT_SET_ANYWHERE_1234567');
ok('NULL is undefined', $missing.defined, False);
ok('NULL is false',     so $missing, False);
ok('NULL keeps its type', $missing.^name, 'Handle');
# a non-NULL return is still a live instance
my $live = getenv-struct($var);
ok('non-NULL is defined', $live.defined);

# ---- 5. Nil for a Str parameter is NULL -------------------------------------
# getcwd(NULL, 0) allocates and answers the directory (glibc, musl and the
# BSDs all do); getcwd(buf, 0) with a non-NULL buf is EINVAL and answers NULL.
# So the call below succeeds only if Nil went across as NULL — with the old
# one-byte "" it came back undefined.
sub getcwd(Str, size_t --> Str) is native { * }
my $cwd = getcwd(Nil, 0);
ok('Nil to a Str parameter is NULL', $cwd.defined);
ok('…and the callee answered through it', $cwd.starts-with('/') || $cwd.contains(':'));

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
