# Expanded code review

Reviewed on 2026-11-14, primarily in `src/`.

Source revision: `be6aca55cc2a4b497f2173a3367fd2c0070a51c0`.

Executable used: `build/rakupp`, reporting `Raku++ 5.8.0-120-gbe6aca55 (2026-11-14) arm64-darwin`. JavaScript comparisons used Node.js `v24.22.1`. No source changes or new test files were made. This report is the sole file created for the review.

## Verdict

**Changes required before a clean sign-off.** The implementation has substantial regression coverage, explicit ownership contracts, and useful differential-testing infrastructure. The arithmetic, JSON, digest, compression, and sandbox checks selected for this review passed. However, the review found memory-lifetime defects in native code, an unchecked Jupyter frame-length overflow, and reproducible JavaScript backend and protocol discrepancies.

There are **17 findings: four P1 and thirteen P2**. Eleven have runtime evidence, including one simulated short-write test. Six are static findings. The P1 findings are source-based: this review did not reproduce them under a sanitizer or exercise the Jupyter issue over a socket. Their failure sequences and validation gaps are stated explicitly below. Finding 17 is a helper-level defect whose current application-level caller was not established.

P1 means a high-priority correctness or memory-safety issue. P2 means a defect that should be fixed in normal development. These priorities are review judgments, not vulnerability scores.

## Findings at a glance

| ID | Priority | Finding | Evidence |
| --- | --- | --- | --- |
| 01 | P1 | Cycle notification accesses a payload after releasing its ownership | Static interleaving analysis |
| 02 | P1 | Interpreter destruction can leave workers using the destroyed interpreter | Static lifetime analysis |
| 03 | P1 | Jupyter frame length can wrap and prevent input consumption | Static analysis; overflow arithmetic checked |
| 04 | P1 | Native array element handles become dangling after growth | Static public-API sequence |
| 05 | P2 | JavaScript async supply/react context disappears after `await` | Reproduced |
| 06 | P2 | JavaScript `.list` subscribes to an asynchronous supply twice | Reproduced |
| 07 | P2 | JavaScript handle `.read(n)` consumes all remaining input | Reproduced |
| 08 | P2 | JavaScript `.getc` returns UTF-16 code units | Reproduced |
| 09 | P2 | JavaScript paths change their target after `chdir` | Reproduced |
| 10 | P2 | JavaScript child-process signal termination loses its status | Reproduced |
| 11 | P2 | JavaScript reads continue after a handle is closed | Reproduced |
| 12 | P2 | JavaScript stdout drops the unwritten suffix after a short write | Reproduced with host fault injection |
| 13 | P2 | Protocol JSON accepts malformed numeric tokens | Reproduced through MCP |
| 14 | P2 | Protocol JSON emits invalid UTF-8 for lone surrogates | Reproduced through MCP |
| 15 | P2 | LSP accepts incomplete JSON and trailing garbage | Reproduced |
| 16 | P2 | SlabPool leaks blocks on threads that only deallocate | Static lifecycle analysis |
| 17 | P2 | Reference-wrapper move assignment is unsafe when its source is owned by its destination | Static helper analysis; caller not established |

### 01 — P1: Retain ownership while notifying the cycle collector

Locations: [Ref.h:288](/Users/ash/rakupp/src/Ref.h:288), [Ref.h:228](/Users/ash/rakupp/src/Ref.h:228), [Cycle.cpp:725](/Users/ash/rakupp/src/Cycle.cpp:725), [Cycle.cpp:110](/Users/ash/rakupp/src/Cycle.cpp:110). The same decrement sequence appears in `PRef::release` at [Ref.h:331](/Users/ash/rakupp/src/Ref.h:331).

`PayRef::release` decrements the reference count and then, for a tracked object that was not the last reference, calls `cycleNoteRelease(p)`. That call reads the object and its flags and may put its raw pointer in the candidate buffer. It holds no reference of its own.

An unsafe interleaving is:

1. A tracked payload has two owning references on different threads.
2. Thread A decrements the count from two to one and pauses before its notification.
3. Thread B decrements from one to zero and destroys the payload. It can observe no buffered flag because A has not added the candidate yet.
4. A resumes and reads or buffers the destroyed payload.

