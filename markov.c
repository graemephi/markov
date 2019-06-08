#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>

// Warning: uses asserts for error handling

// Todo:
// - It turns out the way windows are implemented--"state aliasing" I guess you
//   could call it--can be done independently during analysis or text
//   generation. So there ought to be separate window parameters.
// - Composable markov chains:
//
//       MarkovChain a, b, c; ...; c = compose(&a, &b);
//
//   The only impedement to this efficient rejection sampling. Possible
//   solution: parallel array of generation markers per state in 'leaf' chains,
//   reject tokens by setting their generation to the current generation,
//   increment generation for each generated state.

#define Config_DumpBucketStats 0
#define Config_DetectHashCollisions 0

#if defined(_MSC_VER)
#pragma warning(disable : 4146) // unary minus operator applied to unsigned type, result still unsigned
#pragma warning(disable : 4201) // nonstandard extension used: nameless struct/union
#pragma warning(disable : 4204) // nonstandard extension used: non-constant aggregate initializer
#pragma warning(disable : 4221) // nonstandard extension used: cannot be initialized using address of automatic variable
#endif

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-braces"
#define static_assert _Static_assert
#endif

#define alignof _Alignof

typedef uint8_t u8;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t i32;
typedef int64_t i64;

typedef i32 b32;

typedef size_t usize;
typedef intptr_t isize;

typedef float f32;
typedef double f64;

#define assert_implies(p, q) assert(!(p) || (q))
#define assert_unreachable() assert(!"Unreachable")
#define compile_time_assert(x) static_assert(x, #x)

#if defined(Config_DetectHashCollisions) && Config_DetectHashCollisions
#define assert_no_collision(a, b, length) assert(memcmp(a, b, length) == 0)
#else
#define assert_no_collision(...)
#endif

#if defined(Config_DumpBucketStats) && Config_DumpBucketStats
#define dump_buckets(...) bucket_print_stats(__VA_ARGS__)
#else
#define dump_buckets(...)
#endif

#define KB(x) (x*1024ll)
#define MB(x) (KB(x)*1024ll)
#define GB(x) (MB(x)*1024ll)

#define array_length(arr) (isize)((sizeof(arr) / sizeof((arr)[0])))

#undef min
#undef max

#if defined(_MSC_VER)

b32 QueryPerformanceCounter(i64 *lpPerformanceCount);
b32 QueryPerformanceFrequency(i64 *lpFrequency);

i64 tick_frequency = 0;

i64 ticks()
{
    i64 result;
    QueryPerformanceCounter(&result);
    return result;
}

f64 seconds_between(i64 a, i64 b)
{
    if (tick_frequency == 0) {
        QueryPerformanceFrequency(&tick_frequency);
    }
    return (f64)(b - a) / (f64)tick_frequency;
}

void *CreateThread(void *lpThreadAttributes, usize dwStackSize, i32 (*lpStartAddress)(void *), void *lpParameter, u32 dwCreationFlags, u32 *lpThreadId);
i32 WaitForMultipleObjects(u32 nCount, const void **lpHandles, b32 bWaitAll, i32 dwMilliseconds);

typedef struct Win32_SYSTEM_INFO {
  union {
    u32 dwOemId;
    struct {
      u16 wProcessorArchitecture;
      u16 wReserved;
    } DUMMYSTRUCTNAME;
  } DUMMYUNIONNAME;
  u32     dwPageSize;
  void *lpMinimumApplicationAddress;
  void *lpMaximumApplicationAddress;
  u32 *dwActiveProcessorMask;
  u32     dwNumberOfProcessors;
  u32     dwProcessorType;
  u32     dwAllocationGranularity;
  u16      wProcessorLevel;
  u16      wProcessorRevision;
} Win32_SYSTEM_INFO, *Win32_LPSYSTEM_INFO;
void GetSystemInfo(Win32_LPSYSTEM_INFO lpSystemInfo);

void *create_thread(i32 (*fn)(void *), void *data)
{
    return CreateThread(0, MB(4), fn, data, 0, 0);
}

void wait_for_threads(void **threads, isize n_threads)
{
    WaitForMultipleObjects((u32)n_threads, threads, true, 0xffffffffL);
}

i32 core_count()
{
    Win32_SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}


void *VirtualAlloc(void *lpAddress, usize dwSize, u32 flAllocationType, u32 flProtect);

// Overcommit on windows.
void *system_alloc(isize size)
{
    return VirtualAlloc(0, (usize)size, 0x00001000|0x00002000, 0x04);
}

#define thread_local __declspec(thread)

#else

#define LINE1(x) #x
#define LINE2(x) LINE1(x)
#pragma message("markov.c:" LINE2(__LINE__) ": Not win32. Using stubbed out thread and timer functions")
i64 ticks() { return 0; }
f64 seconds_between(i64 a, i64 b) { (void)a; (void)b; return 0.0; }
void *create_thread(i32 (*fn)(void *), void *data) { fn(data); return 0; }
void wait_for_threads(void *threads, isize n_threads) { (void)threads; (void)n_threads; }
void *system_alloc(isize size) { return malloc((usize)size); }
i32 core_count() { return 1; }
#define thread_local

#endif

f64 to_mb(isize x)
{
    return (f64)x * (1. / 1024. / 1024.);
}

b32 always(b32 condition) {
    assert(condition);
    return condition;
}

b32 never(b32 condition) {
    assert(!condition);
    return condition;
}

void *xmalloc(isize size)
{
    void *result = malloc(size);
    if (result == 0) {
        printf("fatal: malloc failure");
        exit(1);
    }
    return result;
}

void *xcalloc(isize elem_size, isize count)
{
    void *result = calloc(elem_size, count);
    if (result == 0) {
        printf("fatal: calloc failure");
        exit(1);
    }
    return result;
}

void *xrealloc(void *ptr, isize size)
{
    void *result = realloc(ptr, size);
    if (result == 0) {
        printf("fatal: realloc failure");
        exit(1);
    }
    return result;
}

#define max(a, b) (((a) > (b) ? (a) : (b)))
#define min(a, b) (((a) < (b) ? (a) : (b)))
#define clamp_min(a, b) max(a, b)
#define clamp_max(a, b) min(a, b)

b32 multiply_is_safe_32(i32 a, i32 b)
{
    return (INT_MAX / b) > a;
}

b32 multiply_is_safe_ptr(isize a, isize b)
{
    return (INTPTR_MAX / b) > a;
}

b32 is_power_of_two_or_zero(isize x)
{
    return (x & (x - 1)) == 0;
}

b32 is_power_of_two(isize x)
{
    return x != 0 && is_power_of_two_or_zero(x);
}

i32 log2i(isize x)
{
    i32 result = 0;
    while (x) {
        x >>= 1;
        result++;
    }
    return result - 1;
}

isize round_up_to_power_of_two(isize x)
{
    if (is_power_of_two_or_zero(x)) {
        return clamp_min(1, x);
    }

    return 2ll << log2i(x);
}

typedef struct RandomState {
    u64 state;
    u64 inc;
} RandomState;

RandomState random_state = { .state = 0xC0B0L, .inc = 1 };

