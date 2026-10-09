#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace blc {

// Bounded lock-free single-producer / single-consumer ring buffer.
//
// Used to hand raw WebSocket frames from the network thread to the processing thread
// without locks. Exactly one thread may call try_push() and exactly one (other) thread
// may call try_pop().
//
// Exchange semantics: both operations *swap* the caller's object with the slot instead
// of moving into it. With T = a message holding a std::string buffer this recycles
// buffers: the producer gets back a buffer the consumer already finished with, so in
// steady state no frame causes a heap allocation.
//
// Synchronisation: head_ (consumer-owned) and tail_ (producer-owned) are monotonically
// increasing counters. The producer publishes a slot with a release-store of tail_;
// the consumer acquires it, and vice versa for freeing slots. Each side caches the
// other's index to avoid touching the shared cache line on every call.
template <typename T>
class SpscQueue {
public:
    // `capacity` is rounded up to a power of two (minimum 2).
    explicit SpscQueue(std::size_t capacity) : slots_(round_up_pow2(capacity)) {
        mask_ = slots_.size() - 1;
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer only. On success `item` receives the slot's previous value.
    bool try_push(T& item) {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - cached_head_ == slots_.size()) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail - cached_head_ == slots_.size()) return false;  // full
        }
        using std::swap;
        swap(slots_[tail & mask_], item);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer only. On success `item` holds the popped value and the slot keeps the
    // caller's previous value (for reuse by the producer).
    bool try_pop(T& item) {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        if (head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head == cached_tail_) return false;  // empty
        }
        using std::swap;
        swap(slots_[head & mask_], item);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    std::size_t capacity() const noexcept { return slots_.size(); }

    // Approximate; safe to call from any thread (for metrics only).
    std::size_t size_approx() const noexcept {
        return static_cast<std::size_t>(tail_.load(std::memory_order_relaxed) -
                                        head_.load(std::memory_order_relaxed));
    }

private:
    static constexpr std::size_t kCacheLine = 64;

    static std::size_t round_up_pow2(std::size_t n) {
        if (n > (std::size_t{1} << 30)) throw std::invalid_argument("SpscQueue capacity too large");
        std::size_t p = 2;
        while (p < n) p <<= 1;
        return p;
    }

    std::vector<T> slots_;
    std::size_t mask_ = 0;

    alignas(kCacheLine) std::atomic<std::uint64_t> head_{0};  // written by consumer
    std::uint64_t cached_tail_ = 0;                          // consumer's copy of tail_

    alignas(kCacheLine) std::atomic<std::uint64_t> tail_{0};  // written by producer
    std::uint64_t cached_head_ = 0;                          // producer's copy of head_
};

}  // namespace blc