The candidate-buffer lock cannot prevent this: it is acquired after the decrement, and the first flag read in `bufferAdd` is outside it. `cycleQuiet()` only controls the optional classification work; the notification still calls `bufferAdd` when workers are live. Atomic reference counts alone do not preserve lifetime after the releasing thread has relinquished its count.

**Impact:** use-after-free, and potentially a stale candidate pointing at a recycled allocation. This is a source-level concurrency finding; no failing Raku program or sanitizer trace was obtained here.

**Fix direction:** keep the releasing reference alive throughout notification, with a protocol that also coordinates final release and candidate removal. Do not attempt to repair this by incrementing an already-released pointer. Add a deterministic two-thread test that pauses between decrement and notification.

### 02 — P1: Do not destroy an interpreter while its workers can still use it

Locations: [EmbedApi.cpp:295](/Users/ash/rakupp/src/EmbedApi.cpp:295), [Interpreter.cpp:4748](/Users/ash/rakupp/src/Interpreter.cpp:4748), [Interpreter.cpp:7186](/Users/ash/rakupp/src/Interpreter.cpp:7186), [Interpreter.cpp:7149](/Users/ash/rakupp/src/Interpreter.cpp:7149).

`rk_free` deletes the interpreter. Its destructor calls `endPool`, which waits up to two seconds for pooled workers and then detaches workers that are still running. Other worker slots are not joined by that function. Detachment allows the thread to continue; it does not keep the interpreter alive.

The worker closures capture a raw `Interpreter*`. After a pooled job returns, `poolWorker` accesses `runningBodies_`, `poolM_`, `poolStopping_`, and other interpreter members. Promise workers also call through their captured `self` when completing. A job blocked beyond the timeout can therefore resume into freed memory after `rk_free` returns.

**Impact:** embedded hosts can suffer a use-after-free when freeing a session with unfinished work. Process-exit daemon semantics do not provide a lifetime guarantee for a host that continues running. Static finding; the embedding lifecycle was not run against a newly built shared library.

**Fix direction:** join/cancel all workers before releasing their state, or move the state they require into ownership that outlives the workers. Define and test `rk_free` with a long-running job, a blocked job, and both pooled and non-pooled workers.

### 03 — P1: Validate Jupyter frame lengths before adding the header size

Location: [JupyterKernel.cpp:500](/Users/ash/rakupp/src/JupyterKernel.cpp:500), especially the bounds check at [line 507](/Users/ash/rakupp/src/JupyterKernel.cpp:507).

The ZMTP parser reads an eight-byte length into `size_t`, then checks `p->in.size() < off + size`. The addition can wrap. On a 64-bit target, a long-frame header with `off = 9` and length `18446744073709551607` makes `off + size` equal zero.

After a valid greeting, the nine frame-header bytes `02 ff ff ff ff ff ff ff f7` are sufficient to reach that case. With no payload, `substr(9, size)` produces an empty body and `erase(0, 0)` consumes nothing. The parser's loop repeatedly processes the same header, queuing messages without making progress. This occurs before Jupyter message-signature verification. More generally, the receive buffer and multipart accumulation also lack an explicit message-size budget.

**Impact:** a peer that can reach a kernel socket can cause a parser loop and potentially memory exhaustion without presenting a valid signed Jupyter request. The integer wrap was checked, but no live socket exploit was executed.

**Fix direction:** enforce a maximum frame/message size and compare by subtraction after verifying the header is present: reject an excessive length, then check `size > available - off`. Budget multipart accumulation as well. Add boundary tests for `SIZE_MAX`, `SIZE_MAX - 8`, partial headers, and oversized multipart messages.

### 04 — P1: Native element handles do not satisfy the documented lifetime

Locations: [ExtApi.cpp:162](/Users/ash/rakupp/src/ExtApi.cpp:162), [ExtApi.cpp:77](/Users/ash/rakupp/src/ExtApi.cpp:77), and the lifetime contract in [rakupp_ext.h:23](/Users/ash/rakupp/include/rakupp/rakupp_ext.h:23).

`rk_at_pos` returns a pointer directly into a `ValueList`. `rk_push` can grow and relocate that list. A handle saved before growth therefore becomes dangling within the same extension call or embedded evaluation, although the public contract describes handles as valid for the call/evaluation lifetime.

The problematic public-API sequence is:

