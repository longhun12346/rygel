#pragma once

#define __STDC_FORMAT_MACROS
#include <algorithm>
#include <functional>
#include <inttypes.h>
#include <limits.h>
#include <limits>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <utility>
#ifdef _WIN32
    #include <intrin.h>
#endif
#ifdef _MSC_VER
    #define ENABLE_INTSAFE_SIGNED_FUNCTIONS
    #include <intsafe.h>
    #pragma intrinsic(_BitScanReverse)
    #pragma intrinsic(_BitScanReverse64)
    #pragma intrinsic(__rdtsc)
#endif

// ------------------------------------------------------------------------
// Config
// ------------------------------------------------------------------------

#define DYNAMICARRAY_BASE_CAPACITY 8
#define DYNAMICARRAY_GROWTH_FACTOR 2

// Must be a power-of-two
#define HASHSET_BASE_CAPACITY 32
#define HASHSET_MAX_LOAD_FACTOR 0.4f

#define FMT_STRING_BASE_CAPACITY 128
#define FMT_STRING_GROWTH_FACTOR 1.5f
#define FMT_STRING_PRINT_BUFFER_SIZE 1024

// ------------------------------------------------------------------------
// Utilities
// ------------------------------------------------------------------------

#define STRINGIFY_(a) #a
#define STRINGIFY(a) STRINGIFY_(a)
#define CONCAT_(a, b) a ## b
#define CONCAT(a, b) CONCAT_(a, b)
#define UNIQUE_ID(prefix) CONCAT(prefix, __LINE__)
#define FORCE_EXPAND(x) x

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #define ARCH_LITTLE_ENDIAN
#else
    #error Machine architecture not supported
#endif

#if defined(__GNUC__)
    #define GCC_PRAGMA(Pragma) \
        _Pragma(STRINGIFY(Pragma))
    #define GCC_PUSH_IGNORE(Option) \
        GCC_PRAGMA(GCC diagnostic push) \
        GCC_PRAGMA(GCC diagnostic ignored STRINGIFY(Option))
    #define GCC_POP_IGNORE() \
        GCC_PRAGMA(GCC diagnostic pop)

    #define MAYBE_UNUSED __attribute__((unused))
    #define FORCE_INLINE __attribute__((always_inline)) inline
    #define LIKELY(Cond) __builtin_expect(!!(Cond), 1)
    #define UNLIKELY(Cond) __builtin_expect(!!(Cond), 0)

    #ifndef SCNd8
        #define SCNd8 "hhd"
    #endif
    #ifndef SCNi8
        #define SCNi8 "hhd"
    #endif
    #ifndef SCNu8
        #define SCNu8 "hhu"
    #endif
#elif defined(_MSC_VER)
    #define GCC_PRAGMA(Pragma)
    #define GCC_PUSH_IGNORE(Option)
    #define GCC_POP_IGNORE()

    #define MAYBE_UNUSED
    #define FORCE_INLINE __forceinline
    #define LIKELY(Cond) (Cond)
    #define UNLIKELY(Cond) (Cond)
#else
    #error Compiler not supported
#endif

#define Assert(Cond) \
    do { \
        if (!(Cond)) { \
            fputs("Assertion '" STRINGIFY(Cond) "' failed", stderr); \
            abort(); \
        } \
    } while (false)
#ifndef NDEBUG
    #define DebugAssert(Cond) Assert(Cond)
#else
    #define DebugAssert(Cond) \
        do { \
            (void)(Cond); \
        } while (false)
#endif
#define StaticAssert(Cond) \
    static_assert((Cond), STRINGIFY(Cond))

#define READ_ONLY_GLOBAL(Type, Name) \
    static Type Name##_priv; \
    const Type *const Name = &Name##_priv
#if defined(SEPARATE_RUNNER) && defined(_WIN32)
    #ifdef BUILDING_RUNNER
        #define RUNNER_SYMBOL __declspec(dllexport)
        #define CORE_SYMBOL __declspec(dllimport)
    #else
        #define RUNNER_SYMBOL __declspec(dllimport)
        #define CORE_SYMBOL __declspec(dllexport)
    #endif
#else
    #define RUNNER_SYMBOL
    #define CORE_SYMBOL
#endif

constexpr uint16_t MakeUInt16(uint8_t high, uint8_t low) { return ((uint16_t)high << 8) | low; }
constexpr uint32_t MakeUInt32(uint16_t high, uint16_t low) { return ((uint32_t)high << 16) | low; }
constexpr uint64_t MakeUInt64(uint32_t high, uint32_t low) { return ((uint64_t)high << 32) | low; }

constexpr size_t Mebibytes(size_t len) { return len * 1024 * 1024; }
constexpr size_t Kibibytes(size_t len) { return len * 1024; }
constexpr size_t Megabytes(size_t len) { return len * 1000 * 1000; }
constexpr size_t Kilobytes(size_t len) { return len * 1000; }

#define OffsetOf(Type, Member) ((size_t)&(((Type *)nullptr)->Member))

template <typename T, size_t N>
constexpr size_t CountOf(T const (&)[N])
{
    return N;
}

#if defined(__GNUC__)
    static inline int CountLeadingZeros(uint32_t u)
    {
        if (!u) {
            return 32;
        }

        return __builtin_clz(u);
    }
    static inline int CountLeadingZeros(uint64_t u)
    {
        if (!u) {
            return 64;
        }

    #if UINT64_MAX == ULONG_MAX
        return __builtin_clzl(u);
    #elif UINT64_MAX == ULLONG_MAX
        return __builtin_clzll(u);
    #else
        #error Neither unsigned long nor unsigned long long is a 64-bit unsigned integer
    #endif
    }
#elif defined(_MSC_VER)
    static inline int CountLeadingZeros(uint32_t u)
    {
        unsigned long leading_zero;
        if (_BitScanReverse(&leading_zero, u)) {
            return 31 - leading_zero;
        } else {
            return 32;
        }
    }
    static inline int CountLeadingZeros(uint64_t u)
    {
        unsigned long leading_zero;
    #ifdef _WIN64
        if (_BitScanReverse64(&leading_zero, u)) {
            return 63 - leading_zero;
        } else {
            return 64;
        }
    #else
        if (_BitScanReverse(&leading_zero, u >> 32)) {
            return 31 - leading_zero;
        } else if (_BitScanReverse(&leading_zero, (uint32_t)u)) {
            return 63 - leading_zero;
        } else {
            return 64;
        }
    #endif
    }
