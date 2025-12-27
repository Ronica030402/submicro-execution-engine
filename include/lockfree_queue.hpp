#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace hft {

template<typename T, size_t N>
class LockFreeQueue {
    static_assert((N & (N - 1)) == 0, "size must be power of 2");

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    
    // Aligned storage to avoid UB with non-trivial types
    struct Node {
        alignas(T) char data[sizeof(T)];
    };
    Node buf_[N];

public:
    LockFreeQueue() = default;
    
    ~LockFreeQueue() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            T item;
            while (pop(item)) {}
        }
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    bool push(const T& item) {
        auto t = tail_.load(std::memory_order_relaxed);
        auto next_t = (t + 1) & (N - 1);

        if (next_t == head_.load(std::memory_order_acquire))
            return false;

        new (&buf_[t].data) T(item);
        tail_.store(next_t, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        auto h = head_.load(std::memory_order_relaxed);

        if (h == tail_.load(std::memory_order_acquire))
            return false;

        T* ptr = reinterpret_cast<T*>(&buf_[h].data);
        item = std::move(*ptr);
        ptr->~T();
        
        head_.store((h + 1) & (N - 1), std::memory_order_release);
        return true;
    }

    bool peek(T& item) const {
        auto h = head_.load(std::memory_order_acquire);

        if (h == tail_.load(std::memory_order_acquire))
            return false;

        item = *reinterpret_cast<const T*>(&buf_[h].data);
        return true;
    }

    const T* peek() const {
        auto h = head_.load(std::memory_order_acquire);

        if (h == tail_.load(std::memory_order_acquire))
            return nullptr;

        return reinterpret_cast<const T*>(&buf_[h].data);
    }

    template<typename... Args>
    bool emplace(Args&&... args) {
        auto t = tail_.load(std::memory_order_relaxed);
        auto next_t = (t + 1) & (N - 1);

        if (next_t == head_.load(std::memory_order_acquire))
            return false;

        new (&buf_[t].data) T(std::forward<Args>(args)...);
        tail_.store(next_t, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    size_t size() const {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_acquire);
        return (t - h) & (N - 1);
    }

    size_t capacity() const { return N - 1; }
};

// Alias for compatibility
template<typename T, size_t N>
using SPSCQueue = LockFreeQueue<T, N>;

}