```c
RkValue a = rk_array(c);
rk_push(c, a, rk_int(c, 42));
RkValue first = rk_at_pos(c, a, 0);
/* Append enough values to make a's storage grow. */
rk_push(c, a, rk_int(c, 7)); /* repeated as necessary */
/* first now refers to the previous storage. */
rk_int_get(c, first);
```

**Impact:** stale reads or use-after-free in native extensions and bindings performing otherwise ordinary container operations. This is a static finding, not a C-host runtime reproduction. Related borrowed handles such as `rk_val_at` should also be audited for mutation through callbacks; no separate hash failure is claimed here.

**Fix direction:** return an arena-owned value handle or a stable indirection, or explicitly redesign and document borrowed-element lifetimes. Test a saved element handle across array growth and across a callback that mutates its container.

### 05 — P2: Async supply/react context is removed before the continuation runs

Locations: [87-supply.js:44](/Users/ash/rakupp/src/js-rt/87-supply.js:44) and [87-supply.js:94](/Users/ash/rakupp/src/js-rt/87-supply.js:94).

`withCtx` pushes a context, calls the body, and pops the context in `finally`. For an async function, that `finally` runs when the function returns its Promise, before the code following `await` executes. The callback path in `whenever` has the same problem.

Reproduction:

```raku
react {
    await Promise.in(0.01);
    whenever Supply.from-list([42]) { say $_ }
}
```

The interpreter prints `42` and exits zero. The JavaScript output exits one with `whenever outside the lexical scope of a react/supply block`.

**Fix direction:** carry the context explicitly into async continuations or implement async-aware context propagation. Merely keeping one global stack entry pushed until a Promise settles is insufficient when multiple async bodies overlap. Cover `emit`, `done`, and `whenever` both before and after an await.

### 06 — P2: Listing an asynchronous supply starts its producer twice

Location: [87-supply.js:158](/Users/ash/rakupp/src/js-rt/87-supply.js:158), particularly the second `s.tap` at [line 164](/Users/ash/rakupp/src/js-rt/87-supply.js:164).

`supplyList` creates its collecting tap and, when the supply has not finished synchronously, creates a second tap with empty callbacks. A tap on an on-demand supply runs its producer, so the second subscription is not a no-op.

Reproduction:

```raku
my $n = 0;
my $s = supply {
    $n++;
    whenever Promise.in(0.01) { emit 42 }
};
my @x = $s.list;
say $n;
say @x;
```

The interpreter prints `1` and `[42]`. JavaScript prints `2` and `[[object Object]]`. The producer count directly establishes duplicate execution. The second output also shows a pending runtime object escaping into the array; it needs a separate check of the async lowering/materialization path.

**Impact:** duplicate producer side effects and incorrect list results.

**Fix direction:** complete the existing collecting subscription instead of opening another. Test both the number of producer executions and the final collected values for asynchronous supplies.

### 07 — P2: JavaScript `.read(n)` ignores the requested byte count

Location: [60-methods.js:133](/Users/ash/rakupp/src/js-rt/60-methods.js:133), the `read` entry.

The implementation accepts `n` but calls `strEncode(host.handleSlurp(s))`, consuming the entire remainder. It also routes a binary read through text rather than a byte-oriented cursor.

With standard input `abcd`:

```raku
say $*IN.read(2).list;
say $*IN.read(2).list;
```

The interpreter prints `(97 98)` then `(99 100)`. JavaScript prints `(97 98 99 100)` then `()`.

**Fix direction:** maintain a byte-oriented read position, consume at most the requested count, and preserve non-text bytes. Cover zero-length reads, partial reads, EOF, and binary input.

### 08 — P2: JavaScript `.getc` splits Unicode characters

Location: [70-host.js:31](/Users/ash/rakupp/src/js-rt/70-host.js:31).

`handleGetc` indexes a JavaScript string with `t[pos++]` or `h.buf[h.pos++]`. This returns one UTF-16 code unit rather than one Raku character. It splits astral characters and does not implement grapheme boundaries.

With standard input consisting of one `😀`:

```raku
say $*IN.getc.ord;
say $*IN.getc.defined;
```

The interpreter prints `128512` and `False`. JavaScript prints `55357` and `True`: the high surrogate was returned first and the low surrogate remains.

**Fix direction:** consume one grapheme using the runtime's Unicode segmentation machinery, keeping the appropriate underlying position. Include both astral characters and combining sequences in tests.