#else
    #error No implementation of CountLeadingZeros() for this compiler / toolchain
#endif

template <typename T, typename = typename std::enable_if<std::is_enum<T>::value, T>>
typename std::underlying_type<T>::type MaskEnum(T value)
{
    return 1 << static_cast<typename std::underlying_type<T>::type>(value);
}

template <typename Fun>
class ScopeGuard {
    Fun f;
    bool enabled = true;

public:
    ScopeGuard() = delete;
    ScopeGuard(Fun f_) : f(std::move(f_)) {}
    ~ScopeGuard()
    {
        if (enabled)
            f();
    }

    // Those two methods allow for lambda assignement, which makes possible the VZ_DEFER
    // syntax possible.
    ScopeGuard(ScopeGuard &&other)
        : f(std::move(other.f)), enabled(other.enabled)
    {
        other.disable();
    }

    void disable() { enabled = false; }

    ScopeGuard(ScopeGuard &) = delete;
    ScopeGuard& operator=(const ScopeGuard &) = delete;
};

// Honestly, I don't understand all the details in there, this comes from Andrei Alexandrescu.
// https://channel9.msdn.com/Shows/Going+Deep/C-and-Beyond-2012-Andrei-Alexandrescu-Systematic-Error-Handling-in-C
enum class ScopeGuardDeferHelper {};
template <typename Fun>
ScopeGuard<Fun> operator+(ScopeGuardDeferHelper, Fun &&f)
{
    return ScopeGuard<Fun>(std::forward<Fun>(f));
}

// Write 'DEFER { code };' to do something at the end of the current scope, you
// can use DEFER_N(Name) if you need to disable the guard for some reason, and
// DEFER_NC(Name, Captures) if you need to capture values.
#define DEFER \
    auto UNIQUE_ID(defer) = ScopeGuardDeferHelper() + [&]()
#define DEFER_N(Name) \
    auto Name = ScopeGuardDeferHelper() + [&]()
#define DEFER_NC(Name, ...) \
    auto Name = ScopeGuardDeferHelper() + [&, __VA_ARGS__]()

// ------------------------------------------------------------------------
// Overflow Safety
// ------------------------------------------------------------------------

#if defined(__GNUC__)
    template <typename T> bool AddOverflow(T a, T b, T *rret)
        { return __builtin_add_overflow(a, b, rret); }
    template <typename T> bool SubOverflow(T a, T b, T *rret)
        { return __builtin_sub_overflow(a, b, rret); }
    template <typename T> bool MulOverflow(T a, T b, T *rret)
        { return __builtin_mul_overflow(a, b, rret); }