u32 random_pcg(RandomState * rng)
{
    // *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
    // Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)
    u64 oldstate = rng->state;
    // Advance internal state
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
    // Calculate output function (XSH RR), uses old state for max ILP
    u32 xorshifted = (u32)(((oldstate >> 18u) ^ oldstate) >> 27u);
    u32 rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

u32 randomr(RandomState *state, u32 range)
{
    // http://www.pcg-random.org/posts/bounded-rands.html, lemire
    u32 t = (-range) % range;
    u64 m;
    do {
        u32 x = random_pcg(state);
        m = (u64)x * (u64)range;
    } while ((u32)m < t);
    return m >> 32;
}

u64 random_pcg64(RandomState *state)
{
    u64 result = random_pcg(state);
    result = (result << 32) + random_pcg(state);
    return result;
}

u64 randomr64(RandomState *state, u64 range)
{
    u64 mask = round_up_to_power_of_two(range) - 1;
    u64 result = 0;
    do {
        result = random_pcg64(state) & mask;
    } while (result >= range);
    return result;
}

u32 randomi(RandomState *state, u32 lo, u32 hi)
{
    return lo + randomr(state, hi - lo);
}

typedef union f64bits
{
    f64 f;
    u64 u;
} f64bits;

f64 random01d(RandomState *state)
{
    u64 bits = random_pcg64(state);
    f64bits on = { .f = 1. };
    f64bits off = { .u = on.u - 1 };
    f64bits f = { .u = (bits & off.u) | on.u };
    return f.f - 1.;
}

void test_random()
{
    i32 counts[768] = {0};
    i32 n = (1 << 28);
    i64 ticks_a = ticks();
    for (isize i = 0; i < n; i++) {
        i32 k = (i32)randomr64(&random_state, array_length(counts));
        assert(k >= 0 && k < array_length(counts));
        counts[k]++;
    }
    i64 ticks_b = ticks();

    f64 avg = 0.;
    f64 var = 0.;
    for (isize i = 0; i < array_length(counts); i++) {
        assert(counts[i]);
        avg += (f64)counts[i] / (f64)n;
    }
    for (isize i = 0; i < array_length(counts); i++) {
        f64 d = ((f64)counts[i] / (f64)n) - avg;
        var += d*d;
    }
    f64 dev = sqrt(var);

    printf("time %2.3fs\navg %2.3f\nvar %2.3f\ndev %2.3f\n", seconds_between(ticks_a, ticks_b), avg, var, dev);
}

u64 hash64(u64 x)
{
    // http://xoshiro.di.unimi.it/splitmix64.c
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
	return x ^ (x >> 31);
}

#define FNV_offset_basis 0xcbf29ce484222325ull
#define FNV_prime 1099511628211ull
compile_time_assert(sizeof(isize) == sizeof(i64));

u64 hash_fnv(u8 *data, isize count)
{
    u8 *bytes = data;
    u64 hash = FNV_offset_basis;
    for (isize i = 0; i < count; i++) {
        hash ^= bytes[i];
        hash *= FNV_prime;
    }
    // Apparently, there are a few byte sequences for which the FNV hash returns
    // 0. That would be a cue to use something else.
    assert(hash);
    return hash;
}

typedef struct Stack
{
    u8 *base;
    u8 *ptr;
    u8 *end;
} Stack;

thread_local
Stack scratch = {0};

#define stack_alloc(stack, type, count) (type *)stack_alloc_(stack, sizeof(type), alignof(type), count)
void *stack_alloc_(Stack *stack, isize elem_size, isize alignment, isize count)
{
    assert(stack->base);
    assert(multiply_is_safe_ptr(elem_size, count));
    assert(is_power_of_two(alignment));

    isize size = elem_size * count;
    isize align = (alignment - ((usize)stack->ptr & (alignment - 1))) & (alignment - 1);
    size += align;

    isize len = stack->ptr - stack->base;
    isize cap = stack->end - stack->base;

    if (never((len + size) > cap)) {
        return 0;
    }

    u8 *result = stack->ptr + align;
    stack->ptr += size;
    assert(((usize)result & (alignment - 1)) == 0);
    return result;
}

#define stack_alloc_down(stack, type, count) (type *)stack_alloc_down_(stack, sizeof(type), alignof(type), count)
void *stack_alloc_down_(Stack *stack, isize elem_size, isize alignment, isize count)
{
    assert(stack->base);
    assert(multiply_is_safe_ptr(elem_size, count));
    assert(is_power_of_two(alignment));

    isize size = elem_size * count;
    isize align = (usize)stack->end & (alignment - 1);
    stack->end -= align;

    isize cap = stack->end - stack->base;

    if (never(size > cap)) {
        return 0;
    }

    stack->end -= size;
    u8 *result = stack->end;
    assert(((usize)result & (alignment - 1)) == 0);
    return result;
}

#define stack_alloc_zero(stack, type, count) (type *)stack_alloc_zero_(stack, sizeof(type), alignof(type), count)
void *stack_alloc_zero_(Stack *stack, isize elem_size, isize alignment, isize count)
{
    void *result = stack_alloc_(stack, elem_size, alignment, count);
    memset(result, 0, elem_size * count);
    return result;
}


#define stack_push(stack, type, ...) (type *)stack_push_(stack, sizeof(type), alignof(type), (type[1]){__VA_ARGS__})
void *stack_push_(Stack *stack, isize elem_size, isize alignment, void *elem)
{
    void *result = stack_alloc_(stack, elem_size, alignment, 1);
    memmove(result, elem, elem_size);
    return result;
}

void clear_stack(Stack *stack)
{
    stack->ptr = stack->base;
}

Stack push_stack(Stack *stack)
{
    Stack child_stack = (Stack) { stack->ptr, stack->ptr, stack->end };
    stack->ptr = stack->end;
    return child_stack;
}

Stack push_stack_size(Stack *stack, isize size)
{
    if (never((stack->ptr + size) > stack->end)) {
        return (Stack) {0};
    }

    Stack result = { stack->ptr, stack->ptr, stack->ptr + size };
    stack->ptr = result.end;
    return result;
}

void reclaim_stack(Stack *stack, Stack *child_stack)
{
    assert(stack->ptr == stack->end);
    assert(child_stack->base >= stack->base);
    assert(child_stack->end <= stack->ptr);
    stack->ptr = child_stack->ptr;
}

void pop_stack(Stack *stack, Stack *child_stack)
{
    assert(stack->ptr == stack->end);
    assert(child_stack->base >= stack->base);
    assert(child_stack->end <= stack->ptr);
    stack->ptr = child_stack->base;
}

typedef struct File
{
    u8 *data;
    isize size;
} File;

File read_file(Stack *stack, isize size_limit, char const *path)
{
    FILE *f = 0;
    errno_t err = fopen_s(&f, path, "rb");
    assert((err == 0) && "No file!? :(");
    if (err) {
        return (File){0};
    }

    if (size_limit == 0) {
        size_limit = INTPTR_MAX;
    }

    isize chunk_size = clamp_max(MB(4), size_limit);
    File result = (File) { stack->ptr, 0 };

    while (chunk_size) {
        u8 *buf = stack_alloc(stack, u8, chunk_size);
        isize read = fread(buf, 1, chunk_size, f);
        result.size += read;

        if (read < chunk_size) {
            stack->ptr -= (chunk_size - read);
            break;
        }

        chunk_size = min(size_limit - result.size, chunk_size);
    }

    fclose(f);

    stack_alloc(stack, u8, 1);
    result.data[result.size++] = 0;
    return result;
}

typedef struct Bucket Bucket;
typedef struct Bucket
{
    u8 *buffer;
    Bucket *next;
} Bucket;

typedef struct BucketAllocator
{
    Stack allocator;
    Bucket *free_buckets_for_size[32];
    struct { i32 available, allocs, buffer_allocs, splits; } stats[32];
} BucketAllocator;

typedef enum {
    Bucket_Allocation,
    Bucket_BufferAllocated,
    Bucket_BufferTaken,
    Bucket_BufferReturned,
    Bucket_BufferSplit,
    Bucket_BucketAllocated,

    Bucket_InitialSizeIndex = 1,
    Bucket_InitialSize = 1 << Bucket_InitialSizeIndex
} BucketEvent;

void bucket_stats_event(BucketAllocator *buckets, BucketEvent event, isize data)
{
    assert_implies(event != Bucket_BucketAllocated, data >= 0);
    assert_implies(event != Bucket_BucketAllocated, data < 31);

    // Slot 0 is for buckets with no buffers
    // Slot 31 is for all buckets in aggregate
    switch (event) {
        case Bucket_Allocation: {
            isize bucket_index = data;
            buckets->stats[bucket_index].allocs++;
            buckets->stats[31].allocs++;
        } break;
        case Bucket_BufferAllocated: {
            isize bucket_index = data;
            assert(buckets->stats[bucket_index].available == 0);
            buckets->stats[31].buffer_allocs++;
            buckets->stats[bucket_index].buffer_allocs++;
        } break;
        case Bucket_BufferTaken: {
            isize bucket_index = data;
            assert(buckets->stats[bucket_index].available > 0);
            buckets->stats[bucket_index].available--;
            buckets->stats[0].available++;
        } break;
        case Bucket_BufferReturned: {
            isize bucket_index = data;
            assert(buckets->stats[0].available > 0);
            buckets->stats[bucket_index].available++;
            buckets->stats[0].available--;
        } break;
        case Bucket_BufferSplit: {
            isize bucket_index = data;
            assert(buckets->stats[bucket_index].available > 0);
            buckets->stats[bucket_index].available--;
            buckets->stats[0].available++;
            buckets->stats[bucket_index].splits++;
            buckets->stats[31].splits++;
        } break;
        case Bucket_BucketAllocated: {
            buckets->stats[0].available += (i32)data;
            buckets->stats[31].available += (i32)data;
        } break;
        default: assert(!"Invalid BucketEvent");
    }
}

void bucket_print_stats(BucketAllocator *buckets)
{
    isize allocated = buckets->allocator.ptr - buckets->allocator.base;
    isize allocator_size = buckets->allocator.end - buckets->allocator.base;
    isize buckets_size = buckets->stats[31].available * sizeof(Bucket);
    printf("allocated: %2.2fMB / %2.2fMB\n", to_mb(allocated + buckets_size), to_mb(allocator_size + buckets_size));
    printf("bucket overhead: %2.2fKB\n", 1024. * to_mb(buckets->stats[31].available * sizeof(Bucket)));

    isize bytes_in_use = 0;
    isize biggest_alloc = 0;
    for (isize i = Bucket_InitialSizeIndex; i < 31; i++) {
        bytes_in_use += ((buckets->stats[i].buffer_allocs - buckets->stats[i].available) << i);
        if (buckets->stats[i].buffer_allocs != 0) {
            biggest_alloc = i;
        }
    }
    printf("used: %2.2fMB / %2.2fMB (%.3f)\n", to_mb(bytes_in_use), to_mb(allocated), to_mb(bytes_in_use) / to_mb(allocated));
    printf("%3s %12s %13s %12s %12s\n", "idx", "allocs", "buffer allocs", "available", "splits");
    printf("%3d %12d %13d %12d %12d\n", 0, buckets->stats[0].allocs, buckets->stats[0].buffer_allocs, buckets->stats[0].available, buckets->stats[0].splits);
    for (isize i = Bucket_InitialSizeIndex; i <= biggest_alloc; i++) {
        printf("%3lld %12d %13d %12d %12d\n", i, buckets->stats[i].allocs, buckets->stats[i].buffer_allocs, buckets->stats[i].available, buckets->stats[i].splits);
    }
    printf("%3s %12d %13d %12d %12d\n", "all", buckets->stats[31].allocs, buckets->stats[31].buffer_allocs, buckets->stats[31].available, buckets->stats[31].splits);
}

void bucket_return_buffer(BucketAllocator *buckets, void *buffer, isize index)
{
    if (buckets->free_buckets_for_size[0] == 0) {
        buckets->free_buckets_for_size[0] = stack_alloc_down(&buckets->allocator, Bucket, 1);
        buckets->free_buckets_for_size[0]->next = 0;
        bucket_stats_event(buckets, Bucket_BucketAllocated, 1);
    }

    Bucket *added_bucket = buckets->free_buckets_for_size[0];
    buckets->free_buckets_for_size[0] = added_bucket->next;

    added_bucket->buffer = buffer;
    added_bucket->next = buckets->free_buckets_for_size[index];
    buckets->free_buckets_for_size[index] = added_bucket;
    bucket_stats_event(buckets, Bucket_BufferReturned, index);
}

#define bucket_alloc(buckets, type, count) (type *)bucket_alloc_(buckets, sizeof(type), alignof(type), count)
void *bucket_alloc_(BucketAllocator *buckets, isize elem_size, isize alignment, isize elem_count)
{
    assert(multiply_is_safe_ptr(elem_size, elem_count));
    isize size = round_up_to_power_of_two(elem_size * clamp_min(1, elem_count));
    isize next_index = clamp_min(Bucket_InitialSizeIndex, log2i(size));
    Bucket *taken_bucket = buckets->free_buckets_for_size[next_index];

    // Split buffers if there is a larger one available. Fragmentation is already very low
    // from the allocators pov so this is not strictly necessary, but, eh.
    if (taken_bucket == 0) {
        for (isize i = next_index + 1; i < array_length(buckets->free_buckets_for_size); i++) {
            if (buckets->free_buckets_for_size[i] != 0) {
                Bucket *split_bucket = buckets->free_buckets_for_size[i];
                buckets->free_buckets_for_size[i] = split_bucket->next;

                u8 *buffer = split_bucket->buffer;
                isize buffer_size = 1ll << i;
                assert(buffer);

                split_bucket->buffer = 0;
                split_bucket->next = buckets->free_buckets_for_size[0];
                buckets->free_buckets_for_size[0] = split_bucket;
                bucket_stats_event(buckets, Bucket_BufferSplit, i);

                for (isize j = i - 1; j >= next_index; j--) {
                    bucket_return_buffer(buckets, buffer, j);
                    buffer_size >>= 1;
                    assert(buffer_size);
                    buffer += buffer_size;
                }

                bucket_return_buffer(buckets, buffer, next_index);

                taken_bucket = buckets->free_buckets_for_size[next_index];
                assert(taken_bucket && (taken_bucket->buffer == buffer));
                assert(log2i(buffer_size) == next_index);
                break;
            }
        }
    }

    void *result = 0;

    if (taken_bucket) {
        buckets->free_buckets_for_size[next_index] = taken_bucket->next;
        assert(taken_bucket->buffer);

        result = taken_bucket->buffer;
        taken_bucket->buffer = 0;

        taken_bucket->next = buckets->free_buckets_for_size[0];
        buckets->free_buckets_for_size[0] = taken_bucket;

        bucket_stats_event(buckets, Bucket_BufferTaken, next_index);
    } else {
        isize next_size = 1ll << next_index;
        result = stack_alloc_(&buckets->allocator, 1, alignment, next_size);
        assert((buckets->allocator.ptr - (u8*)result) >= (elem_size * elem_count));
        bucket_stats_event(buckets, Bucket_BufferAllocated, next_index);
    }

    bucket_stats_event(buckets, Bucket_Allocation, next_index);
    return result;
}

#define bucket_alloc_zero(buckets, type, count) (type *)bucket_alloc_zero_(buckets, sizeof(type), alignof(type), count)
void *bucket_alloc_zero_(BucketAllocator *buckets, isize elem_size, isize alignment, isize elem_count)
{
    void *result = bucket_alloc_(buckets, elem_size, alignment, elem_count);
    memset(result, 0, elem_size * elem_count);
    return result;
}

#define bucket_realloc(buckets, type, old_buffer, old_count, new_count) (type *)bucket_realloc_(buckets, sizeof(type), alignof(type), old_buffer, old_count, new_count)
void *bucket_realloc_(BucketAllocator *buckets, isize elem_size, isize alignment, void *old_buffer, isize old_count, isize new_count)
{
    assert(multiply_is_safe_ptr(elem_size, new_count));
    assert_implies(old_buffer != 0, buckets->allocator.base <= (u8 *)old_buffer && (u8 *)old_buffer < buckets->allocator.end);
    isize old_size = round_up_to_power_of_two(elem_size * old_count);
    isize new_size = round_up_to_power_of_two(elem_size * new_count);

    if (old_buffer && new_size <= Bucket_InitialSize) {
        return old_buffer;
    }

    if (old_buffer) {
        i32 old_index = log2i(old_size);
        assert(old_index >= Bucket_InitialSizeIndex);
        assert(old_index < 31);
        bucket_return_buffer(buckets, old_buffer, old_index);
    }

    void *result = bucket_alloc_(buckets, elem_size, alignment, new_count);

    if (old_buffer) {
        memmove(result, old_buffer, elem_size * old_count);
    }

    assert(result);
    return result;
}

#define bucket_free(buckets, type, buffer, count) bucket_free_(buckets, sizeof(type), buffer, count)
void bucket_free_(BucketAllocator *buckets, isize elem_size, void *buffer, isize count)
{
    if (buffer) {
        assert(multiply_is_safe_ptr(elem_size, count));
        isize bucket_index = log2i(round_up_to_power_of_two(elem_size * count));
        assert(bucket_index >= Bucket_InitialSizeIndex);
        assert(bucket_index < 31);
        bucket_return_buffer(buckets, buffer, bucket_index);
    }
}

thread_local
BucketAllocator buckets = {0};

typedef struct HashTable
{
    isize occupied;
    isize capacity;
    isize collisions;
    usize *keys;
    u32 *value_indices;
    usize *keys_linear;
    usize *values_linear;
} HashTable;

void table_fit(HashTable *table, isize count)
{
    count = round_up_to_power_of_two(count);
    if (count > table->capacity) {
        table->keys_linear = bucket_realloc(&buckets, usize, table->keys_linear, table->capacity, count);
        table->values_linear = bucket_realloc(&buckets, usize, table->values_linear, table->capacity, count);
        bucket_free(&buckets, usize, table->keys, table->capacity);
        bucket_free(&buckets, u32, table->value_indices, table->capacity);
        table->keys = bucket_alloc_zero(&buckets, usize, count);
        table->value_indices = bucket_alloc_zero(&buckets, u32, count);
        table->capacity = count;
        table->collisions = 0;

        usize mask = count - 1;
        for (isize i = 0; i < table->occupied; i++) {
            usize hash = hash64(table->keys_linear[i]);
            usize index = hash & mask;
            while (table->keys[index] != 0) {
                assert(table->keys[index] != table->keys_linear[i]);
                index = (index + 1) & mask;
            }
            table->keys[index] = table->keys_linear[i];
            table->value_indices[index] = (u32)i;

            if ((hash & mask) != index) {
                table->collisions++;
            }
        }
    }
}

void table_ensure_fit(HashTable *table)
{
    isize min_capacity = 1024;
    if (((table->occupied + 16) >= table->capacity) || (table->collisions > (table->capacity * 2 / 5))) {
        table_fit(table, clamp_min(min_capacity, table->capacity * 2));
    }
}

isize table_get(HashTable *table, usize key)
{
    assert(is_power_of_two_or_zero(table->capacity));
    isize result = -1;

    if (table->capacity == 0) {
        return result;
    }

    usize hash = hash64(key);
    usize mask = table->capacity - 1;
    usize index = hash & mask;
    while (table->keys[index] && (table->keys[index] != key)) {
        index = (index + 1) & mask;
    }
    if (table->keys[index] == key) {
        result = table->value_indices[index];
        assert(table->keys_linear[result] == key);
    }

    return result;
}

usize table_value(HashTable *table, isize index)
{
    assert(index >= 0 && index < table->capacity);
    return table->values_linear[index];
}

usize table_get_value(HashTable *table, usize key)
{
    return table_value(table, table_get(table, key));
}

void table_add(HashTable *table, usize key, usize value)
{
    assert(key != 0);

    table_ensure_fit(table);

    usize hash = hash64(key);
    usize mask = table->capacity - 1;
    usize index = hash & mask;

    while (table->keys[index] && (table->keys[index] != key)) {
        index = (index + 1) & mask;
    }

    if (table->keys[index] == key) {
        assert_unreachable(); // This is a reasonable thing to do but the rest of this file should never do it.
        table->values_linear[table->value_indices[index]] = value;
        return;
    }

    assert((isize)index < table->capacity);
    assert(table->keys[index] == 0);
    assert(table->value_indices[index] == 0);
    assert(table->occupied < UINT_MAX);

    table->keys[index] = key;
    table->value_indices[index] = (u32)table->occupied;
    table->keys_linear[table->occupied] = key;
    table->values_linear[table->occupied] = value;
    table->occupied++;

    if ((hash & mask) != index) {
        table->collisions++;
    }
}

usize table_get_or_add_value(HashTable *table, usize key, usize value)
{
    isize idx = table_get(table, key);
    if (idx >= 0) {
        return table_value(table, idx);
    }
    table_add(table, key, value);
    return value;
}

// The same as HashTable but specialised for counting very small (1/2) and very
// large (100k+ ?) numbers of state transitions.
//
// One gotcha: value_indices are stored offset by 1, so that we can distinguish
// between a free slot and the 0th slot, without storing a separate keys buffer
// (as in HashTable)
enum
{
    HashCounter_InPlaceCutoff = 5, // MarkovRow == 64 bytes
    HashCounter_LinearSearchInitialSize = 8,
    HashCounter_LinearSearchCutoff = 64
};

typedef struct HashCounter
{
    i32 occupied;
    i32 capacity;
    union {
        struct {
            u32 *value_indices;
            u32 *keys_linear;
            u32 *values_linear;
        };
        struct {
            u32 keys_inplace[HashCounter_InPlaceCutoff];
            u32 values_inplace[HashCounter_InPlaceCutoff];
        };
    };
} HashCounter;

u32 *hashcounter_keys(HashCounter *table)
{
    if (table->occupied <= HashCounter_InPlaceCutoff) {
        return table->keys_inplace;
    }

    return table->keys_linear;
}

u32 *hashcounter_values(HashCounter *table)
{
    if (table->occupied <= HashCounter_InPlaceCutoff) {
        return table->values_inplace;
    }

    return table->values_linear;
}

b32 hashcounter_contains(HashCounter *table, u32 key)
{
    b32 result = false;
    if (table->occupied <= HashCounter_LinearSearchCutoff) {
        u32 *keys = hashcounter_keys(table);
        for (isize i = 0; i < table->occupied; i++) {
            if (keys[i] == key) {
                result = true;
                break;
            }
        }
    } else {
        usize hash = hash64(key);
        usize mask = table->capacity - 1;
        usize index = hash & mask;
        u32 *keys = hashcounter_keys(table) - 1;
        while (table->value_indices[index] && (keys[table->value_indices[index]] != key)) {
            index = (index + 1) & mask;
        }
        if (table->value_indices[index] && keys[table->value_indices[index]] == key) {
            result = true;
        }
    }

    return result;
}

void hashcounter_increment(HashCounter *table, u32 key, i32 inc)
{
    u32 *keys = hashcounter_keys(table);
    u32 *values = hashcounter_values(table);

    if (table->occupied < HashCounter_LinearSearchCutoff) {
        for (isize i = 0; i < table->occupied; i++) {
            if (keys[i] == key) {
                values[i] += inc;
                // !
                return;
            }
        }
        if (table->occupied == HashCounter_InPlaceCutoff) {
            i32 new_cap = HashCounter_LinearSearchInitialSize;
            u32 *keys_linear = bucket_alloc(&buckets, u32, new_cap);
            u32 *values_linear = bucket_alloc(&buckets, u32, new_cap);
            for (isize i = 0; i < table->occupied; i++) {
                keys_linear[i] = keys[i];
                values_linear[i] = values[i];
            }
            keys = keys_linear;
            values = values_linear;
            table->capacity = new_cap;
            table->value_indices = 0;
            table->keys_linear = keys_linear;
            table->values_linear = values_linear;
        } else if (table->capacity && table->occupied >= table->capacity) {
            assert(table->occupied == table->capacity);
            i32 old_cap = table->capacity;
            i32 new_cap = old_cap * 2;
            table->capacity = new_cap;
            table->keys_linear = bucket_realloc(&buckets, u32, table->keys_linear, old_cap, new_cap);
            table->values_linear = bucket_realloc(&buckets, u32, table->values_linear, old_cap, new_cap);
            keys = table->keys_linear;
            values = table->values_linear;
        }
    } else {
        // Keeping occupancy low doesn't seem to matter
        if (table->occupied > table->capacity * 96 / 100) {
            i32 old_cap = table->capacity;
            i32 new_cap = old_cap * 2;
            table->capacity = new_cap;
            table->keys_linear = bucket_realloc(&buckets, u32, table->keys_linear, old_cap, new_cap);
            table->values_linear = bucket_realloc(&buckets, u32, table->values_linear, old_cap, new_cap);
            keys = table->keys_linear;
            values = table->values_linear;
            table->value_indices = bucket_realloc(&buckets, u32, table->value_indices, old_cap, new_cap);
            memset(table->value_indices, 0, new_cap*sizeof(u32));

            isize mask = new_cap - 1;
            for (isize i = 0; i < table->occupied; i++) {
                usize hash = hash64(table->keys_linear[i]);
                usize index = hash & mask;
                while (table->value_indices[index] != 0) {
                    index = (index + 1) & mask;
                }
                table->value_indices[index] = (u32)i + 1;
            }
        }

        usize hash = hash64(key);
        usize mask = table->capacity - 1;
        usize index = hash & mask;
        u32 *keys_linear = table->keys_linear - 1;
        while (table->value_indices[index] && (keys_linear[table->value_indices[index]] != key)) {
            index = (index + 1) & mask;
        }
        if (table->value_indices[index] && keys_linear[table->value_indices[index]] == key) {
            table->values_linear[table->value_indices[index] - 1] += inc;
            // !
            return;
        } else {
            assert((i32)index < table->capacity);
            assert(table->value_indices[index] == 0);
            table->value_indices[index] = (u32)table->occupied + 1;
        }
    }

    assert_implies(keys == table->keys_inplace, table->occupied < array_length(table->keys_inplace));
    assert_implies(table->occupied >= array_length(table->keys_inplace), table->occupied < table->capacity);
    keys[table->occupied] = key;
    values[table->occupied] = inc;
    table->occupied++;
}

typedef struct InternTable
{
    HashTable table;
    Stack strings_memory;
    u8 *strings;
} InternTable;

InternTable interns = {0};

i32 intern(u8 *start, isize length)
{
    if (interns.strings == 0) {
        // Assume this never runs out.
        isize strings_size = MB(256);
        u8 *strings = xmalloc(strings_size);
        interns.strings_memory = (Stack) { strings, strings, strings + strings_size };
        interns.strings = strings;
    }

    usize hash = hash_fnv((u8*)start, length);
    isize index = table_get(&interns.table, (usize)hash);
    i32 result = 0;

    if (index >= 0) {
        result = (i32)table_value(&interns.table, index);
        assert_no_collision(interns.strings + result, start, length);
    } else {
        u8 *buf = stack_alloc(&interns.strings_memory, u8, length + 1);
        memmove(buf, start, length);
        buf[length] = 0;

        assert((buf - interns.strings) < INT_MAX);
        i32 str_index = (i32)(buf - interns.strings);

        table_add(&interns.table, hash, str_index);

        result = str_index;
    }

    return result;
}

u8 *get_interned_token(u32 id)
{
    return interns.strings + id;
}

typedef struct RingBuffer
{
    usize *values;
    u32 index;
    u32 mask;
} RingBuffer;

RingBuffer make_ringbuffer(Stack *stack, u32 size)
{
    size = (u32)round_up_to_power_of_two(size);
    RingBuffer result = { .mask = size - 1 };
    result.values = stack_alloc(stack, usize, size);
    return result;
}

void ringbuffer_push(RingBuffer *buffer, usize value)
{
    buffer->values[++buffer->index & buffer->mask] = value;
}

usize ringbuffer_get(RingBuffer *buffer, isize index)
{
    assert((u32)index <= buffer->mask);
    return buffer->values[(buffer->index - index) & buffer->mask];
}

typedef struct TokenString
{
    u32 *tokens;
    i32 length;
    i32 capacity;
} TokenString;

b32 is_whitespace(u8 *p)
{
    return (*p != 0) && (*p <= ' ');
}

b32 is_whitespace_excluding_newline(u8 *p)
{
    return is_whitespace(p) && (*p != '\n');
}

void push_token(TokenString *str, u32 token)
{
    if (str->tokens == 0) {
        isize bytes = MB(1);
        str->tokens = xmalloc(bytes);
        str->capacity = (u32)(bytes / sizeof(u32));
    }
    if (str->length == str->capacity) {
        assert(multiply_is_safe_32(str->capacity, sizeof(u32) * 2));
        str->tokens = xrealloc(str->tokens, str->capacity * sizeof(u32) * 2);
        str->capacity *= 2;
    }
    assert(str->length < str->capacity);
    str->tokens[str->length++] = token;
}

void pop_tokens(TokenString *str, i32 count)
{
    assert(str->length >= count);
    str->length -= count;
}

void print_tokens(u32 *tokens, isize length)
{
    b32 f64_quote_flipflop = 0;
    b32 needs_space = true;

    // Pretty bad, but I don't care
    for (isize i = 0; i < length; i++) {
        u8 *token = get_interned_token(tokens[i]);
        switch (*token) {
            case '\'': {
                // Can't disambiguate between single quotes and apostrophes.. boo
                printf("\'");
                needs_space = false;
            } break;
            case '"': {
                // Wrong half the time
                printf("\"");
                needs_space = f64_quote_flipflop;
                f64_quote_flipflop ^= 1;
            } break;
            case '.':
            case ',':
            case '!':
            case ';':
            case ':':
            case '%':
            case ')':
            case ']': {
                printf("%c", *token);
                needs_space = true;
            } break;
            case '(':
            case '[':
            case '$':{
                printf(" %c", *token);
                needs_space = false;
            } break;
            case '*':
            case '_': {
                printf("%s", token);
                needs_space = false;
            } break;
            case '@': {
                // wiki-103 token for hyphenation
                needs_space = false;
                printf("%c", token[1]);
            } break;
            default: {
                if (needs_space) {
                    printf(" ");
                }
                printf("%s", token);
                needs_space = true;
            }
        }
    }
}

typedef struct SourceText
{
    TokenString text;
    HashTable states;
} SourceText;

typedef struct MarkovRow
{
    HashCounter counts;
    i64 cumulative;
    i32 position_first_seen;
} MarkovRow;

compile_time_assert(sizeof(MarkovRow) == 64);

typedef struct MarkovChain
{
    SourceText source;

    // With large texts and large window_power multipliers, hashcounter values
    // can overflow. If they have, this is set to true.
    b32 count_overflow;

    // Number of sequential tokens that make a state
    i32 order;
    // Number of tokens to predict
    i32 stride;
    // Number of tokens back to look
    i32 offset;
    // Size of window to f64 count over
    i32 analysis_window;
    i32 generator_window;
    // Scale count by window_powers[window_position]. Must be of size window + 1.
    i32 *analysis_window_powers;
    i32 *generator_window_powers;
    // Probability multiplier on generation
    f64 power;
    // Require that this chain can make any transition drawn from a set
    b32 required;

    // If set, randomly sample the source text n times. If 0, do the whole text.
    i32 sample;

    HashTable state_to_row;
} MarkovChain;

typedef struct MarkovChainSet
{
    MarkovChain *chains;
    isize count;

    // If set, use chain power as the probabilty of a draw from a chain.
    // Otherwise, use the cumulative row value scaled by the chain power,
    // relative to the rows of other chains.
    b32 absolute_power;

    // Power of this set when generating from multiple sets.
    f64 set_power;

    i32 order;
    i32 stride;
    i32 offset;
    i32 analysis_window;
    i32 generator_window;
    i32 state_size;

    i32 *generated_counts;
    i32 *rejected_counts;
} MarkovChainSet;

usize hash_state(u32 *tokens, isize order)
{
    return (usize)hash_fnv((u8 *)tokens, order * sizeof(u32));
}

usize hash_state_flatten_zero(u32 *tokens, isize order)
{
    for (isize i = 0; i < order; i++) {
        if (tokens[i] == 0) {
            return hash_state(&(u32){0}, 1);
        }
    }
    return hash_state(tokens, order);
}

void dump_markov_row(MarkovChain *chain, MarkovRow *row)
{
    printf("\"");
    print_tokens(&chain->source.text.tokens[row->position_first_seen], chain->order);
    printf("\" (%lld):\n", row->cumulative);
    u32 *keys = hashcounter_keys(&row->counts);
    u32 *counts = hashcounter_values(&row->counts);
    for (isize i = 0; i < row->counts.occupied; i++) {
        printf("\t");
        print_tokens(chain->source.text.tokens + keys[i], chain->stride);
        printf(" (%d)\n", counts[i]);
    }
    printf("\n");
}

void markov_chain_set_init(MarkovChainSet *set)
{
    assert(set->chains);
    assert(set->count > 0);

    set->generated_counts = stack_alloc_zero(&scratch, i32, set->count);
    set->rejected_counts = stack_alloc_zero(&scratch, i32, set->count);

    MarkovChain *chains = set->chains;
    i32 offset = 0;
    i32 order = 0;
    i32 stride = 0;
    i32 analysis_window = 0;
    i32 generator_window = 0;
    i32 min_stride = chains[0].stride;
    for (isize i = 0; i < set->count; i++) {
        assert(chains[i].count_overflow == false);
        assert(chains[i].order >= 0);
        assert(chains[i].offset >= 0);
        assert(chains[i].analysis_window >= 0);
        offset = max(offset, chains[i].offset);
        order = max(order, chains[i].order);
        analysis_window = max(analysis_window, chains[i].analysis_window);
        generator_window = max(generator_window, chains[i].generator_window);
        stride = max(stride, chains[i].stride);
        min_stride = min(min_stride, chains[i].stride);

        if (chains[i].analysis_window_powers == 0) {
            chains[i].analysis_window_powers = stack_alloc(&scratch, i32, chains[i].analysis_window + 1);
            for (isize j = 0; j <= chains[i].analysis_window; j++) {
                chains[i].analysis_window_powers[j] = 1;
            }
        }
        if (chains[i].generator_window_powers == 0) {
            chains[i].generator_window_powers = stack_alloc(&scratch, i32, chains[i].generator_window + 1);
            for (isize j = 0; j <= chains[i].generator_window; j++) {
                chains[i].generator_window_powers[j] = 1;
            }
        }
    }

    for (isize i = 0; i < set->count; i++) {
        // Violating this effectively removes any chain with stride less than
        // that of the required chains.
        assert_implies(chains[i].required, chains[i].stride == min_stride);
    }

    set->order = order;
    set->offset = offset;
    set->stride = stride;
    set->analysis_window = analysis_window;
    set->generator_window = generator_window;
    set->state_size = order + offset + max(analysis_window, generator_window);
}

void build_markov_chain(MarkovChain *chain)
{
    TokenString *text = &chain->source.text;
    HashTable *states = &chain->source.states;

    chain->stride = chain->stride ? chain->stride : 1;

    RingBuffer *window = stack_push(&scratch, RingBuffer, make_ringbuffer(&scratch, chain->analysis_window + 1));

    i32 *window_powers = 0;
    if (chain->analysis_window_powers) {
        window_powers = chain->analysis_window_powers;
    } else {
        window_powers = stack_alloc(&scratch, i32, chain->analysis_window + 1);
        for (isize i = 0; i <= chain->analysis_window; i++) {
            window_powers[i] = 1;
        }
    }

    usize null_key = hash_state(&(u32){0}, 1);

    table_fit(&chain->state_to_row, text->length / 8);

    isize state_size = chain->order + chain->offset + chain->analysis_window;
    // Defaults--sample the entire text contiguously
    isize sample_count = 1;
    isize states_per_sample = text->length - state_size - chain->stride;
    isize sample_index = 0;
    isize last_sample = 1;
    if (chain->sample > 0) {
        // Or randomly sample sample_count times
        states_per_sample = 32;
        sample_count = clamp_max(text->length, chain->sample) / states_per_sample;
        last_sample = text->length - state_size - chain->stride - states_per_sample;
        sample_index = randomr(&random_state, (u32)last_sample);
    }

    for (isize n = 0; n < sample_count; n++) {
        for (isize i = 0; i < chain->analysis_window; i++) {
            isize idx = i + sample_index;
            usize state_key = hash_state(&text->tokens[idx], chain->order);
            isize state_index = table_get(&chain->state_to_row, state_key);

            MarkovRow *row = 0;
            if (state_index == -1) {
                row = bucket_alloc(&buckets, MarkovRow, 1);
                *row = (MarkovRow) { .position_first_seen = (i32)idx };
                table_add(&chain->state_to_row, state_key, (usize)row);
            } else {
                row = (MarkovRow *)table_value(&chain->state_to_row, state_index);
                assert_no_collision(&text->tokens[i], &text->tokens[row->position_first_seen], chain->order);
            }

            ringbuffer_push(window, (usize)row);
        }

        isize end = sample_index + state_size + states_per_sample;
        for (isize end_of_window = sample_index + chain->analysis_window, end_of_state = sample_index + state_size; end_of_state < end; end_of_window++, end_of_state++) {
            usize next_key = hash_state_flatten_zero(&text->tokens[end_of_state], chain->stride);
            u32 next_state = (u32)table_get_or_add_value(states, next_key, end_of_state);

            // Discard any state that contains <unk> in wiki103, or whatever
            // indicates an unknown/removed low-power word in another corpus. We
            // only do this for the state being transitioned to, not the one being
            // transitioned from; this gives the chain some ability to recover if it
            // gets into a state it hasn't seen before.
            if (next_key == null_key) {
                continue;
            }

            assert_no_collision(&text->tokens[end_of_state], &text->tokens[next_state], chain->stride);
            usize state_key = hash_state(&text->tokens[end_of_window], chain->order);
            isize state_index = table_get(&chain->state_to_row, state_key);

            MarkovRow *row = 0;
            if (state_index == -1) {
                row = bucket_alloc(&buckets, MarkovRow, 1);
                *row = (MarkovRow) { .position_first_seen = (i32)end_of_window };
                table_add(&chain->state_to_row, state_key, (usize)row);
            } else {
                row = (MarkovRow *)table_value(&chain->state_to_row, state_index);
                assert_no_collision(&text->tokens[end_of_window], &text->tokens[row->position_first_seen], chain->order);
            }

            ringbuffer_push(window, (usize)row);

            for (isize i = 0; i <= chain->analysis_window; i++) {
                MarkovRow *row_in_window = (MarkovRow *)ringbuffer_get(window, i);
                hashcounter_increment(&row_in_window->counts, next_state, window_powers[i]);
                row_in_window->cumulative += window_powers[i];
            }
        }

        sample_index = randomr(&random_state, (u32)last_sample);
    }

    for (isize i = 0; i < chain->state_to_row.occupied; i++) {
        MarkovRow *row = (MarkovRow *)chain->state_to_row.values_linear[i];
        assert(row->cumulative > 0);

        u32 *values = hashcounter_values(&row->counts);

        i64 sum = 0;
        for (isize j = 0; j < row->counts.occupied; j++) {
            assert(values[j] > 0);
            sum += values[j];
        }
        assert(row->cumulative == sum);
        chain->count_overflow = row->cumulative != sum;
    }
}

typedef struct MarkovChainThreadData
{
    Stack stack;
    MarkovChain **chains;
    i32 start;
    i32 end;
} MarkovChainThreadData;

i32 build_markov_chain_thread(void *userdata)
{
    MarkovChainThreadData *data = userdata;
    if (scratch.base == 0) {
        scratch = push_stack_size(&data->stack, MB(8));
    }
    if (buckets.allocator.base == 0) {
        buckets.allocator = push_stack_size(&data->stack, GB(2));
    }
    for (isize i = data->start; i < data->end; i++) {
        build_markov_chain(data->chains[i]);
    }
    dump_buckets(&buckets);
    return 0;
}

void build_markov_chain_sets(Stack *memory, MarkovChainSet *sets, i32 set_count)
{
    i32 thread_count = core_count();
    void **threads = stack_alloc(&scratch, void *, thread_count);

    isize n_chains = 0;
    for (i32 i = 0; i < set_count; i++) {
        n_chains += sets[i].count;
    }

    MarkovChain **chains = stack_alloc(&scratch, MarkovChain *, n_chains);

    isize chain_index = 0;
    for (i32 i = 0; i < set_count; i++) {
        for (i32 j = 0; j < sets[i].count; j++) {
            chains[chain_index++] = &sets[i].chains[j];
        }
    }

    i32 chains_per_thread = (i32)n_chains / thread_count;
    isize stack_space_per_thread = (memory->end - memory->ptr) / thread_count;
    i32 remainder = n_chains % thread_count;
    i32 start = 0;
    i32 end = chains_per_thread;
    for (i32 i = 0; i < thread_count; i++) {
        Stack chain_stack = push_stack_size(memory, stack_space_per_thread);
        if (remainder) {
            remainder--;
            end++;
        }
        threads[i] = create_thread(build_markov_chain_thread, stack_push(&scratch, MarkovChainThreadData, (MarkovChainThreadData) {
            .stack = chain_stack,
            .chains = chains,
            .start = start,
            .end = end
        }));
        start = end;
        end += chains_per_thread;
    }
    assert(end == (n_chains + chains_per_thread));
    wait_for_threads(threads, thread_count);
}

typedef struct ActiveRow
{
    MarkovChain *chain;
    MarkovRow *row;
    f64 boundary;
    i64 cumulative;
    i32 chain_index;
    i32 window_index;
    b32 required;
} ActiveRow;

typedef struct PushRandomStateTransitionResult
{
    i32 n_generated;
    i32 chain_index;
} PushRandomStateTransitionResult;

PushRandomStateTransitionResult push_random_state_transition(TokenString *string, MarkovChainSet *set, u32 *state_tokens)
{
    // This function is a little involved, mostly to satisfy the 'required'
    // option: it turns out that, for most configurations you'd ever use it
    // with, the number of required states vastly outnumber the number of
    // non-required states. The obvious thing--removing them all upfront--is
    // incredibly slow, whereas just rejection sampling works (usually) quite
    // well, provided we do it without replacement. So, we need to track
    // non-rejected transitions, and sample from those. This is further
    // complicated by the possibility of setting power to 0, which combined with
    // required means the chain will only be sampled from if it has the only
    // transitions for the current state. Since we're rejection sampling, we
    // only find out when we run out of non-required states to sample from.
    Stack stack = push_stack(&scratch);
    usize null_key = hash_state(&(u32){0}, 1);

    ActiveRow *active_rows = stack_alloc(&stack, ActiveRow, set->count * (set->generator_window + 1));
    isize active_count = 0;
    isize possible_transitions = 0;
    isize zero_power_transitions = 0;
    f64 power = 0.;
    for (i32 i = 0; i < set->count; i++) {
        MarkovChain *chain = &set->chains[i];
        isize state_offset = set->state_size - chain->order - chain->offset - chain->generator_window;
        for (i32 j = 0; j <= chain->generator_window; j++) {
            usize state_hash = hash_state(state_tokens + state_offset + j, chain->order);
            isize index = table_get(&chain->state_to_row, state_hash);
            if (index >= 0) {
                MarkovRow *row = (MarkovRow *)table_value(&chain->state_to_row, index);

                // For big values this will make a mess of the precision of the
                // boundary values. But choosing within a row will be fine,
                // because it's all integer based.
                i64 cumulative = row->cumulative * chain->generator_window_powers[chain->generator_window - j];
                assert(cumulative >= 0);

                if (set->absolute_power) {
                    power += chain->power;
                } else {
                    power += (f64)cumulative * chain->power;
                }

                possible_transitions += row->counts.occupied;
                active_rows[active_count++] = (ActiveRow) {
                    .chain = chain,
                    .row = row,
                    .boundary = power,
                    .cumulative = cumulative,
                    .chain_index = i,
                    .window_index = chain->generator_window - j,
                    .required = chain->required
                };

                if (chain->power == 0.) {
                    zero_power_transitions += row->counts.occupied;
                }
            }
        }
    }

    PushRandomStateTransitionResult result = { 1, -1 };

    if (active_count > 0) {
        // i64, because
        //
        // count * some_analysis_window_power * some_generator_window_power
        //
        // can overflow. I think it only really happens for fairly contrived
        // data sets and windows.
        i64 **row_choices = stack_alloc(&stack, i64 *, active_count);
        i64 *choices = stack_alloc(&stack, i64, possible_transitions);
        i64 *choice = choices;
        for (isize i = 0; i < active_count; i++) {
            MarkovChain *chain = active_rows[i].chain;
            MarkovRow *row = active_rows[i].row;
            i32 window_index = active_rows[i].window_index;
            row_choices[i] = choice;
            u32 *counts = hashcounter_values(&row->counts);
            for (isize j = 0; j < row->counts.occupied; j++) {
                *choice++ = (i64)counts[j] * chain->generator_window_powers[window_index];
            }
        }

        while (possible_transitions > 0) {
            if (possible_transitions == zero_power_transitions) {
                // There are no non-zero power rows left to use, so unlock
                // the zero power rows
                power = 0.;
                for (isize i = 0; i < active_count; i++) {
                    assert_implies(active_rows[i].chain->power > 0, active_rows[i].cumulative == 0);
                    if (active_rows[i].cumulative > 0) {
                        if (set->absolute_power) {
                            power += 1.;
                        } else {
                            power += (f64)active_rows[i].cumulative;
                        }
                    }
                    active_rows[i].boundary = power;
                }
            }

            f64 chooser = random01d(&random_state) * power;
            isize row_choice = 0;
            for (; row_choice < active_count; row_choice++) {
                // <=: chooser and boundary can be 0 if only zero power rows remain.
                if (active_rows[row_choice].cumulative > 0 && chooser <= active_rows[row_choice].boundary) {
                    break;
                }
            }
            assert(row_choice < active_count);
            ActiveRow *active_row = &active_rows[row_choice];

            MarkovRow *row = active_row->row;
            i32 chain_index = active_row->chain_index;
            MarkovChain *chain = active_row->chain;

            i64 t = randomr64(&random_state, active_row->cumulative);
            i64 *counts = row_choices[row_choice];
            i64 cumsum = 0;
            isize transition = 0;
            for (; transition < row->counts.occupied; transition++) {
                cumsum += counts[transition];
                if (t < cumsum) {
                    break;
                }
            }
            assert(transition < row->counts.occupied);

            u32 *states = hashcounter_keys(&row->counts);
            u32 state = states[transition];
            isize idx = state;
            for (isize k = 0; k < chain->stride; k++) {
                push_token(string, chain->source.text.tokens[idx++]);
            }

            b32 state_ok = true;
            for (isize i = 0; i < active_count && state_ok; i++) {
                MarkovChain *req_chain = active_rows[i].chain;
                if (active_rows[i].required && chain != req_chain) {
                    if (hashcounter_contains(&active_rows[i].row->counts, state) == false) {
                        // We need to reconstruct the hash.. hoo boy
                        state_ok = false;

                        assert(req_chain->stride <= chain->stride);
                        usize key = hash_state_flatten_zero(&chain->source.text.tokens[state], req_chain->stride);
                        if (key != null_key) {
                            isize req_state_index = table_get(&req_chain->source.states, key);
                            if (req_state_index >= 0) {
                                u32 req_state = (u32)table_value(&req_chain->source.states, req_state_index);
                                state_ok = hashcounter_contains(&active_rows[i].row->counts, req_state);
                            }
                        }
                    }
                }
            }

            if (state_ok == false) {
                possible_transitions--;

                pop_tokens(string, chain->stride);

                i64 count_removed = row_choices[row_choice][transition];
                active_row->cumulative -= count_removed;

                f64 power_removed = 0.;
                if (set->absolute_power == false) {
                    power_removed = (f64)count_removed;
                    if (possible_transitions >= zero_power_transitions) {
                        power_removed *= chain->power;
                    }
                }

                if (active_row->cumulative == 0) {
                    active_row->required = false;

                    if (set->absolute_power) {
                        assert_implies(active_row->chain->power == 0, possible_transitions < zero_power_transitions);
                        power_removed = active_row->chain->power ? active_row->chain->power : 1.;
                    }
                }

                power -= power_removed;
                for (isize i = row_choice; i < active_count; i++) {
                    active_rows[i].boundary -= power_removed;
                }

                counts[transition] = 0;

                set->rejected_counts[chain_index]++;
                continue;
            }

            set->generated_counts[chain_index]++;
            result = (PushRandomStateTransitionResult) { chain->stride, chain_index };
            break;
        }
    }

    if (possible_transitions == 0) {
        push_token(string, 0);
    }

    pop_stack(&scratch, &stack);

    return result;
}

TokenString generate_markov_text(MarkovChainSet *sets, i32 set_count, u32 *initial_state, isize length)
{
    TokenString result = {0};
    f64 power = 0.;

    isize state_size = 0;
    for (isize i = 0; i < set_count; i++) {
        markov_chain_set_init(&sets[i]);
        state_size = max(state_size, sets[i].state_size);
        power += sets[i].set_power;
    }

    for (isize i = 0; i < state_size; i++) {
        push_token(&result, initial_state[i]);
    }

    isize generated = 0;
    while (result.length < length) {
        u32 *state = &result.tokens[generated];
        u32 set = 0;
        if (power > 0) {
            f64 choice = random01d(&random_state) * power;
            f64 acc = 0;
            for (isize i = 0; i < set_count; i++) {
                acc += sets[i].set_power;
                if (choice <= acc) {
                    set = (u32)i;
                    break;
                }
            }
        } else {
            set = randomr(&random_state, set_count);
        }

        PushRandomStateTransitionResult prst = push_random_state_transition(&result, &sets[set], state);
        if (prst.chain_index == -1) {
            for (isize i = 0; i < set_count - 1; i++) {
                pop_tokens(&result, 1);
                set = (set + 1) % set_count;
                prst = push_random_state_transition(&result, &sets[set], state);
                if (prst.chain_index >= 0) {
                    break;
                }
            }
        }
        generated += prst.n_generated;
    }

    return result;
}

enum {
    Markdown_Word = 0,
    Markdown_Whitespace,
    Markdown_Single,
    Markdown_Consecutive
};

static const u8 markdown_char_type[256] = {
    [0] = Markdown_Whitespace,
    [1] = Markdown_Whitespace,
    [2] = Markdown_Whitespace,
    [3] = Markdown_Whitespace,
    [4] = Markdown_Whitespace,
    [5] = Markdown_Whitespace,
    [6] = Markdown_Whitespace,
    [7] = Markdown_Whitespace,
    [8] = Markdown_Whitespace,
    [9] = Markdown_Whitespace,
    ['\n'] = Markdown_Single,
    [11] = Markdown_Whitespace,
    [12] = Markdown_Whitespace,
    [13] = Markdown_Whitespace,
    [14] = Markdown_Whitespace,
    [15] = Markdown_Whitespace,
    [16] = Markdown_Whitespace,
    [17] = Markdown_Whitespace,
    [18] = Markdown_Whitespace,
    [19] = Markdown_Whitespace,
    [20] = Markdown_Whitespace,
    [21] = Markdown_Whitespace,
    [22] = Markdown_Whitespace,
    [23] = Markdown_Whitespace,
    [24] = Markdown_Whitespace,
    [25] = Markdown_Whitespace,
    [26] = Markdown_Whitespace,
    [27] = Markdown_Whitespace,
    [28] = Markdown_Whitespace,
    [29] = Markdown_Whitespace,
    [30] = Markdown_Whitespace,
    [31] = Markdown_Whitespace,
    [32] = Markdown_Whitespace,
    [','] = Markdown_Single,
    ['!'] = Markdown_Single,
    ['"'] = Markdown_Single,
    ['\''] = Markdown_Single,
    [':'] = Markdown_Single,
    [';'] = Markdown_Single,
    ['-'] = Markdown_Single,
    ['+'] = Markdown_Single,
    ['='] = Markdown_Single,
    ['<'] = Markdown_Single,
    ['>'] = Markdown_Single,
    ['('] = Markdown_Single,
    [')'] = Markdown_Single,
    ['['] = Markdown_Single,
    [']'] = Markdown_Single,
    ['/'] = Markdown_Single,
    ['#'] = Markdown_Consecutive,
    ['_'] = Markdown_Consecutive,
    ['*'] = Markdown_Consecutive,
    ['.'] = Markdown_Consecutive,
};

u8 *consume_markdown_token(u8 **cursor)
{
    u8 *p = *cursor;
    u8 *last_whitespace = 0;
    while (markdown_char_type[*p] == Markdown_Whitespace) {
        last_whitespace = p;
        p++;
    }
    u8 *result = p;
    switch (markdown_char_type[*p]) {
        // Token is until next non-word character.
        case Markdown_Word: {
            // Special case for tables.
            if (*p == '|') {
                while (markdown_char_type[*p] != Markdown_Whitespace) {
                    p++;
                }
            } else {
                while (markdown_char_type[*p] == Markdown_Word) {
                    p++;
                }
            }
        } break;
        // Always one character.
        case Markdown_Single: {
            p++;
        } break;
        // Token is character repeating. ## Titles, __italics__, **emphasis**, ... ellipses
        // Hack: We include one character of leading and trailing whitespace, so that it can distinguish between open and closing tokens
        case Markdown_Consecutive: {
            if (last_whitespace) {
                result = last_whitespace;
            }
            u8 c = *p;
            while (*p == c) {
                p++;
            }
            if (markdown_char_type[*p] == Markdown_Whitespace) {
                p++;
            }
        } break;
        default: assert_unreachable();
    }

    *cursor = p;
    return result;
}

TokenString tokenize_md(File *file)
{
    u8 *text = file->data;
    assert(text[file->size - 1] == 0);
    TokenString result = {0};
    u8 *p = text;
    while (*p) {
        u8 *start = consume_markdown_token(&p);
        u8 *end = p;
        if (start != end) {
            push_token(&result, intern(start, end - start));
        }
    }
    return result;
}

TokenString tokenize_wiki103(File *file)
{
    // Todo: There's some weird junk like \n"\n"\n in the csv that might be worth filtering out

    u8 *text = file->data;
    assert(text[file->size - 1] == 0);

    TokenString result = {0};

    u8 *p = text;
    while (*p) {
        while (*p && is_whitespace_excluding_newline(p)) {
            p++;
        }
        u8 *start = p;
        while (*p && (is_whitespace_excluding_newline(p) == false)) {
            p++;
        }
        u8 *end = p;
        if (end[-1] == '-' && ((end - start) > 1)) {
            // wiki103 puts hyphens on the end of words for cases like "two- and
            // three-fold". If you want to do cutting-edge research I guess you
            // might want to emulate this behaviour but all other puncuation
            // gets its own token (for example, "'s" is a token). It's not clear
            // to me that this is not an oversight.
            end--;
        }
        if (*start == '=' && *end == ' ') {
            // These suck.
            continue;
        }
        if (start != end) {
            push_token(&result, intern(start, end - start));
        }
    }
    return result;
}

typedef struct Tag
{
    u8 *tag;
    i32 length;
    b32 keep_body;
} Tag;

#define memcmp_eq_literal(a, literal) (memcmp(a, literal, sizeof(literal) - 1) == 0)

Tag consume_tag(u8 **cursor)
{
    u8 *p = *cursor;
    assert(*p == '<');
    p++;
    Tag result = { .tag = p };

    while ((*p != '>') && (is_whitespace(p) == false)) {
        result.length++;
        p++;
    }

    while (*p++ != '>') {
        ;
    }

    result.keep_body = (result.tag[0] == '/')    // Closing tag
                    || (p[-2] == '/')            // Self-closing tag
                    || memcmp_eq_literal(result.tag, "sp")
                    || memcmp_eq_literal(result.tag, "p")
                    || memcmp_eq_literal(result.tag, "title")
                    || memcmp_eq_literal(result.tag, "speaker")
                    || memcmp_eq_literal(result.tag, "placeName")
                    || memcmp_eq_literal(result.tag, "quote")
                    || memcmp_eq_literal(result.tag, "l ")
                    || memcmp_eq_literal(result.tag, "l>")
                    || memcmp_eq_literal(result.tag, "foreign")
                    || memcmp_eq_literal(result.tag, "div1");

    *cursor = p;
    return result;
}

void skip_tag(u8 **cursor, Tag tag)
{
    assert(tag.keep_body == false);
    u8 *p = *cursor;
    while (memcmp(p, tag.tag, tag.length) != 0) {
        p++;
    }
    if (p[-1] != '/') {
        p++;
        skip_tag(&p, tag);
    }
    while (*p++ != '>') {
        ;
    }
    *cursor = p;
}

void skip_perseus_xml_whitespace_and_tags(u8 **cursor)
{
    u8 *p = *cursor;
    while (is_whitespace(p) || *p == '<') {
        while (*p && is_whitespace(p)) {
            p++;
        }
        if (*p == '<') {
            Tag tag = consume_tag(&p);
            if (tag.keep_body == false) {
               skip_tag(&p, tag);
            }
        }
    }
    *cursor = p;
}

TokenString tokenize_perseus_xml(File *file)
{
    u8 *text = file->data;
    assert(text[file->size - 1] == 0);

    TokenString result = {0};

    u8 *p = text;

    while (memcmp_eq_literal(p, "<sp>") == false) {
        p++;
    }

    while (*p) {
        skip_perseus_xml_whitespace_and_tags(&p);
        if (*p) {
            // Treat the actual text like markdown
            u8 *start = consume_markdown_token(&p);
            u8 *end = p;
            assert(*start != '<');
            if (start != end) {
                push_token(&result, intern(start, end - start));
            }
        }
    }

    return result;
}

void dump_token_string(TokenString *string)
{
    print_tokens(string->tokens, string->length);
}

void dump_markov_chain(MarkovChain *chain)
{
    printf("%d-%d-%d:\n", chain->order, chain->offset, chain->stride);
    for (isize i = 0; i < chain->state_to_row.occupied; i++) {
        MarkovRow *row = (MarkovRow *)chain->state_to_row.values_linear[i];
        dump_markov_row(chain, row);
    }
    printf("\n\n");
}

TokenString *partition_token_string(TokenString *string, i32 partitions)
{
    TokenString *result = stack_alloc(&scratch, TokenString, partitions);
    i32 tokens_per_partition = string->length / partitions;
    // Discard end tokens if not an even division. We already handle partition
    // boundaries incorrectly, and this is really intended for big corpuses,
    // where the difference amounts to rounding error
    i32 token_index = 0;
    for (isize i = 0; i < partitions; i++) {
        result[i] = (TokenString) { string->tokens + token_index, tokens_per_partition, 0 };
        token_index += tokens_per_partition;
    }
    return result;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    isize blob_size = GB(16);
    u8 *blob = system_alloc(blob_size);
    assert(blob);
    Stack stack = { blob, blob, blob + blob_size };

    scratch = push_stack_size(&stack, MB(8));
    buckets.allocator = push_stack_size(&stack, GB(1));
    stack = push_stack(&stack);

    random_state.inc = ticks();

    // Make 0 <unk>. Wiki103 uses this for words that appear less than 3 times in the entire dataset.
    // This lets us skip it easily during text analysis/generation.
    intern((u8*)"<unk>", sizeof("<unk>") - 1);

    i64 ticks_a = ticks();

    SourceText sources[] = {
        { .text = tokenize_md((File[]){ read_file(&stack, 0, "markov/dnd.md")})},
        { .text = tokenize_perseus_xml((File[]){ read_file(&stack, 0, "markov/republic.xml")})},
    };

    i64 ticks_b = ticks();

    MarkovChain chains_a[] = {
        { .source = sources[0], .order = 1, .offset =  0, .stride = 1, .power = 0, .required = true },
        { .source = sources[0], .order = 2, .offset =  0, .stride = 1, .power = 0, .required = true },
        { .source = sources[0], .order = 2, .offset = 0, .stride = 1, .power = 1. / 2., .generator_window = 0 },
        { .source = sources[0], .order = 2, .offset = 3, .stride = 1, .power = 1. / 4., .generator_window = 1 },
        { .source = sources[0], .order = 2, .offset = 7, .stride = 1, .power = 1. / 9., .generator_window = 2, .analysis_window = 1 },
        { .source = sources[0], .order = 2, .offset = 12, .stride = 1, .power = 1. / 16., .generator_window = 3, .analysis_window = 2 },
    };

    MarkovChain chains_b[] = {
        { .source = sources[1], .order = 1, .offset =  0, .stride = 1, .power = 0, .required = true },
        { .source = sources[1], .order = 2, .offset =  0, .stride = 1, .power = 0, .required = true },
        { .source = sources[1], .order = 2, .offset = 0, .stride = 1, .power = 1. / 2., .generator_window = 0 },
        { .source = sources[1], .order = 2, .offset = 3, .stride = 1, .power = 1. / 4., .generator_window = 1 },
        { .source = sources[1], .order = 2, .offset = 7, .stride = 1, .power = 1. / 9., .generator_window = 2, .analysis_window = 1 },
        { .source = sources[1], .order = 2, .offset = 12, .stride = 1, .power = 1. / 16., .generator_window = 3, .analysis_window = 2 },
    };

    MarkovChain *chains_chains[] = { chains_a, chains_b };

    MarkovChainSet sets[] = {
        {
            .chains = chains_a,
            .count = array_length(chains_a),
            .set_power = 1,
            .absolute_power = true,
        },
        {
            .chains = chains_b,
            .count = array_length(chains_b),
            .set_power = 1,
            .absolute_power = true,
        },
    };

    build_markov_chain_sets(&stack, sets, array_length(sets));

    i64 ticks_c = ticks();

    TokenString *tokens = &chains_a[0].source.text;
    TokenString generated = generate_markov_text(sets, array_length(sets), tokens->tokens + randomi(&random_state, tokens->length / 4, tokens->length * 3 / 4), 4096);

    i64 ticks_d = ticks();

    printf("Unique tokens: %lld\n", interns.table.occupied);
    printf("Tokenise: %2.3fs\n", seconds_between(ticks_a, ticks_b));
    printf("Analyse: %2.3fs\n", seconds_between(ticks_b, ticks_c));
    printf("Generate: %2.3fs\n", seconds_between(ticks_c, ticks_d));

    for(isize i = 0; i < array_length(sets); i++) {
        for (isize j = 0; j < sets[i].count; j++) {
            printf("%lld %d-%d-%d: %d (%d rejected)\n", i, chains_chains[i][j].order, chains_chains[i][j].offset, chains_chains[i][j].stride, sets[i].generated_counts[j], sets[i].rejected_counts[j]);
        }
    }


    dump_token_string(&generated);

    return 0;
}