### 09 — P2: JavaScript IO paths resolve against the wrong working directory

Locations: [60-methods.js:132](/Users/ash/rakupp/src/js-rt/60-methods.js:132), [40-objects.js:425](/Users/ash/rakupp/src/js-rt/40-objects.js:425), and [60-methods.js:276](/Users/ash/rakupp/src/js-rt/60-methods.js:276).

The runtime has a `cwd` field on `RIOPath`, but path methods pass `s.path` alone to host operations. Ordinary construction can also leave the base undefined, with `.CWD` then reading the current host directory. Changing directories changes what an already-created relative path designates.

Run from the repository root:

```raku
my $p = "README.md".IO;
chdir "docs";
say $p.absolute;
```

The interpreter prints `/Users/ash/rakupp/README.md`. JavaScript prints `/Users/ash/rakupp/docs/README.md`.

**Impact:** subsequent operations can read or write a different file from the path originally captured. Only the read-only `.absolute` case was executed here.

**Fix direction:** capture the base directory when a path is created, resolve operations against it, and preserve it through derived paths such as `.parent` and `.add`.

### 10 — P2: JavaScript `Proc` loses signal termination information

Locations: [70-host.js:95](/Users/ash/rakupp/src/js-rt/70-host.js:95), [70-host.js:171](/Users/ash/rakupp/src/js-rt/70-host.js:171), and [70-host.js:177](/Users/ash/rakupp/src/js-rt/70-host.js:177).

`host.run` passes only `spawnSync`'s `status` to `mkProc`. A signal-terminated child has a null status; this becomes exit code one. `.signal` is hardcoded to zero.

Reproduction, terminating only the spawned shell itself:

```raku
my $p = run "/bin/sh", "-c", q[kill -TERM $$];
say $p.exitcode;
say $p.signal;
```

The interpreter prints `0` and `15`. JavaScript prints `1` and `0`.

**Fix direction:** retain both normal exit status and signal information from the host result, map the signal to the runtime representation, and make `.Bool` and unsuccessful-process diagnostics use the same state.

### 11 — P2: Closed JavaScript handles still answer reads

Locations: [70-host.js:28](/Users/ash/rakupp/src/js-rt/70-host.js:28), [70-host.js:92](/Users/ash/rakupp/src/js-rt/70-host.js:92), and [60-methods.js:133](/Users/ash/rakupp/src/js-rt/60-methods.js:133).

Closing a handle sets `h.closed`, but the read helpers do not check it and continue reading the already-buffered content.

Reproduction using an existing repository file:

```raku
my $h = open "README.md";
$h.close;
say (try $h.get) // $!.^name;
```

The interpreter prints `X::IO::Closed`. JavaScript prints the first line of the file, `# Raku++`.

**Fix direction:** enforce handle state consistently before reads and writes. Test `.get`, `.read`, `.slurp`, and writes after close rather than only the `.opened` predicate.

### 12 — P2: JavaScript stdout discards data after a short write

Location: [70-host.js:51](/Users/ash/rakupp/src/js-rt/70-host.js:51). The stderr adapter immediately below uses the same single-call approach.

`host.writeOut` ignores the byte count returned by `fs.writeSync`. `host.flush` then clears the whole output buffer. When the host accepts only a prefix, the unwritten suffix is lost. Its `EAGAIN` path also recursively retries, risking stack growth under sustained backpressure.

**Validation:** the Node host's `fs.writeSync` was wrapped to perform a real write of only the first two characters and return that write count. Compiling and running `say "abcdefgh"` then produced only `ab`, with exit status zero. This is a simulated short-write condition, not a claim that a normal terminal produced one during the review.

**Fix direction:** write a byte buffer with an explicit offset until all bytes are accepted, with bounded/non-recursive retry behavior. Cover partial writes, multibyte UTF-8, and transient `EAGAIN`.

### 13 — P2: MCP/Jupyter JSON number parsing accepts invalid tokens

Location: [JsonLite.h:275](/Users/ash/rakupp/src/JsonLite.h:275), especially [line 291](/Users/ash/rakupp/src/JsonLite.h:291).

The scanner permits decimal points, exponent markers, and signs in arbitrary positions. The floating-point path calls `strtod` without checking where conversion stopped, so a malformed token can be partially converted and accepted as a different value.