#elif defined(_MSC_VER)
    // Unfortunately intsafe.h functions change rret to an invalid value when overflow would
    // occur. We want to keep the old value in, like GCC instrinsics do. The good solution
    // would be to implement our own functions, but I'm too lazy to do it right now and test
    // it's working. That'll do for now.
    #define DEFINE_OVERFLOW_OPERATION(Type, Operation, Func) \
        static inline bool Operation##Overflow(Type a, Type b, Type *rret) \
        { \
            Type backup = *rret; \
            if (!Func(a, b, rret)) { \
                return false; \
            } else { \
                *rret = backup; \
                return true; \
            } \
        }
    #define OVERFLOW_OPERATIONS(Type, Prefix) \
        DEFINE_OVERFLOW_OPERATION(Type, Add, Prefix##Add) \
        DEFINE_OVERFLOW_OPERATION(Type, Sub, Prefix##Sub) \
        DEFINE_OVERFLOW_OPERATION(Type, Mul, Prefix##Mult)

    OVERFLOW_OPERATIONS(signed char, Int8)
    OVERFLOW_OPERATIONS(unsigned char, UInt8)
    OVERFLOW_OPERATIONS(short, Short)
    OVERFLOW_OPERATIONS(unsigned short, UShort)
    OVERFLOW_OPERATIONS(int, Int)
    OVERFLOW_OPERATIONS(unsigned int, UInt)
    OVERFLOW_OPERATIONS(long, Long)
    OVERFLOW_OPERATIONS(unsigned long, ULong)
    OVERFLOW_OPERATIONS(long long, LongLong)
    OVERFLOW_OPERATIONS(unsigned long long, ULongLong)

    #undef OVERFLOW_OPERATIONS
    #undef DEFINE_OVERFLOW_OPERATION
#else
    #error Overflow operations are not implemented for this compilter
#endif

// ------------------------------------------------------------------------
// Memory / Allocator
// ------------------------------------------------------------------------

struct AllocatorList {
    AllocatorList *prev;
    AllocatorList *next;
};

struct AllocatorBucket {
    AllocatorList head;
    alignas(8) uint8_t data[];
};

class Allocator {
    AllocatorList list = { &list, &list };

public:
    enum Flag {
        ZeroMemory = 1,
        Resizable = 2
    };

    Allocator() = default;
    Allocator(Allocator &) = delete;
    Allocator &operator=(const Allocator &) = delete;
    ~Allocator() { ReleaseAll(); }

    static void ReleaseAll(Allocator *alloc);

    static void *Allocate(Allocator *alloc, size_t size, unsigned int flags = 0);
    static void Resize(Allocator *alloc, void **ptr, size_t old_size, size_t new_size,
                       unsigned int flags = 0);
    static void Release(Allocator *alloc, void *ptr, size_t size);

private:
    void ReleaseAll();

    void *Allocate(size_t size, unsigned int flags = 0);
    void Resize(void **ptr, size_t old_size, size_t new_size, unsigned int flags = 0);
    void Release(void *ptr, size_t size);
};

// ------------------------------------------------------------------------
// Collections
// ------------------------------------------------------------------------

template <typename T>
struct ArraySlice {
    size_t offset;
    size_t len;

    ArraySlice() = default;
    ArraySlice(size_t offset, size_t len) : offset(offset), len(len) {}
};

// I'd love to make ArrayRef default to { nullptr, 0 } but unfortunately that makes
// it a non-POD and prevents putting it in a union.
template <typename T>
struct ArrayRef {
    T *ptr;
    size_t len;

    ArrayRef() = default;
    constexpr ArrayRef(T &value) : ptr(&value), len(1) {}
    constexpr ArrayRef(T *ptr_, size_t len_) : ptr(ptr_), len(len_) {}
    template <size_t N>
    constexpr ArrayRef(T (&arr)[N]) : ptr(arr), len(N) {}

    void Reset()
    {
        ptr = nullptr;
        len = 0;
    }

    T *begin() const { return ptr; }
    T *end() const { return ptr + len; }

    bool IsValid() const { return ptr; }

    T &operator[](size_t idx) const
    {
        DebugAssert(idx < len);
        return ptr[idx];
    }

    operator ArrayRef<const T>() const { return ArrayRef<const T>(ptr, len); }

    ArrayRef Take(ArraySlice<T> slice) const
    {
        ArrayRef<T> sub;

        if (slice.len > len || slice.offset > len - slice.len) {
            sub = {};
            return sub;
        }
        sub.ptr = ptr + slice.offset;
        sub.len = slice.len;

        return sub;
    }
    ArrayRef Take(size_t offset, size_t len) const
        { return Take(ArraySlice<T>(offset, len)); }
};

// Unfortunately C strings ("foobar") are implicity converted to ArrayRef<T> with the
// templated array constructor. But the NUL terminator is obviously counted in. Since I
// don't want to give up implicit conversion for every type, so instead we need to
// specialize ArrayRef<const char> and ArrayRef<char>.
template <>
struct ArrayRef<const char> {
    const char *ptr;
    size_t len;

    ArrayRef() = default;
    constexpr ArrayRef(const char &value) : ptr(&value), len(1) {}
    explicit constexpr ArrayRef(const char *ptr_, size_t len_) : ptr(ptr_), len(len_) {}
    template <size_t N>
    explicit constexpr ArrayRef(const char (&arr)[N]) : ptr(arr), len(N) {}

    void Reset()
    {
        ptr = nullptr;
        len = 0;
    }

    const char *begin() const { return ptr; }
    const char *end() const { return ptr + len; }

    bool IsValid() const { return ptr; }

    const char &operator[](size_t idx) const
    {
        DebugAssert(idx < len);
        return ptr[idx];
    }

    ArrayRef Take(size_t sub_offset, size_t sub_len) const
    {
        ArrayRef<const char> sub;

        if (sub_len > len || sub_offset > len - sub_len) {
            sub = {};
            return sub;
        }
        sub.ptr = ptr + sub_offset;
        sub.len = sub_len;

        return sub;
    }
    ArrayRef Take(const ArraySlice<const char> &slice) const
        { return Take(slice.offset, slice.len); }
};
template <>
struct ArrayRef<char> {
    char *ptr;
    size_t len;

    ArrayRef() = default;
    constexpr ArrayRef(char &value) : ptr(&value), len(1) {}
    explicit constexpr ArrayRef(char *ptr_, size_t len_) : ptr(ptr_), len(len_) {}
    template <size_t N>
    explicit constexpr ArrayRef(char (&arr)[N]) : ptr(arr), len(N) {}

    void Reset()
    {
        ptr = nullptr;
        len = 0;
    }

    char *begin() const { return ptr; }
    char *end() const { return ptr + len; }

    bool IsValid() const { return ptr; }

    char &operator[](size_t idx) const
    {
        DebugAssert(idx < len);
        return ptr[idx];
    }

    operator ArrayRef<const char>() const { return ArrayRef<const char>(ptr, len); }

    ArrayRef Take(size_t sub_offset, size_t sub_len) const
    {
        ArrayRef<char> sub;

        if (sub_len > len || sub_offset > len - sub_len) {
            sub = {};
            return sub;
        }
        sub.ptr = ptr + sub_offset;
        sub.len = sub_len;

        return sub;
    }
    ArrayRef Take(const ArraySlice<char> &slice) const
        { return Take(slice.offset, slice.len); }
};

template <typename T>
static inline constexpr ArrayRef<T> MakeArrayRef(T *ptr, size_t len)
{
    return ArrayRef<T>(ptr, len);
}
template <typename T, size_t N>
static inline constexpr ArrayRef<T> MakeArrayRef(T (&arr)[N])
{
    return ArrayRef<T>(arr, N);
}

static inline ArrayRef<char> MakeStrRef(char *str)
{
    return ArrayRef<char>(str, strlen(str));
}
static inline ArrayRef<const char> MakeStrRef(const char *str)
{
    return ArrayRef<const char>(str, strlen(str));
}
static inline ArrayRef<char> MakeStrRef(char *str, size_t max_len)
{
    return ArrayRef<char>(str, strnlen(str, max_len));
}
static inline ArrayRef<const char> MakeStrRef(const char *str, size_t max_len)
{
    return ArrayRef<const char>(str, strnlen(str, max_len));
}

template <typename T, size_t N>
class FixedArray {
public:
    T data[N];

    typedef T value_type;
    typedef T *iterator_type;

    operator ArrayRef<T>() { return ArrayRef<T>(data, N); }
    operator ArrayRef<const T>() const { return ArrayRef<const T>(data, N); }

    T *begin() { return data; }
    const T *begin() const { return data; }
    T *end() { return data + N; }
    const T *end() const { return data + N; }

    T &operator[](size_t idx)
    {
        DebugAssert(idx < N);
        return data[idx];
    }
    const T &operator[](size_t idx) const
    {
        DebugAssert(idx < N);
        return data[idx];
    }

    ArrayRef<T> Take(size_t offset, size_t len) const
        { return ArrayRef<T>(data, N).Take(offset, len); }
    ArrayRef<T> Take(ArraySlice<T> slice) const
        { return ArrayRef<T>(data, N).Take(slice); }
};

template <typename T, size_t N>
class LocalArray {
public:
    T data[N];
    size_t len = 0;

    typedef T value_type;
    typedef T *iterator_type;

    // TODO: Check behavior of Append() after Clear()
    void Clear()
    {
        for (size_t i = 0; i < len; i++) {
            data[i].~T();
        }
        len = 0;
    }

    operator ArrayRef<T>() { return ArrayRef<T>(data, len); }
    operator ArrayRef<const T>() const { return ArrayRef<const T>(data, len); }

    T *begin() { return data; }
    const T *begin() const { return data; }
    T *end() { return data + len; }
    const T *end() const { return data + len; }

    T &operator[](size_t idx)
    {
        DebugAssert(idx < len);
        return data[idx];
    }
    const T &operator[](size_t idx) const
    {
        DebugAssert(idx < len);
        return data[idx];
    }

    T *Append(const T &value)
    {
        DebugAssert(len < N);

        T *it = data + len;
        *it = value;
        len++;

        return it;
    }
    T *Append(ArrayRef<const T> values)
    {
        DebugAssert(values.len <= N - len);

        T *it = data + len;
        for (size_t i = 0; i < values.len; i++) {
            data[len + i] = values[i];
        }
        len += values.len;

        return it;
    }

    void RemoveFrom(size_t first)
    {
        if (first >= len)
            return;

        for (size_t i = first; i < len; i++) {
            data[i].~T();
        }
        len = first;
    }
    void RemoveLast(size_t count = 1) { RemoveFrom(len - count); }

    ArrayRef<T> Take(size_t offset, size_t len) const
        { return ArrayRef<T>(data, this->len).Take(offset, len); }
    ArrayRef<T> Take(ArraySlice<T> slice) const
        { return ArrayRef<T>(data, this->len).Take(slice); }
};

template <typename T>
class HeapArray {
    // StaticAssert(std::is_trivially_copyable<T>::value);

public:
    T *ptr = nullptr;
    size_t len = 0;
    size_t capacity = 0;
    Allocator *allocator = nullptr;

    typedef T value_type;
    typedef T *iterator_type;

    HeapArray() = default;
    HeapArray(HeapArray &) = delete;
    HeapArray &operator=(const HeapArray &) = delete;
    ~HeapArray() { Clear(); }

    void Clear() { SetCapacity(0); }

    operator ArrayRef<T>() { return ArrayRef<T>(ptr, len); }
    operator ArrayRef<const T>() const { return ArrayRef<const T>(ptr, len); }

    T *begin() { return ptr; }
    const T *begin() const { return ptr; }
    T *end() { return ptr + len; }
    const T *end() const { return ptr + len; }

    T &operator[](size_t idx)
    {
        DebugAssert(idx < len);
        return ptr[idx];
    }
    const T &operator[](size_t idx) const
    {
        DebugAssert(idx < len);
        return ptr[idx];
    }

    void SetCapacity(size_t new_capacity)
    {
        if (new_capacity == capacity)
            return;

        if (len > new_capacity) {
            for (size_t i = new_capacity; i < len; i++) {
                ptr[i].~T();
            }
            len = new_capacity;
        }
        Allocator::Resize(allocator, (void **)&ptr, capacity * sizeof(T), new_capacity * sizeof(T));
        capacity = new_capacity;
    }

    void Reserve(size_t min_capacity)
    {
        if (min_capacity <= capacity)
            return;

        SetCapacity(min_capacity);
    }

    void Grow(size_t reserve_capacity = 1)
    {
        if (reserve_capacity <= capacity - len) {
            return;
        }

        size_t needed_capacity;
#if !defined(NDEBUG)
        DebugAssert(!AddOverflow(capacity, reserve_capacity, &needed_capacity));
#else
        needed_capacity = capacity + reserve_capacity;
#endif

        size_t new_capacity;
        if (!capacity) {
            new_capacity = DYNAMICARRAY_BASE_CAPACITY;
        } else {
            new_capacity = capacity;
        }
        do {
            new_capacity = (size_t)(new_capacity * DYNAMICARRAY_GROWTH_FACTOR);
        } while (new_capacity < needed_capacity);

        SetCapacity(new_capacity);
    }

    void Trim() { SetCapacity(len); }

    T *Append()
    {
        if (len == capacity) {
            Grow();
        }

        T *first = ptr + len;
        new (ptr + len) T;
        len++;
        return first;
    }
    T *Append(const T &value)
    {
        if (len == capacity) {
            Grow();
        }

        T *first = ptr + len;
        new (ptr + len) T;
        ptr[len++] = value;
        return first;
    }
    T *Append(ArrayRef<const T> values)
    {
        if (values.len > capacity - len) {
            Grow(values.len);
        }

        T *first = ptr + len;
        for (const T &value: values) {
            new (ptr + len) T;
            ptr[len++] = value;
        }
        return first;
    }

    void RemoveFrom(size_t from)
    {
        if (from >= len)
            return;

        for (size_t i = from; i < len; i++) {
            ptr[i].~T();
        }
        len = from;
    }
    void RemoveLast(size_t count = 1) { RemoveFrom(len - count); }

    ArrayRef<T> Take(size_t offset, size_t len) const
        { return ArrayRef<T>(ptr, this->len).Take(offset, len); }
    ArrayRef<T> Take(ArraySlice<T> slice) const
        { return ArrayRef<T>(ptr, this->len).Take(slice); }
};

template <typename T, size_t BucketSize = 1024>
class DynamicQueue {
public:
    struct Bucket {
        T *values;
        Allocator allocator;
    };

    class Iterator {
    public:
        class DynamicQueue<T, BucketSize> *queue;
        size_t idx;

        Iterator(DynamicQueue<T, BucketSize> *queue, size_t idx)
            : queue(queue), idx(idx) {}

        T *operator->() { return &(*queue)[idx]; }
        const T *operator->() const { return &(*queue)[idx]; }
        T &operator*() { return (*queue)[idx]; }
        const T &operator*() const { return (*queue)[idx]; }

        const Iterator &operator++()
        {
            idx++;
            return *this;
        }
        Iterator operator++(int)
        {
            Iterator ret = *this;
            idx++;
            return ret;
        }

        bool operator==(const Iterator &other) const
            { return queue == other.queue && idx == other.idx; }
        bool operator!=(const Iterator &other) const { return !(*this == other); }
    };

    HeapArray<Bucket *> buckets;
    size_t offset = 0;
    size_t len = 0;
    Allocator *bucket_allocator;

    typedef T value_type;
    typedef Iterator iterator_type;

    DynamicQueue()
    {
        Bucket *first_bucket = *buckets.Append(CreateBucket());
        bucket_allocator = &first_bucket->allocator;
    }

    DynamicQueue(DynamicQueue &) = delete;
    DynamicQueue &operator=(const DynamicQueue &) = delete;

    ~DynamicQueue()
    {
        for (Bucket *bucket: buckets) {
            DeleteBucket(bucket);
        }
    }

    void Clear()
    {
        for (Bucket *bucket: buckets) {
            DeleteBucket(bucket);
        }
        buckets.Clear();

        Bucket *first_bucket = *buckets.Append(CreateBucket());
        offset = 0;
        len = 0;
        bucket_allocator = &first_bucket->allocator;
    }

    Iterator begin() { return Iterator(this, 0); }
    const Iterator begin() const { return Iterator(this, 0); }
    Iterator end() { return Iterator(this, len); }
    const Iterator end() const { return Iterator(this, len); }

    const T &operator[](size_t idx) const
    {
        DebugAssert(idx < len);

        idx += offset;
        size_t bucket_idx = idx / BucketSize;
        size_t bucket_offset = idx % BucketSize;

        return buckets[bucket_idx]->values[bucket_offset];
    }
    T &operator[](size_t idx) { return (T &)(*(const DynamicQueue *)this)[idx]; }

    T *Append(const T &value)
    {
        size_t bucket_idx = (offset + len) / BucketSize;
        size_t bucket_offset = (offset + len) % BucketSize;

        T *first = buckets[bucket_idx]->values + bucket_offset;
        new (first) T;
        *first = value;

        len++;
        if (bucket_offset == BucketSize - 1) {
            Bucket *new_bucket = *buckets.Append(CreateBucket());
            bucket_allocator = &new_bucket->allocator;
        }

        return first;
    }

    void RemoveFrom(size_t from)
    {
        if (from >= len)
            return;
        if (!from) {
            Clear();
            return;
        }

        size_t start_idx = offset + from;
        size_t end_idx = offset + len;
        size_t start_bucket_idx = start_idx / BucketSize;
        size_t end_bucket_idx = end_idx / BucketSize;

        {
            Iterator end_it = end();
            for (Iterator it(this, from); it != end_it; it++) {
                it->~T();
            }
        }

        for (size_t i = start_bucket_idx + 1; i <= end_bucket_idx; i++) {
            DeleteBucket(buckets[i]);
        }
        buckets.RemoveFrom(start_bucket_idx + 1);
        if (start_idx % BucketSize == 0) {
            DeleteBucket(buckets[buckets.len - 1]);
            buckets[buckets.len - 1] = CreateBucket();
        }

        len = from;
        bucket_allocator = &buckets[buckets.len - 1]->allocator;
    }
    void RemoveLast(size_t count = 1) { RemoveFrom(len - count); }

    void RemoveFirst(size_t count = 1)
    {
        if (count >= len) {
            Clear();
            return;
        }

        size_t end_idx = offset + count;
        size_t end_bucket_idx = end_idx / BucketSize;

        {
            Iterator end_it(this, count);
            for (Iterator it = begin(); it != end_it; it++) {
                it->~T();
            }
        }

        if (end_bucket_idx) {
            for (size_t i = 0; i < end_bucket_idx; i++) {
                DeleteBucket(buckets[i]);
            }
            memmove(&buckets[0], &buckets[end_bucket_idx],
                    (buckets.len - end_bucket_idx) * sizeof(Bucket *));
            buckets.RemoveLast(end_bucket_idx);
        }

        offset = (offset + count) % BucketSize;
        len -= count;
    }

private:
    Bucket *CreateBucket()
    {
        Bucket *new_bucket = (Bucket *)Allocator::Allocate(buckets.allocator, sizeof(Bucket));
        new (&new_bucket->allocator) Allocator;
        new_bucket->values = (T *)Allocator::Allocate(&new_bucket->allocator, BucketSize * sizeof(T));
        return new_bucket;
    }

    void DeleteBucket(Bucket *bucket)
    {
        bucket->allocator.~Allocator();
        Allocator::Release(buckets.allocator, bucket, sizeof(Bucket));
    }
};

template <typename KeyType, typename ValueType,
          typename Handler = typename std::remove_pointer<ValueType>::type::HashHandler>
class HashSet {
public:
    ValueType *data = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    Allocator *allocator = nullptr;

    HashSet() = default;
    HashSet(HashSet &) = delete;
    HashSet &operator=(const HashSet &) = delete;
    ~HashSet() { Clear(); }

    void Clear()
    {
        count = 0;
        Rehash(0);
    }

    ValueType *Find(const KeyType &key)
        { return (ValueType *)((const HashSet *)this)->Find(key); }
    const ValueType *Find(const KeyType &key) const
    {
        if (!capacity)
            return nullptr;

        uint64_t hash = Handler::HashKey(key);
        size_t idx = hash & (capacity - 1);
        return Find(idx, key);
    }
    ValueType FindValue(const KeyType &key, const ValueType &default_value)
        { return (ValueType)((const HashSet *)this)->FindValue(key, default_value); }
    const ValueType FindValue(const KeyType &key, const ValueType &default_value) const
    {
        const ValueType *it = Find(key);
        return it ? *it : default_value;
    }

    ValueType *Append(const ValueType &value)
    {
        const KeyType &key = Handler::GetKey(value);
        uint64_t hash = Handler::HashKey(key);

        if (capacity) {
            size_t idx = HashToIndex(hash);
            if (!Find(idx, key)) {
                if (count >= (size_t)(capacity * HASHSET_MAX_LOAD_FACTOR)) {
                    Rehash(capacity << 1);
                    idx = HashToIndex(hash);
                }
                return Insert(idx, value);
            } else {
                return nullptr;
            }
        } else {
            Rehash(HASHSET_BASE_CAPACITY);

            size_t idx = HashToIndex(hash);
            return Insert(idx, value);
        }
    }

    ValueType *Set(const ValueType &value)
    {
        const KeyType &key = Handler::GetKey(value);
        uint64_t hash = Handler::HashKey(key);

        if (capacity) {
            size_t idx = HashToIndex(hash);
            ValueType *it = Find(idx, key);
            if (!it) {
                if (count >= (size_t)(capacity * HASHSET_MAX_LOAD_FACTOR)) {
                    Rehash(capacity << 1);
                    idx = HashToIndex(hash);
                }
                return Insert(idx, value);
            } else {
                *it = value;
                return it;
            }
        } else {
            Rehash(HASHSET_BASE_CAPACITY);

            size_t idx = HashToIndex(hash);
            return Insert(idx, value);
        }
    }

    void Remove(ValueType *it)
    {
        if (!it)
            return;
        DebugAssert(!Handler::IsEmpty(*it));

        it->~ValueType();

        size_t idx = it - data;
        do {
            size_t next_idx = (idx + 1) & (capacity - 1);
            if (KeyToIndex(Handler::GetKey(data[next_idx])) >= idx)
                break;
            data[idx] = data[next_idx];

            idx = next_idx;
        } while (!Handler::IsEmpty(data[idx]));
    }
    void Remove(const KeyType &key) { Remove(Find(key)); }

private:
    ValueType *Find(size_t idx, const KeyType &key)
        { return (ValueType *)((const HashSet *)this)->Find(idx, key); }
    const ValueType *Find(size_t idx, const KeyType &key) const
    {
        while (!Handler::IsEmpty(data[idx])) {
            const KeyType &it_key = Handler::GetKey(data[idx]);
            if (Handler::CompareKeys(it_key, key))
                return &data[idx];
            idx = (idx + 1) & (capacity - 1);
        }
        return nullptr;
    }

    ValueType *Insert(size_t idx, const ValueType &value)
    {
        DebugAssert(!Handler::IsEmpty(value));

        while (!Handler::IsEmpty(data[idx])) {
            idx = (idx + 1) & (capacity - 1);
        }
        data[idx] = value;
        count++;

        return &data[idx];
    }

    void Rehash(size_t new_capacity)
    {
        if (new_capacity == capacity)
            return;
        DebugAssert(count <= new_capacity);

        ValueType *old_data = data;
        size_t old_capacity = capacity;

        if (new_capacity) {
            // We need to zero memory for POD values, we could avoid it in other
            // cases but I'll wait for widespred constexpr if (C++17) support
            data = (ValueType *)Allocator::Allocate(allocator, new_capacity * sizeof(ValueType),
                                                    Allocator::ZeroMemory);
            for (size_t i = 0; i < new_capacity; i++) {
                new (&data[i]) ValueType;
            }
            capacity = new_capacity;

            count = 0;
            for (size_t i = 0; i < old_capacity; i++) {
                if (!Handler::IsEmpty(old_data[i])) {
                    size_t new_idx = KeyToIndex(Handler::GetKey(old_data[i]));
                    Insert(new_idx, old_data[i]);
                    old_data[i].~ValueType();
                }
            }
        } else {
            data = nullptr;
            capacity = 0;

            count = 0;
            for (size_t i = 0; i < old_capacity; i++) {
                if (!Handler::IsEmpty(old_data[i])) {
                    old_data[i].~ValueType();
                }
            }
        }

        Allocator::Release(allocator, old_data, old_capacity * sizeof(ValueType));
    }

    size_t HashToIndex(uint64_t hash) const
    {
        return hash & (capacity - 1);
    }
    size_t KeyToIndex(const KeyType &key) const
    {
        uint64_t hash = Handler::HashKey(key);
        return HashToIndex(hash);
    }
};

static inline uint64_t DefaultHash(char key) { return (uint64_t)key; }
static inline uint64_t DefaultHash(unsigned char key) { return key; }
static inline uint64_t DefaultHash(short key) { return (uint64_t)key; }
static inline uint64_t DefaultHash(unsigned short key) { return key; }
static inline uint64_t DefaultHash(int key) { return (uint64_t)key; }
static inline uint64_t DefaultHash(unsigned int key) { return key; }
static inline uint64_t DefaultHash(long key) { return (uint64_t)key; }
static inline uint64_t DefaultHash(unsigned long key) { return key; }
static inline uint64_t DefaultHash(long long key) { return (uint64_t)key; }
static inline uint64_t DefaultHash(unsigned long long key) { return key; }

// FNV-1a
static inline uint64_t DefaultHash(const char *key)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; key[i]; i++) {
        hash ^= key[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

static inline bool DefaultCompare(char key1, char key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned char key1, unsigned char key2)
    { return key1 == key2; }
static inline bool DefaultCompare(short key1, short key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned short key1, unsigned short key2)
    { return key1 == key2; }
static inline bool DefaultCompare(int key1, int key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned int key1, unsigned int key2)
    { return key1 == key2; }
static inline bool DefaultCompare(long key1, long key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned long key1, unsigned long key2)
    { return key1 == key2; }
static inline bool DefaultCompare(long long key1, long long key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned long long key1, unsigned long long key2)
    { return key1 == key2; }

static inline bool DefaultCompare(const char *key1, const char *key2)
    { return !strcmp(key1, key2); }

#define HASH_SET_HANDLER_EX(ValueType, KeyMember, EmptyKey, HashFunc, CompareFunc) \
    class HashHandler { \
    public: \
        static bool IsEmpty(const ValueType &value) \
            { return value.KeyMember == (EmptyKey); } \
        static bool IsEmpty(const ValueType *value) { return !value; } \
        static const decltype(ValueType::KeyMember) &GetKey(const ValueType &value) \
            { return value.KeyMember; } \
        static const decltype(ValueType::KeyMember) &GetKey(const ValueType *value) \
            { return value->KeyMember; } \
        static uint64_t HashKey(const decltype(ValueType::KeyMember) &key) \
            { return HashFunc(key); } \
        static bool CompareKeys(const decltype(ValueType::KeyMember) &key1, \
                                const decltype(ValueType::KeyMember) &key2) \
            { return CompareFunc((key1), (key2)); } \
    }
#define HASH_SET_HANDLER(ValueType, KeyMember) \
    HASH_SET_HANDLER_EX(ValueType, KeyMember, decltype(ValueType::KeyMember)(), DefaultHash, DefaultCompare)

template <typename KeyType, typename ValueType>
class HashMap {
    struct Bucket {
        ValueType value; // Keep first, see Remove(it) method
        KeyType key;

        HASH_SET_HANDLER(Bucket, key);
    };

public:
    HashSet<KeyType, Bucket> set;
    Allocator *&allocator = set.table.allocator;

    ValueType *Set(const KeyType &key, const ValueType &value)
        { return &set.Set({value, key})->value; }

    void Remove(ValueType *it) { set.Remove((Bucket *)it); }
    void Remove(KeyType key) { Remove(Find(key)); }

    ValueType *Find(const KeyType &key)
        { return (ValueType *)((const HashMap *)this)->Find(key); }
    const ValueType *Find(const KeyType &key) const
    {
        const Bucket *set_it = set.Find(key);
        return set_it ? &set_it->value : nullptr;
    }
    ValueType FindValue(const KeyType key, const ValueType &default_value)
        { return (ValueType)((const HashMap *)this)->FindValue(key, default_value); }
    const ValueType FindValue(const KeyType key, const ValueType &default_value) const
    {
        const ValueType *it = Find(key);
        return it ? *it : default_value;
    }
};

// ------------------------------------------------------------------------
// Date and Time
// ------------------------------------------------------------------------

union Date {
    int32_t value;
    struct {
#ifdef ARCH_LITTLE_ENDIAN
        int8_t day;
        int8_t month;
        int16_t year;
#else
        int16_t year;
        int8_t month;
        int8_t day;
#endif
    } st;

    Date() = default;
    Date(int16_t year, int8_t month, int8_t day)
#ifdef ARCH_LITTLE_ENDIAN
        : st({day, month, year}) { DebugAssert(IsValid()); }
#else
        : st({year, month, day}) { DebugAssert(IsValid()); }
#endif

    static inline bool IsLeapYear(int16_t year)
    {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }
    static inline int8_t DaysInMonth(uint16_t year, uint8_t month)
    {
        static const int8_t DaysPerMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return DaysPerMonth[month - 1] + (month == 2 && IsLeapYear(year));
    }

    static Date FromString(const char *date_str);
    static Date FromJulianDays(int days);

    bool IsValid() const
    {
        if (st.year < -4712)
            return false;
        if (st.month < 1 || st.month > 12)
            return false;
        if (st.day < 1 || st.day > DaysInMonth(st.year, st.month))
            return false;

        return true;
    }

    bool operator==(Date other) const { return value == other.value; }
    bool operator!=(Date other) const { return value != other.value; }
    bool operator>(Date other) const { return value > other.value; }
    bool operator>=(Date other) const { return value >= other.value; }
    bool operator<(Date other) const { return value < other.value; }
    bool operator<=(Date other) const { return value <= other.value; }

    int ToJulianDays() const;

    int operator-(Date other) const
        { return ToJulianDays() - other.ToJulianDays(); }

    Date operator+(int days) const
    {
        if (days < 5 && days > -5) {
            Date date = *this;
            if (days > 0) {
                while (days--) {
                    ++date;
                }
            } else {
                while (days++) {
                    --date;
                }
            }
            return date;
        } else {
            return Date::FromJulianDays(ToJulianDays() + days);
        }
    }
    // That'll fail with INT_MAX days but that's far more days than can
    // be represented as a date anyway
    Date operator-(int days) const { return *this + (-days); }

    Date &operator+=(int days) { *this = *this + days; return *this; }
    Date &operator-=(int days) { *this = *this - days; return *this; }
    Date &operator++();
    Date operator++(int) { Date date = *this; ++(*this); return date; }
    Date &operator--();
    Date operator--(int) { Date date = *this; --(*this); return date; }
};

uint64_t GetMonotonicTime();

// ------------------------------------------------------------------------
// Strings
// ------------------------------------------------------------------------

char *MakeString(Allocator *alloc, ArrayRef<const char> bytes);
char *DuplicateString(Allocator *alloc, const char *str, size_t max_len = SIZE_MAX);

static inline bool IsAsciiAlpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static inline bool IsAsciiDigit(char c)
{
    return (c >= '0' && c <= '9');
}
static inline bool IsAsciiAlphaOrDigit(char c)
{
    return IsAsciiAlpha(c) || IsAsciiDigit(c);
}

static inline char UpperAscii(char c)
{
    if (c >= 'a' && c <= 'z') {
        return c - 32;
    } else {
        return c;
    }
}
static inline char LowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + 32;
    } else {
        return c;
    }
}

class FmtArg {
public:
    enum class Type {
        StrRef,
        StrBuf,
        Char,
        Bool,
        Integer,
        Unsigned,
        Double,
        Binary,
        Hexadecimal,
        MemorySize,
        DiskSize,
        Date,
        List
    };

    Type type;
    union {
        ArrayRef<const char> str_ref;
        char str_buf[12];
        char ch;
        bool b;
        int64_t i;
        uint64_t u;
        struct {
            double value;
            int precision;
        } d;
        const void *ptr;
        size_t size;
        Date date;
        struct {
            ArrayRef<FmtArg> args;
            const char *separator;
        } list;
    } value;
    int repeat = 1;

    FmtArg() = default;
    FmtArg(const char *str) : type(Type::StrRef) { value.str_ref = MakeStrRef(str ? str : "(null)"); }
    FmtArg(ArrayRef<const char> str) : type(Type::StrRef) { value.str_ref = str; }
    FmtArg(char c) : type(Type::Char) { value.ch = c; }
    FmtArg(bool b) : type(Type::Bool) { value.b = b; }
    FmtArg(unsigned char u)  : type(Type::Unsigned) { value.u = u; }
    FmtArg(short i) : type(Type::Integer) { value.i = i; }
    FmtArg(unsigned short u) : type(Type::Unsigned) { value.u = u; }
    FmtArg(int i) : type(Type::Integer) { value.i = i; }
    FmtArg(unsigned int u) : type(Type::Unsigned) { value.u = u; }
    FmtArg(long i) : type(Type::Integer) { value.i = i; }
    FmtArg(unsigned long u) : type(Type::Unsigned) { value.u = u; }
    FmtArg(long long i) : type(Type::Integer) { value.i = i; }
    FmtArg(unsigned long long u) : type(Type::Unsigned) { value.u = u; }
    FmtArg(double d) : type(Type::Double) { value.d = { d, -1 }; }
    FmtArg(const void *ptr) : type(Type::Hexadecimal) { value.u = (uint64_t)ptr; }
    FmtArg(const Date &date) : type(Type::Date) { value.date = date; }

    FmtArg &Repeat(int new_repeat) { repeat = new_repeat; return *this; }
};

static inline FmtArg FmtBin(uint64_t u)
{
    FmtArg arg;
    arg.type = FmtArg::Type::Binary;
    arg.value.u = u;
    return arg;
}
static inline FmtArg FmtHex(uint64_t u)
{
    FmtArg arg;
    arg.type = FmtArg::Type::Hexadecimal;
    arg.value.u = u;
    return arg;
}

static inline FmtArg FmtDouble(double d, int precision = -1)
{
    FmtArg arg;
    arg.type = FmtArg::Type::Double;
    arg.value.d.value = d;
    arg.value.d.precision = precision;
    return arg;
}

static inline FmtArg FmtMemSize(size_t size)
{
    FmtArg arg;
    arg.type = FmtArg::Type::MemorySize;
    arg.value.size = size;
    return arg;
}
static inline FmtArg FmtDiskSize(size_t size)
{
    FmtArg arg;
    arg.type = FmtArg::Type::DiskSize;
    arg.value.size = size;
    return arg;
}
static inline FmtArg FmtList(ArrayRef<FmtArg> args, const char *sep = ", ")
{
    FmtArg arg;
    arg.type = FmtArg::Type::List;
    arg.value.list.args = args;
    arg.value.list.separator = sep;
    return arg;
}

enum class LogLevel {
    Debug,
    Info,
    Error
};

size_t FmtString(ArrayRef<char> buf, const char *fmt,
                 ArrayRef<const FmtArg> args);
char *FmtString(Allocator *alloc, const char *fmt, ArrayRef<const FmtArg> args);
void FmtPrint(FILE *fp, const char *fmt, ArrayRef<const FmtArg> args);
void FmtLog(LogLevel level, const char *ctx, const char *fmt,
            ArrayRef<const FmtArg> args);

// Print formatted strings to fixed-size buffer
static inline size_t Fmt(ArrayRef<char> buf, const char *fmt)
{
    return FmtString(buf, fmt, {});
}
template <typename... Args>
static inline size_t Fmt(ArrayRef<char> buf, const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    return FmtString(buf, fmt, fmt_args);
}

// Print formatted strings to dynamic char array
static inline char *Fmt(Allocator *alloc, const char *fmt)
{
    return FmtString(alloc, fmt, {});
}
template <typename... Args>
static inline char *Fmt(Allocator *alloc, const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    return FmtString(alloc, fmt, fmt_args);
}

// Print formatted strings to stdio FILE
static inline  void Print(FILE *fp, const char *fmt)
{
    FmtPrint(fp, fmt, {});
}
template <typename... Args>
static inline void Print(FILE *fp, const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    FmtPrint(fp, fmt, fmt_args);
}

// Print formatted strings to stdout
static inline void Print(const char *fmt)
{
    FmtPrint(stdout, fmt, {});
}
template <typename... Args>
static inline void Print(const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    FmtPrint(stdout, fmt, fmt_args);
}

// Variants of the Print() functions with terminating newline
template <typename... Args>
static inline void PrintLn(FILE *fp, const char *fmt, Args... args)
{
    Print(fp, fmt, args...);
    putc('\n', fp);
}
template <typename... Args>
static inline void PrintLn(const char *fmt, Args... args)
{
    Print(stdout, fmt, args...);
    putchar('\n');
}
static inline void PrintLn()
{
    putchar('\n');
}

// ------------------------------------------------------------------------
// Debug and errors
// ------------------------------------------------------------------------

// Log text line to stderr with context, for the Log() macros below
static inline void Log(LogLevel level, const char *ctx, const char *fmt)
{
    FmtLog(level, ctx, fmt, {});
}
template <typename... Args>
static inline void Log(LogLevel level, const char *ctx, const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    FmtLog(level, ctx, fmt, fmt_args);
}

static inline constexpr const char *SimplifyLogContext(const char *ctx)
{
    const char *new_ctx = ctx;
    for(; *ctx; ctx++) {
        if (*ctx == '/' || *ctx == '\\') {
            new_ctx = ctx + 1;
        }
    }
    return new_ctx;
}

#define LOG_LOCATION SimplifyLogContext(__FILE__ ":" STRINGIFY(__LINE__))
#define LogDebug(...) Log(LogLevel::Debug, LOG_LOCATION, __VA_ARGS__)
#define LogInfo(...) Log(LogLevel::Info, LOG_LOCATION, __VA_ARGS__)
#define LogError(...) Log(LogLevel::Error, LOG_LOCATION, __VA_ARGS__)

void PushLogHandler(std::function<void(FILE *)> handler);
void PopLogHandler();

// ------------------------------------------------------------------------
// System
// ------------------------------------------------------------------------

#ifdef _WIN32
    #define PATH_SEPARATORS "\\/"
    #define FOPEN_COMMON_FLAGS
#else
    #define PATH_SEPARATORS "/"
    #define FOPEN_COMMON_FLAGS "e"
#endif

bool ReadFile(Allocator *alloc, const char *filename, size_t max_size,
              ArrayRef<uint8_t> *out_data);
static inline bool ReadFile(Allocator *alloc, const char *filename, size_t max_size,
                            uint8_t **out_data, size_t *out_len)
{
    ArrayRef<uint8_t> data;
    if (!ReadFile(alloc, filename, max_size, &data))
        return false;
    *out_data = data.ptr;
    *out_len = data.len;
    return true;
}

enum class FileType {
    Directory,
    File,
    Special
};

struct FileInfo {
    FileType type;
};

enum class EnumStatus {
    Error,
    Partial,
    Done
};

EnumStatus EnumerateDirectory(const char *dirname, const char *filter,
                              std::function<bool(const char *, const FileInfo &)> func);
bool EnumerateDirectoryFiles(const char *dirname, const char *filter, Allocator &str_alloc,
                             HeapArray<const char *> *out_files, size_t max_files);

// ------------------------------------------------------------------------
// Option Parser
// ------------------------------------------------------------------------

class OptionParser {
    size_t limit;
    size_t smallopt_offset = 0;
    char buf[80];

public:
    ArrayRef<const char *> args;
    size_t pos = 0;

    const char *current_option = nullptr;
    const char *current_value = nullptr;

    OptionParser(ArrayRef<const char *> args)
        : limit(args.len), args(args) {}
    OptionParser(int argc, char **argv)
        : limit(argc > 0 ? argc - 1 : 0),
          args(limit ? (const char **)(argv + 1) : nullptr, limit) {}

    const char *ConsumeOption();
    const char *ConsumeOptionValue();
    const char *ConsumeNonOption();
    void ConsumeNonOptions(HeapArray<const char *> *non_options);

    const char *RequireOptionValue(const char *usage_str = nullptr);
};

static inline bool TestOption(const char *opt, const char *test1, const char *test2 = nullptr)
{
    return !strcmp(opt, test1) ||
           (test2 && !strcmp(opt, test2));
}
