#include "tt.h"
#include <cstring>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

TranspositionTable TTable;

// Static assert to ensure packing
static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes");

namespace {
constexpr size_t kLargePageThresholdMb = 256;

size_t large_page_size_bytes() {
#if defined(_WIN32)
    return static_cast<size_t>(GetLargePageMinimum());
#elif defined(__linux__)
    return 2ULL * 1024 * 1024;
#else
    return 0;
#endif
}
} // namespace

TranspositionTable::TranspositionTable(size_t size_mb) {
    current_gen = 0;
    resize(size_mb);
}

TranspositionTable::~TranspositionTable() {
    release();
}

void TranspositionTable::resize(size_t size_mb) {
    size_t size_bytes = size_mb * 1024 * 1024;
    size_t bucket_size = sizeof(TTBucket);
    size_t target_buckets = size_bytes / bucket_size;

    // Power of two
    size_t desired_buckets = 1;
    while (desired_buckets * 2 <= target_buckets) desired_buckets *= 2;

    bool want_large_pages = use_large_pages && size_mb >= kLargePageThresholdMb;
    size_t alloc_buckets = desired_buckets;

    if (want_large_pages) {
        size_t page_size = large_page_size_bytes();
        if (page_size == 0) {
            want_large_pages = false;
        } else {
            while (alloc_buckets > 1 && ((alloc_buckets * bucket_size) % page_size) != 0) {
                alloc_buckets /= 2;
            }
        }
    }

    release();
    num_buckets = alloc_buckets;
    size_t alloc_bytes = num_buckets * bucket_size;

    if (!(want_large_pages && alloc_bytes > 0 && allocate_large_pages(alloc_bytes))) {
        allocate_standard(num_buckets);
    }

    clear();
}

void TranspositionTable::set_large_pages(bool enabled) {
    use_large_pages = enabled;
}

void TranspositionTable::release() {
    if (using_large_pages && buckets != nullptr) {
#if defined(_WIN32)
        VirtualFree(buckets, 0, MEM_RELEASE);
#elif defined(__linux__)
        munmap(buckets, num_buckets * sizeof(TTBucket));
#endif
    }

    buckets = nullptr;
    using_large_pages = false;
    fallback_buckets.clear();
    fallback_buckets.shrink_to_fit();
    num_buckets = 0;
}

bool TranspositionTable::allocate_large_pages(size_t bytes) {
#if defined(_WIN32)
    void* mem = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    if (!mem) return false;
    buckets = static_cast<TTBucket*>(mem);
    using_large_pages = true;
    return true;
#elif defined(__linux__)
    void* mem = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (mem == MAP_FAILED) return false;
    buckets = static_cast<TTBucket*>(mem);
    using_large_pages = true;
    return true;
#else
    (void)bytes;
    return false;
#endif
}

void TranspositionTable::allocate_standard(size_t bucket_count) {
    fallback_buckets.clear();
    fallback_buckets.resize(bucket_count);
    buckets = fallback_buckets.data();
    using_large_pages = false;
}

void TranspositionTable::clear() {
    if (buckets && num_buckets > 0) {
        std::memset(buckets, 0, num_buckets * sizeof(TTBucket));
    }
    current_gen = 0;
}

void TranspositionTable::new_search() {
    current_gen++; // wraps 0-255 naturally
}

bool TranspositionTable::probe(Key key, TTEntry& entry) {
    size_t idx = key & (num_buckets - 1); // Power of 2 mask
    TTBucket& bucket = buckets[idx];

    for (int i = 0; i < 2; i++) {
        if (bucket.entries[i].key == key) {
            entry = bucket.entries[i];
            // Refresh generation
            bucket.entries[i].set_gen(current_gen);
            return true;
        }
    }
    return false;
}

void TranspositionTable::prefetch(Key key) const {
    size_t idx = key & (num_buckets - 1);
    __builtin_prefetch(&buckets[idx]);
}

void TranspositionTable::store(Key key, uint16_t move, int score, int eval, int depth, int bound) {
    size_t idx = key & (num_buckets - 1);
    TTBucket& bucket = buckets[idx];

    int replace_idx = -1;

    // 1. Check if key is already in bucket
    for (int i = 0; i < 2; i++) {
        if (bucket.entries[i].key == key) {
            replace_idx = i;
            break;
        }
    }

    if (replace_idx != -1) {
        // Update existing
        TTEntry& e = bucket.entries[replace_idx];

        // Replace if deeper OR entry is from different generation
        // Note: old logic was e.gen != current_gen.
        // With modular aging, we might want "if older than current".
        // But for exact replacement, strict "not equal" is usually fine to refresh.
        // Or if we want to be strict about "better" entry:
        // if (depth >= e.depth || (e.gen() != (current_gen & 0x3F)))
        // Let's stick to the logic provided: replacement if deeper or older.

        bool replace = (depth >= e.depth || e.gen() != (current_gen & 0x3F));

        e.key = key; // Redundant but safe
        e.set_gen(current_gen);

        if (replace) {
             e.update(key, move, score, eval, depth, bound, current_gen);
        }
        return;
    }

    // 2. Not found, choose victim
    int best_score = -10000;

    for (int i = 0; i < 2; i++) {
        const TTEntry& e = bucket.entries[i];

        // Age calculation using modular arithmetic
        int age = e.relative_age(current_gen & 0x3F);

        // Scoring replacement suitability
        // Higher score = better candidate to replace (victim)
        int victim_score = age * 1000;
        victim_score -= e.depth; // Deeper = keep (lower victim score)

        // Penalty for replacing exact entry?
        // "never replace EXACT with non-EXACT if depths are similar"
        // EXACT is 1.
        if (e.bound() == 1) victim_score -= 5000;

        if (victim_score > best_score) {
            best_score = victim_score;
            replace_idx = i;
        }
    }

    // Replace at replace_idx
    TTEntry& e = bucket.entries[replace_idx];
    e.update(key, move, score, eval, depth, bound, current_gen);
}

int TranspositionTable::hashfull() const {
    int sample = (num_buckets < 1000) ? num_buckets : 1000;
    if (sample == 0) return 0;
    int count = 0;
    for (int i=0; i<sample; i++) {
        if (buckets[i].entries[0].key != 0) count++;
        if (buckets[i].entries[1].key != 0) count++;
    }
    return (count * 500) / sample;
}
