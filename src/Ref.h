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

template <class T>
class Ref {
    T* p_ = nullptr;
    void retain() const noexcept { if (p_) p_->refs_.fetch_add(1, std::memory_order_relaxed); }
    // The last owner's work is OUT of line, as shared_ptr's is: inlined, the
    // payload's whole destructor lands in every Value destructor that
    // releases one, and the hot paths paid for the code size (asg +2.5%).
    [[gnu::noinline]] static void destroy(T* p) noexcept {
        p->~T();
        SlabPool::dealloc(static_cast<void*>(const_cast<std::remove_const_t<T>*>(p)), sizeof(T));
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
        if (p_ != o.p_) { o.retain(); release(); p_ = o.p_; }
        return *this;
    }
    Ref& operator=(Ref&& o) noexcept {
        if (this != &o) { release(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    Ref& operator=(std::nullptr_t) noexcept { reset(); return *this; }

    void reset() noexcept { release(); p_ = nullptr; }
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
    void* mem = SlabPool::alloc(sizeof(T));
    T* p;
    try { p = ::new (mem) T(std::forward<A>(a)...); }
    catch (...) { SlabPool::dealloc(mem, sizeof(T)); throw; }
    return Ref<T>(p);
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
enum : std::uint32_t { kPayHyperIterTaken = 1 };

template <class T>
struct Body final : Payload {
    T v;
    template <class... A>
    explicit Body(std::in_place_t, A&&... a) : v(std::forward<A>(a)...) {}
    void destroySelf() noexcept override {
        this->~Body();
        SlabPool::dealloc(static_cast<void*>(this), sizeof(Body));
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
    Payload* p_ = nullptr;
    void retain() const noexcept { if (p_) p_->refs_.fetch_add(1, std::memory_order_relaxed); }
    [[gnu::noinline]] static void destroy(Payload* p) noexcept { p->destroySelf(); }
    void release() noexcept {
        if (p_ && p_->refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) destroy(p_);
    }
public:
    PayRef() noexcept = default;
    PayRef(std::nullptr_t) noexcept {}
    // ADOPTS a body whose count was already taken for this handle
    explicit PayRef(Payload* adopt, bool alreadyRetained) noexcept : p_(adopt) { if (!alreadyRetained) retain(); }
    PayRef(const PayRef& o) noexcept : p_(o.p_) { retain(); }
    PayRef(PayRef&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~PayRef() { release(); }
    PayRef& operator=(const PayRef& o) noexcept {
        if (p_ != o.p_) { o.retain(); release(); p_ = o.p_; }
        return *this;
    }
    PayRef& operator=(PayRef&& o) noexcept {
        if (this != &o) { release(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    PayRef& operator=(std::nullptr_t) noexcept { reset(); return *this; }
    void reset() noexcept { release(); p_ = nullptr; }
    Payload* get() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    long use_count() const noexcept { return p_ ? (long)p_->refs_.load(std::memory_order_acquire) : 0; }
    friend bool operator==(const PayRef& a, const PayRef& b) noexcept { return a.p_ == b.p_; }
    friend bool operator!=(const PayRef& a, const PayRef& b) noexcept { return a.p_ != b.p_; }
    friend bool operator==(const PayRef& a, std::nullptr_t) noexcept { return !a.p_; }
    friend bool operator!=(const PayRef& a, std::nullptr_t) noexcept { return a.p_ != nullptr; }
};

// The TYPED handle — what arrS()/codeS()/makePayload<T>() hand out, and what
// replaces std::shared_ptr<T> for a payload object everywhere else: `->` and
// `*` reach the object, get() is its address, and it converts to the slot.
template <class T>
class PRef {
    using B = typename BodyOf<T>::type;
    B* b_ = nullptr;
    void retain() const noexcept { if (b_) b_->refs_.fetch_add(1, std::memory_order_relaxed); }
    [[gnu::noinline]] static void destroy(B* b) noexcept { b->destroySelf(); }
    void release() noexcept {
        if (b_ && b_->refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) destroy(b_);
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
        if (b_ != o.b_) { o.retain(); release(); b_ = o.b_; }
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
    long use_count() const noexcept { return b_ ? (long)b_->refs_.load(std::memory_order_acquire) : 0; }
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
    void* mem = SlabPool::alloc(sizeof(Body<T>));
    Body<T>* b;
    try { b = ::new (mem) Body<T>(std::in_place, std::forward<A>(a)...); }
    catch (...) { SlabPool::dealloc(mem, sizeof(Body<T>)); throw; }
    return PRef<T>::adoptNew(b);
}

} // namespace rakupp
