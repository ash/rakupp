// Ref<T> — an intrusive, thread-safe reference-counted pointer: 8 bytes where
// a std::shared_ptr is 16 (VALUE32-PLAN design A, batch 4).
//
// The count lives IN the object (RefCounted, below), so the handle is the
// object pointer alone. Objects come from the same slab pool makePayload uses
// (SlabPool.h) and are made only through makeRef, which is what lets the
// destructor hand the block back by size. Copying a Ref is one relaxed atomic
// increment, as copying a shared_ptr was; the last release is acq_rel, so the
// destroying thread sees every write the others made.
//
// Only types that are private to Value's representation use it: an object
// another subsystem also holds by shared_ptr (a Callable, an Env) cannot be
// adopted, because its count lives in a control block Ref cannot see.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include "BodyPool.h"
#include "SlabPool.h"

namespace rakupp {

// The count. Copying an object (cloning a shared block before a write) must
// NOT copy its count — the clone starts with one owner, the Ref that made it.
struct RefCounted {
    mutable std::atomic<std::uint32_t> refs_{1};
    RefCounted() = default;
    RefCounted(const RefCounted&) noexcept : refs_{1} {}
    RefCounted& operator=(const RefCounted&) noexcept { return *this; }
};

// Where a Ref's objects are made: SlabPool, but for a type a Value reaches by
// handle (a specialization names BodyPool for it, before its first use).
template <class T> struct RefPoolOf { using pool = SlabPool; };

template <class T>
class Ref {
    T* p_ = nullptr;
    void retain() const noexcept { if (p_) p_->refs_.fetch_add(1, std::memory_order_relaxed); }
    // The last owner's work is OUT of line, as shared_ptr's is: inlined, the
    // payload's whole destructor lands in every Value destructor that
    // releases one, and the hot paths paid for the code size (asg +2.5%).
    [[gnu::noinline]] static void destroy(T* p) noexcept {
        p->~T();
        RefPoolOf<std::remove_const_t<T>>::pool::dealloc(static_cast<void*>(const_cast<std::remove_const_t<T>*>(p)), sizeof(T));
    }
    void release() noexcept {
        if (p_ && p_->refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) destroy(p_);
    }
    template <class U, class... A> friend Ref<U> makeRef(A&&...);
    explicit Ref(T* adopt) noexcept : p_(adopt) {}   // takes the count makeRef set to 1

public:
    Ref() noexcept = default;
    Ref(std::nullptr_t) noexcept {}
    Ref(const Ref& o) noexcept : p_(o.p_) { retain(); }
    Ref(Ref&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~Ref() { release(); }
    Ref& operator=(const Ref& o) noexcept {
        // (o.p_ is read BEFORE the release: `o` may live in what this alone
        // owned — `node = node->next` — and is gone once that is released)
        if (p_ != o.p_) { auto* n = o.p_; o.retain(); release(); p_ = n; }
        return *this;
    }
    Ref& operator=(Ref&& o) noexcept {
        if (this != &o) { release(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    Ref& operator=(std::nullptr_t) noexcept { reset(); return *this; }

    void reset() noexcept { release(); p_ = nullptr; }
    // The count, by hand, for a holder that keeps the pointer in bytes it
    // shares with something else (CowStr: VALUE32-PLAN design C). leak() hands
    // this Ref's count to the raw pointer, adopt() takes one back, and
    // releaseRaw() drops one.
    T* leak() noexcept { T* p = p_; p_ = nullptr; return p; }
    static Ref adopt(T* p) noexcept { return Ref(p); }
    static void releaseRaw(T* p) noexcept {
        if (p && p->refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) destroy(p);
    }
    T* get() const noexcept { return p_; }
    T& operator*() const noexcept { return *p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    // As shared_ptr::use_count: a snapshot, exact only when no other thread
    // can be copying the same object (the copy-on-write test uses it so).
    long use_count() const noexcept { return p_ ? (long)p_->refs_.load(std::memory_order_acquire) : 0; }

    friend bool operator==(const Ref& a, const Ref& b) noexcept { return a.p_ == b.p_; }
    friend bool operator!=(const Ref& a, const Ref& b) noexcept { return a.p_ != b.p_; }
    friend bool operator==(const Ref& a, std::nullptr_t) noexcept { return !a.p_; }
    friend bool operator!=(const Ref& a, std::nullptr_t) noexcept { return a.p_ != nullptr; }
};

template <class T, class... A>
inline Ref<T> makeRef(A&&... a) {
    static_assert(alignof(T) <= alignof(std::max_align_t), "makeRef does not serve over-aligned types");
    using Pool = typename RefPoolOf<std::remove_const_t<T>>::pool;
    void* mem = Pool::alloc(sizeof(T));
    T* p;
    try { p = ::new (mem) T(std::forward<A>(a)...); }
    catch (...) { Pool::dealloc(mem, sizeof(T)); throw; }
    return Ref<T>(p);
}

// HRef<T> — Ref<T> for a block a Value holds by HANDLE (BodyPool.h): the cold
// block. The same counted reference with the same surface; the block comes
// from BodyPool, and where handles are on the reference is 4 bytes.
template <class T>
class HRef {
    BodyHandle<T> s_;
    void retain() const noexcept { if (s_) s_.raw()->refs_.fetch_add(1, std::memory_order_relaxed); }
    [[gnu::noinline]] static void destroy(T* p) noexcept {   // (out of line, as Ref's)
        p->~T();
        BodyPool::dealloc(static_cast<void*>(const_cast<std::remove_const_t<T>*>(p)), sizeof(T));
    }
    void release() noexcept {
        if (s_) { T* p = s_.raw(); if (p->refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) destroy(p); }
    }
    template <class U, class... A> friend HRef<U> makeHRef(A&&...);
    explicit HRef(T* adopt) noexcept { s_.set(adopt); }   // takes the count makeHRef set to 1

public:
    HRef() noexcept = default;
    HRef(std::nullptr_t) noexcept {}
    HRef(const HRef& o) noexcept : s_(o.s_) { retain(); }
    HRef(HRef&& o) noexcept : s_(o.s_) { o.s_ = {}; }
    ~HRef() { release(); }
    HRef& operator=(const HRef& o) noexcept {
        if (s_ != o.s_) { auto n = o.s_; o.retain(); release(); s_ = n; }   // (as Ref's)
        return *this;
    }
    HRef& operator=(HRef&& o) noexcept {
        if (this != &o) { release(); s_ = o.s_; o.s_ = {}; }
        return *this;
    }
    HRef& operator=(std::nullptr_t) noexcept { reset(); return *this; }
    void reset() noexcept { release(); s_ = {}; }
    T* get() const noexcept { return s_.get(); }
    T& operator*() const noexcept { return *s_.raw(); }
    T* operator->() const noexcept { return s_.raw(); }
    explicit operator bool() const noexcept { return (bool)s_; }
    long use_count() const noexcept { return s_ ? (long)s_.raw()->refs_.load(std::memory_order_acquire) : 0; }

    friend bool operator==(const HRef& a, const HRef& b) noexcept { return a.s_ == b.s_; }
    friend bool operator!=(const HRef& a, const HRef& b) noexcept { return a.s_ != b.s_; }
    friend bool operator==(const HRef& a, std::nullptr_t) noexcept { return !a.s_; }
    friend bool operator!=(const HRef& a, std::nullptr_t) noexcept { return (bool)a.s_; }
};

template <class T, class... A>
inline HRef<T> makeHRef(A&&... a) {
    static_assert(alignof(T) <= alignof(std::max_align_t), "makeHRef does not serve over-aligned types");
    void* mem = BodyPool::alloc(sizeof(T));
    T* p;
    try { p = ::new (mem) T(std::forward<A>(a)...); }
    catch (...) { BodyPool::dealloc(mem, sizeof(T)); throw; }
    return HRef<T>(p);
}

} // namespace rakupp

namespace rakupp {

// ---------------------------------------------------------------------------
// The VALUE PAYLOAD: what Value::p_ points at (VALUE32-PLAN design A, batch 4).
//
// A payload's kind is not known to the pointer — Value::pk_ says it — so the
// body carries a virtual destroy and the slot is one untyped 8-byte PayRef.
// The payload classes themselves (ValueList, ValueMap, Callable, ObjectData,
// MatchData, Value) are untouched: each is wrapped in a Body<T>, so a
// ValueList stays the plain vector every `ValueList args` local is. A body is
// 16 bytes of header (vtable, count, flags) — what allocate_shared's control
// block was — and the object right behind it.
struct Payload {
    mutable std::atomic<std::uint32_t> refs_{1};
    // Per-BODY flags, shared by every Value that holds this body: a HyperSeq's
    // element buffer records here that its one iterator was handed out.
    mutable std::atomic<std::uint32_t> pflags_{0};
    Payload() = default;
    Payload(const Payload&) = delete;
    Payload& operator=(const Payload&) = delete;
    virtual void destroySelf() noexcept = 0;
protected:
    ~Payload() = default;
};
enum : std::uint32_t {
    kPayHyperIterTaken = 1,
    // The cycle collector's bits (Cycle.h, CYCLE-GC-PLAN.md). BUFFERED: the
    // body is a candidate, and bits 8 and up hold its place in the buffer, so
    // the body's death can take it out (cycleForget) — the buffer holds no
    // count. IN_DESTROY_REG: an object the DESTROY registry holds one count
    // on, which the collector's walk accounts as known. FINALIZED: the
    // collector already ran this object's DESTROY; the registry sweep won't.
    kPayCycleBuffered = 2,
    kPayInDestroyReg = 4,
    kPayCycleFinalized = 8,
    // ACYCLIC: an object of a class whose attributes hold only values
    // (cycleClassify); it stays untracked even after a collection.
    kPayCycleAcyclic = 16,
    kPayKeptDetached = 32,   // a kept `@a.head(n)`'s buffer, its array refilled (keepEndsContainers)
    kPayFatRat = 64,         // a Rat body (PK::Rat) whose Value is a FatRat: its type, not its parts
    // WHICH_ID: the body has a `.WHICH` serial in Which.cpp's table, which its
    // death takes out (whichForget), so a body later made at its address gets
    // a serial of its own
    kPayWhichId = 128,
};
constexpr unsigned kPayCycleIdxShift = 8;
void cycleForget(Payload* p) noexcept;   // Cycle.cpp
void whichForget(Payload* p) noexcept;   // Which.cpp

// The count's top bit: a TRACKED body (an object's: see makePayload) whose
// every release that leaves it alive makes it a cycle-collector candidate —
// the drop that strands a group of objects can be any release at all, a
// temporary's included. The bit lives IN the count so the inline release
// test stays one compare: a tracked body's old count reads negative, as a
// last release's reads 1, and only those two leave the inline path. Every
// reader of the count masks it (use_count).
constexpr std::uint32_t kRefTracked = 0x80000000u;
constexpr std::uint32_t kRefMask = 0x7fffffffu;
extern bool g_cycleTrack;                       // Cycle.cpp: objects are tracked
void cycleNoteRelease(Payload* p) noexcept;     // Cycle.cpp
[[gnu::noinline]] inline void payloadReleaseSlow(Payload* p, std::uint32_t old) noexcept {
    if ((old & kRefMask) == 1) {
        const std::uint32_t f = p->pflags_.load(std::memory_order_relaxed);
        if (f & kPayCycleBuffered) cycleForget(p);
        if (f & kPayWhichId) whichForget(p);
        p->destroySelf();
    }
    else if (old & kRefTracked) cycleNoteRelease(p);
}
struct ObjectData;
// An object's block is its body and, right behind it, the hash its attributes
// are kept in (AttrTable): one allocation an object, as when the hash was a
// member of ObjectData. Defined in Value.h, once both are complete.
inline std::size_t objectTailSize() noexcept;
inline void objectAdoptTail(ObjectData& od, void* tail) noexcept;
inline std::size_t objectTailOf(const ObjectData& od) noexcept;   // 0, or objectTailSize()

extern const bool g_race;                                  // RaceDetector.h: --race is on
void raceForgetRange(const void* begin, std::size_t bytes);  // …and a dying body's shadows go
template <class T>
struct Body final : Payload {
    T v;
    template <class... A>
    explicit Body(std::in_place_t, A&&... a) : v(std::forward<A>(a)...) {}
    void destroySelf() noexcept override {
        std::size_t n = sizeof(Body);
        if constexpr (std::is_same<T, ObjectData>::value) n += objectTailOf(v);
        if (g_race) raceForgetRange(this, n);
        this->~Body();
        BodyPool::dealloc(static_cast<void*>(this), n);
    }
};

// How a handle reaches its object. Every payload kind but one is a Body<T>
// with the object inside; a Value payload (a Pair's value, a container cell)
// may instead ALIAS a Value that lives elsewhere — a hash entry handed out as
// a live Pair (ValueHash::aliasOf) — so its bodies share a base that holds
// the target pointer, and an owning body points it at its own Value.
struct Value;
struct ValueBodyBase : Payload {
    Value* target = nullptr;
};
template <> struct Body<Value>;   // defined in Value.h, once Value is complete
template <class T> struct BodyOf {
    using type = Body<T>;
    static T* obj(type* b) noexcept { return &b->v; }
};
template <> struct BodyOf<Value> {
    using type = ValueBodyBase;
    static Value* obj(type* b) noexcept { return b->target; }
};

// The untyped slot. Copy = one relaxed increment; the last release destroys
// through the body's own kind, out of line.
class PayRef {
    BodyHandle<Payload> s_;   // (4 bytes where handles are on: BodyPool.h)
    void retain() const noexcept { if (s_) s_.raw()->refs_.fetch_add(1, std::memory_order_relaxed); }
    void release() noexcept {
        if (s_) {
            Payload* p = s_.raw();
            std::uint32_t old = p->refs_.fetch_sub(1, std::memory_order_acq_rel);
            if ((std::int32_t)old <= 1) payloadReleaseSlow(p, old);
        }
    }
public:
    PayRef() noexcept = default;
    PayRef(std::nullptr_t) noexcept {}
    // ADOPTS a body whose count was already taken for this handle
    explicit PayRef(Payload* adopt, bool alreadyRetained) noexcept { s_.set(adopt); if (!alreadyRetained) retain(); }
    PayRef(const PayRef& o) noexcept : s_(o.s_) { retain(); }
    PayRef(PayRef&& o) noexcept : s_(o.s_) { o.s_ = {}; }
    ~PayRef() { release(); }
    PayRef& operator=(const PayRef& o) noexcept {
        if (s_ != o.s_) { auto n = o.s_; o.retain(); release(); s_ = n; }   // (as Ref's)
        return *this;
    }
    PayRef& operator=(PayRef&& o) noexcept {
        if (this != &o) { release(); s_ = o.s_; o.s_ = {}; }
        return *this;
    }
    PayRef& operator=(std::nullptr_t) noexcept { reset(); return *this; }
    void reset() noexcept { release(); s_ = {}; }
    Payload* get() const noexcept { return s_.get(); }
    // The body, for a caller that knows the slot is not empty (Value's pk_ says so).
    Payload* raw() const noexcept { return s_.raw(); }
    explicit operator bool() const noexcept { return (bool)s_; }
    long use_count() const noexcept { return s_ ? (long)(s_.raw()->refs_.load(std::memory_order_acquire) & kRefMask) : 0; }
    friend bool operator==(const PayRef& a, const PayRef& b) noexcept { return a.s_ == b.s_; }
    friend bool operator!=(const PayRef& a, const PayRef& b) noexcept { return a.s_ != b.s_; }
    friend bool operator==(const PayRef& a, std::nullptr_t) noexcept { return !a.s_; }
    friend bool operator!=(const PayRef& a, std::nullptr_t) noexcept { return (bool)a.s_; }
};

// The TYPED handle — what arrS()/codeS()/makePayload<T>() hand out, and what
// replaces std::shared_ptr<T> for a payload object everywhere else: `->` and
// `*` reach the object, get() is its address, and it converts to the slot.
template <class T>
class PRef {
    using B = typename BodyOf<T>::type;
    B* b_ = nullptr;
    void retain() const noexcept { if (b_) b_->refs_.fetch_add(1, std::memory_order_relaxed); }
    void release() noexcept {
        if (b_) {
            std::uint32_t old = b_->refs_.fetch_sub(1, std::memory_order_acq_rel);
            if ((std::int32_t)old <= 1) payloadReleaseSlow(b_, old);
        }
    }
    template <class U, class... A> friend PRef<U> makePayload(A&&...);
    template <class U> friend class PRef;
    struct Adopt {};
    PRef(B* b, Adopt) noexcept : b_(b) {}
public:
    // Takes over a body that was just made, its count already 1 for us.
    static PRef adoptNew(B* b) noexcept { return PRef(b, Adopt{}); }
    using element_type = T;
    PRef() noexcept = default;
    PRef(std::nullptr_t) noexcept {}
    PRef(const PRef& o) noexcept : b_(o.b_) { retain(); }
    PRef(PRef&& o) noexcept : b_(o.b_) { o.b_ = nullptr; }
    ~PRef() { release(); }
    PRef& operator=(const PRef& o) noexcept {
        if (b_ != o.b_) { auto* n = o.b_; o.retain(); release(); b_ = n; }   // (as Ref's)
        return *this;
    }
    PRef& operator=(PRef&& o) noexcept {
        if (this != &o) { release(); b_ = o.b_; o.b_ = nullptr; }
        return *this;
    }
    PRef& operator=(std::nullptr_t) noexcept { reset(); return *this; }

    // The slot's body, typed — the caller (Value, which knows pk_) vouches for
    // the kind. Takes its own count.
    static PRef fromSlot(const PayRef& s) noexcept {
        PRef r; r.b_ = static_cast<B*>(s.get()); r.retain(); return r;
    }
    // …and from the object's own address, for a holder that keeps only that
    // (a builtin that must not own its own Callable): valid while some owner
    // keeps the body alive, which a running routine's caller does.
    static PRef fromObject(T* obj) noexcept {
        PRef r;
        if (obj) {
            r.b_ = reinterpret_cast<B*>(reinterpret_cast<char*>(obj) - bodyOffset());
            r.retain();
        }
        return r;
    }
    static std::size_t bodyOffset() noexcept {
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
        return offsetof(Body<T>, v);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    }
    PayRef slot() const noexcept { return PayRef(b_, false); }

    void reset() noexcept { release(); b_ = nullptr; }
    T* get() const noexcept { return b_ ? BodyOf<T>::obj(b_) : nullptr; }
    T& operator*() const noexcept { return *BodyOf<T>::obj(b_); }
    T* operator->() const noexcept { return BodyOf<T>::obj(b_); }
    explicit operator bool() const noexcept { return b_ != nullptr; }
    long use_count() const noexcept { return b_ ? (long)(b_->refs_.load(std::memory_order_acquire) & kRefMask) : 0; }
    Payload* body() const noexcept { return b_; }
    friend bool operator==(const PRef& a, const PRef& b) noexcept { return a.b_ == b.b_; }
    friend bool operator!=(const PRef& a, const PRef& b) noexcept { return a.b_ != b.b_; }
    friend bool operator==(const PRef& a, std::nullptr_t) noexcept { return !a.b_; }
    friend bool operator!=(const PRef& a, std::nullptr_t) noexcept { return a.b_ != nullptr; }
    friend bool operator<(const PRef& a, const PRef& b) noexcept { return a.b_ < b.b_; }
};

// THE way a payload object is made (it replaces std::make_shared and the old
// allocate_shared makePayload alike): one slab block, header and object.
template <class T, class... A>
inline PRef<T> makePayload(A&&... a) {
    static_assert(alignof(Body<T>) <= alignof(std::max_align_t), "payloads are not over-aligned");
#if RAKUPP_BODY_HANDLES
    static_assert(sizeof(Body<T>) + 256 <= BodyPool::kMaxBody, "a payload body larger than BodyPool serves");
#endif
    if constexpr (std::is_same<T, ObjectData>::value) {
        // the body, then its attribute hash behind it (objectAdoptTail); a copy
        // is made by assignment, into the hash already in place
        static_assert(sizeof...(A) <= 1, "an object is made empty or copied from another");
        const std::size_t n = sizeof(Body<T>) + objectTailSize();
        void* mem = BodyPool::alloc(n);
        Body<T>* b = ::new (mem) Body<T>(std::in_place);
        objectAdoptTail(b->v, static_cast<char*>(mem) + sizeof(Body<T>));
        try { (void(b->v = std::forward<A>(a)), ...); }
        catch (...) { b->destroySelf(); throw; }
        if (g_cycleTrack) b->refs_.store(1u | kRefTracked, std::memory_order_relaxed);
        return PRef<T>::adoptNew(b);
    }
    void* mem = BodyPool::alloc(sizeof(Body<T>));
    Body<T>* b;
    try { b = ::new (mem) Body<T>(std::in_place, std::forward<A>(a)...); }
    catch (...) { BodyPool::dealloc(mem, sizeof(Body<T>)); throw; }
    return PRef<T>::adoptNew(b);
}

} // namespace rakupp