MCP input:

```json
{"jsonrpc":"2.0","id":1e+,"method":"ping"}
```

Observed response:

```json
{"jsonrpc":"2.0","id":1,"result":{}}
```

The invalid request was executed and its ID changed instead of receiving a parse error. The shared parser is also used by Jupyter; only the MCP route was exercised.

**Fix direction:** enforce JSON's numeric grammar before conversion, require full-token conversion, and reject non-finite results where they cannot be represented as JSON numbers.

### 14 — P2: Lone surrogate escapes produce invalid UTF-8 on the wire

Location: [JsonLite.h:205](/Users/ash/rakupp/src/JsonLite.h:205).

The parser combines a valid surrogate pair, but otherwise passes an unmatched surrogate directly to `utf8()`. The writer emits those bytes unchanged.

MCP input:

```json
{"jsonrpc":"2.0","id":"\ud800","method":"ping"}
```

The response contains bytes `ED A0 80` in the ID. A strict UTF-8 decode of the response fails. The request itself was sent as ASCII with a JSON escape, so this was not invalid UTF-8 supplied by the test harness.

**Fix direction:** define a consistent policy for unpaired surrogates, such as rejection, and never encode surrogate code points as UTF-8. Validate low-surrogate ranges when combining pairs. The separate LSP parser should be included in this audit because it has its own surrogate handling.

### 15 — P2: The LSP parser treats incomplete documents as valid requests

Locations: [Lsp.cpp:161](/Users/ash/rakupp/src/Lsp.cpp:161), [Lsp.cpp:251](/Users/ash/rakupp/src/Lsp.cpp:251), [Lsp.cpp:267](/Users/ash/rakupp/src/Lsp.cpp:267), and [Lsp.cpp:431](/Users/ash/rakupp/src/Lsp.cpp:431).

`JsonParser::parse` does not require complete input consumption. Object and array parsers return constructed values when their closing delimiters are absent. Parsing failures are frequently converted into plausible values rather than a failed document.

Both of these bodies, sent with correct `Content-Length` framing, received successful responses:

```text
{"jsonrpc":"2.0","id":77,"method":"initialize","params":{}
{"jsonrpc":"2.0","id":78,"method":"shutdown"}trailing-junk
```

The first is missing the outer closing brace; the second has additional non-whitespace content. The server returned `result` responses for IDs 77 and 78.

**Fix direction:** propagate parse failure, require each delimiter, and require only whitespace after the root value. Consolidating protocol parsers could reduce duplicated defects, but the shared parser must first be corrected as described in findings 13 and 14.

### 16 — P2: Deallocation-only threads never register SlabPool cleanup

Location: [SlabPool.h:97](/Users/ash/rakupp/src/SlabPool.h:97).

The reaper is registered by `alloc`, but `dealloc` can populate a thread's free lists without registering it. A thread that only destroys objects allocated on another thread can terminate with retained blocks that will never be returned to the allocator. This is explicitly a supported cross-thread ownership pattern in the pool's design comments.

The per-thread retention limit bounds one thread's loss, but repeated short-lived deallocation-only threads accumulate the loss. `BodyPool::dealloc` already has an arming step; `SlabPool::dealloc` does not.

**Fix direction:** register cleanup before retaining the first block on any thread. Also revisit the claim at [SlabPool.h:137](/Users/ash/rakupp/src/SlabPool.h:137) that clearing `armed` permits re-registration: revisiting a previously destroyed function-local `thread_local Reaper` does not construct it again. Retention during late thread-local destruction needs an explicit policy.

**Validation gap:** no allocator instrumentation or leak-sanitizer run was performed. This is a static lifecycle finding.

### 17 — P2: Move assignment destroys an owned source before reading it

Locations: [Ref.h:70](/Users/ash/rakupp/src/Ref.h:70), [Ref.h:138](/Users/ash/rakupp/src/Ref.h:138), [Ref.h:304](/Users/ash/rakupp/src/Ref.h:304), and [Ref.h:352](/Users/ash/rakupp/src/Ref.h:352).

The move assignments call `release()` before extracting and clearing the source pointer/handle. If the source wrapper lives inside the destination's solely owned object, releasing the destination destroys the source wrapper first.

The minimal ownership shape is a node containing another reference of the same kind:

```cpp
node = std::move(node->next);
```

With sole ownership of the parent, this leaves the assignment accessing the destroyed `next` member. The copy-assignment implementation already comments on and handles this aliasing case, but the move implementation does not.

**Fix direction:** move the source ownership into a temporary and clear the source before releasing the destination. Test this shape for each wrapper, including a null child and a child with no other owners.

**Scope qualification:** the helper defect is clear from the implementation, but this review did not establish an existing Raku-level path that invokes this exact assignment shape. It should not be represented as a reproduced application crash.

## Validation performed

All Raku commands ran with `RAKUPP_NO_PRECOMP=1`. Python used `-B`. Programs generated for differential checks and compiler output were kept in memory. No source rebuild, JIT compilation, native executable generation, dependency installation, or full test-suite run was performed.

### Passing checks

| Check | Result |
| --- | --- |
| `t/regression/int-kernel-exactness.raku` | 53 checks passed; exit 0 |
| `t/regression/loop-kernel-exactness.raku` | 151 checks passed; exit 0 |
| `t/regression/loop-kernel-containers.raku` | 28 checks passed; exit 0 |
| `t/regression/kernel-gcd-lcm.raku` | 9 checks passed; exit 0 |
| `t/regression/json-codec-number-typing-and-nfc.raku` | 48 checks passed; exit 0 |
| Python integer oracle | 312 expressions matched; arithmetic, floor division, remainder, and bitwise operations around signed 64-bit boundaries and larger values up to 1,024 bits |
| Python digest/checksum oracle | 40 results matched across MD5, SHA-1, SHA-224/256/384/512, CRC-32, and Adler-32 |
| Python-compressed input inflated by Raku++ | 15 results matched across zlib, gzip, and raw framing |
| Raku++-compressed output inflated by Python | 15 results matched across the same three formats |
| Language sandbox probes | Five filesystem/process operations were refused with `X::SecurityPolicy::Sandbox`; ordinary arithmetic remained allowed |

The data corpus included empty input, `abc`, all byte values, repeated zero bytes, and deterministic random bytes. The integer and random-byte corpus used seed `20261114`. These are useful spot checks, not exhaustive algorithm verification or cryptographic certification.

The sandbox probes covered `slurp` by string path, path-method `slurp`, an unopened `IO::Handle` passed to `slurp`, `run`, and path existence. They do not establish the completeness of the language sandbox or the Linux/macOS OS sandbox.

### JavaScript comparisons

Each listed Raku program was run with the interpreter and compiled using `--target=js --standalone -e`, with output captured directly from stdout and evaluated by Node. The relevant emitted runtime lines were checked against `70-host.js`, `87-supply.js`, and `60-methods.js`; they matched the source locations discussed above.

For the two standard-input cases, the bootstrap read the generated code and test input from an in-memory JSON envelope, then replaced Node's `fs.readFileSync(0, ...)` with the supplied test string. That isolates the runtime's own cursor/character behavior without creating program files. For finding 12, only the host write behavior was replaced to simulate a short write; the generated runtime was unchanged.

A compact reproduction pattern for cases that do not need standard input is:

```python
import os, subprocess

env = dict(os.environ, RAKUPP_NO_PRECOMP="1")
node = "/Users/ash/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node"
source = 'my $p = "README.md".IO; chdir "docs"; say $p.absolute;'

reference = subprocess.run(
    ["build/rakupp", "-e", source], env=env,
    capture_output=True, timeout=10)
generated = subprocess.run(
    ["build/rakupp", "--target=js", "--standalone", "-e", source],
    env=env, capture_output=True, timeout=10, check=True)
actual = subprocess.run(
    [node, "-e", "eval(require('fs').readFileSync(0,'utf8'))"],
    input=generated.stdout, capture_output=True, timeout=10)
print(reference.returncode, reference.stdout, reference.stderr)
print(actual.returncode, actual.stdout, actual.stderr)
```

For finding 12, the injected wrapper retained the original `fs.writeSync`, wrote only `s.slice(0, 2)` for string writes to stdout, and returned the actual short count. The observed output was `ab` for `say "abcdefgh"`.

## Review coverage and limitations

This was a substantially expanded, targeted review, not a line-by-line audit of the entire implementation. The top-level C++ and header files alone total roughly 344,000 lines, including generated material.

| Area | Work performed |
| --- | --- |
| Ownership and storage | Focused inspection of `Ref.h`, `SlabPool.h`, `BodyPool.h`, and selected `Value.h`, `ValueVec.h`, `ValueHash.h`, `StrMap.h`, `Cycle.cpp`, and `Which.cpp` paths |
| Interpreter lifecycle and concurrency | Traced worker creation, completion, pool shutdown, destructor behavior, and embedding ownership in `Interpreter.cpp`, `Interpreter.h`, and `EmbedApi.cpp` |
| Native boundary | Read the extension/embedding public contracts and their implementations in `ExtApi.cpp` and `ExtCtx.h`; checked handle and exception-boundary design |
| Protocol servers | Focused JSON/parser and framing inspection in `JsonLite.h`, `McpServer.cpp`, `Lsp.cpp`, and selected `JupyterKernel.cpp` transport paths; exercised MCP and LSP over stdio |
| JavaScript backend | Focused inspection and differential execution of host I/O, process handling, Promises, and Supplies in `70-host.js`, `60-methods.js`, `86-async.js`, `87-supply.js`, and relevant object representations |
| Arithmetic and data | Sampled `IntOps.h`, `BigInt.h/.cpp`, `IntKernel.cpp`, `CNumeric.h`, `BuiltinsJson.cpp`, `DataCsv.cpp`, `DataDigest.cpp`, `DataZlib.cpp`, `DataRandom.cpp`, and selected `Zlib.cpp` paths; ran focused regressions and independent Python comparisons |
| Parsing and compilation | Sampled `Parser.cpp`, `AstSerial.cpp`, `Codegen.cpp`, `codegen/Operators.cpp`, `codegen/Refusals.cpp`, `Cnp.cpp`, `Jit.cpp`, and compiler dispatch in `main.cpp`; no complete AOT/JIT correctness claim |
| Sandbox and OS interaction | Inspected capability gates, delegation to handle/path methods, and parts of `SandboxOs.cpp`; ran read-only language-sandbox refusal checks |

The public documentation in `README.md`, `t/README.md`, the embedding headers, and relevant design/guide files was used to check intended contracts. Their historical suite results were not treated as measurements made by this review.

The pre-existing shared library identified itself through its filename as an older 5.5.0 artifact, so it was not used to claim reproduction of current embedding defects. Windows, Linux, Deno, browser/WASM, and sanitizer behavior were not tested. The source findings in those areas remain bounded by that limitation.

## Additional concerns not counted as findings

- **Signed serialization shifts:** [AstSerial.cpp:40](/Users/ash/rakupp/src/AstSerial.cpp:40) performs zigzag encoding with `v << 1` before conversion to unsigned. Negative signed left shifts are undefined under the project's C++17 model. This warrants a UBSan test and unsigned-first implementation; no resulting cache corruption was demonstrated here.
- **C ABI exception coverage:** many allocation and conversion operations occur outside the guarded evaluation path. The broad no-exception public contract deserves a failure-injection audit. No ordinary-input exception escape was reproduced, so this is not counted as a separate confirmed runtime defect.
- **Transport resource limits:** beyond the concrete Jupyter overflow, MCP line accumulation and Jupyter multipart/queue growth need explicit budgets if peers are not trusted. No large-memory stress test was run.
- **Native/AOT/JIT equivalence:** sampled whitelist and fallback code is not enough to approve these backends. Their differential gates should be run after changes, especially where a fast path writes containers before falling back.

## Suggested repair order

1. Resolve the payload-release and interpreter/worker lifetime protocols, and add deterministic sanitizer tests. Review the native handle contract at the same time.
2. Fix Jupyter frame-length arithmetic and enforce message budgets before signature parsing.
3. Repair async context propagation and supply materialization. Extend the JavaScript differential suite with the concrete programs above.
4. Correct JavaScript I/O/path/process behavior and short-write handling.
5. Make protocol JSON validation consistent and strict, then address allocator cleanup and reference-wrapper aliasing.

The strongest positive signal is that the selected core semantic checks passed, including independent arithmetic and compression comparisons. The strongest negative signal is that several boundary contracts—ownership, async context, byte/character positions, and message framing—are not preserved by the implementations that claim them. Those boundaries deserve targeted tests rather than relying only on broad language-suite totals.
