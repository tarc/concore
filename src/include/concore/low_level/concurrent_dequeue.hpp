/**
 * @file    concurrent_dequeue.hpp
 * @brief   Definition of @ref concore::v1::concurrent_dequeue "concurrent_dequeue"
 *
 * @see     @ref concore::v1::concurrent_dequeue "concurrent_dequeue"
 */
#pragma once

#include "spin_backoff.hpp"

#include <atomic>
#include <vector>
#include <deque>
#include <mutex>
#include <cassert>

namespace concore {

namespace detail {

//! Bounded deque implementation, to be used if we the number of elements are relatively low.
template <typename T>
struct bounded_dequeue {
    //! The state of an item in the fast queue
    enum class item_state {
        freed,
        constructing,
        valid,
        destructing,
    };
    //! We need to add an atomic to the element, to know the state of each element.
    struct wrapped_elem { // NOLINT(cppcoreguidelines-pro-type-member-init)
        //! Indicates that the 'elem_' is considered part of the queue, and can be popped.
        //! This may be true while trying to move away data from it.
        //! While constructing the element this is false.
        std::atomic<int> state_{0};
        //! The element that we store in our queue
        T elem_;
    };
    //! The size of the queue with fast access
    const uint16_t size_;
    //! Circular buffer that contains the elements
    std::vector<wrapped_elem> circular_buffer_;
    //! The range of elements valid in our circular buffer
    std::atomic<uint32_t> fast_range_{0};

    //! Union for the [start, end) range in the fast vector, used to access the atomic
    union fast_range {
        uint32_t int_value;
        struct {
            uint16_t start; //!< The start index in our fast queue
            uint16_t end;   //!< The end index in our fast queue
        };
    };

    bounded_dequeue(size_t size)
        : size_(static_cast<uint16_t>(size))
        , circular_buffer_(size) {}

    //! Reserve one slot at the back of the fast queue. Yields the position of the reserved item.
    //! Returns false if we don't have enough room to add new elements.
    bool reserve_back(uint16_t& pos) {
        const auto max_dist = static_cast<uint16_t>(size_ - 3);
        fast_range old{}, desired{};
        old.int_value = fast_range_.load(std::memory_order_relaxed);
        while (true) {
            if (uint16_t(old.end - old.start) > max_dist)
                return false;
            desired = old;
            desired.end++;
            if (fast_range_.compare_exchange_weak(old.int_value, desired.int_value,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                pos = old.end;
                return true;
            }
        }
    }
    //! Reserve one slot at the front of the fast queue. Yields the position of the reserved item.
    //! Returns false if we don't have enough room to add new elements.
    bool reserve_front(uint16_t& pos) {
        const auto max_dist = static_cast<uint16_t>(size_ - 3);
        fast_range old{}, desired{};
        old.int_value = fast_range_.load(std::memory_order_relaxed);
        while (true) {
            if (uint16_t(old.end - old.start) > max_dist)
                return false;
            desired = old;
            desired.start--;
            if (fast_range_.compare_exchange_weak(old.int_value, desired.int_value,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                pos = desired.start;
                return true;
            }
        }
    }
    //! Consumes one slot at the front of the fast queue. Yields the position of the consumed item.
    bool consume_front(uint16_t& pos) {
        fast_range old{}, desired{};
        old.int_value = fast_range_.load(std::memory_order_relaxed);
        while (true) {
            if (old.start == old.end)
                return false;
            desired = old;
            desired.start++;
            if (fast_range_.compare_exchange_weak(old.int_value, desired.int_value,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                pos = old.start;
                return true;
            }
        }
    }
    //! Consumes one slot at the front of the fast queue. Yields the position of the reserved item.
    bool consume_back(uint16_t& pos) {
        fast_range old{}, desired{};
        old.int_value = fast_range_.load(std::memory_order_relaxed);
        while (true) {
            if (old.start == old.end)
                return false;
            desired = old;
            desired.end--;
            if (fast_range_.compare_exchange_weak(old.int_value, desired.int_value,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                pos = desired.end;
                return true;
            }
        }
    }

    //! Construct an element in the fast queue.
    //! The place at position 'pos' is already reserved
    void construct_in_fast(uint16_t pos, T&& elem) {
        wrapped_elem& item = circular_buffer_[pos % size_];

        // Typically, the item is not being used by anybody else; but in rare conditions it might
        // not have finished to be destructed. In that case, wait for it to become free.
        int old = static_cast<int>(item_state::freed);
        int desired = static_cast<int>(item_state::constructing);
        spin_backoff spinner;
        while (!item.state_.compare_exchange_strong(
                old, desired, std::memory_order_acq_rel, std::memory_order_relaxed))
            spinner.pause();

        // Ok. Now we can finally construct the element
        item.elem_ = std::move(elem);
        item.state_.store(static_cast<int>(item_state::valid), std::memory_order_release);
    }

    //! Extract an element from the fast queue.
    //! The place at position 'pos' is already marked as free
    void extract_from_fast(uint16_t pos, T& elem) {
        wrapped_elem& item = circular_buffer_[pos % size_];

        // Typically, the item is valid; but in rare conditions it might not have finished to be
        // constructed. In that case, wait for it to become valid, before getting the data out of
        // it.
        spin_backoff spinner;
        int old = static_cast<int>(item_state::valid);
        int desired = static_cast<int>(item_state::destructing);
        while (!item.state_.compare_exchange_strong(
                old, desired, std::memory_order_acq_rel, std::memory_order_relaxed))
            spinner.pause();

        // Ok. Now we can finally pop the element
        elem = std::move(item.elem_);
        item.state_.store(static_cast<int>(item_state::freed), std::memory_order_release);
    }
};

} // namespace detail

inline namespace v1 {

/**
 * @brief      Concurrent double-ended queue implementation, for a small number of elements.
 *
 * @tparam     T     The type of elements to store
 *
 * @details
 *
 * This will try to preallocate a vector with enough elements to cover the most common cases.
 * Operations on the concurrent queue when we have few elements are fast: we only make atomic
 * operations, no memory allocation. We only use spin mutexes in this case.
 *
 * If we have too many elements in the queue, we switch to a slower implementation that can grow to
 * a very large number of elements. For this we use regular mutexes.
 *
 * Note 1: when switching between fast and slow, the FIFO ordering of the queue is lost.
 *
 * Note 2: for efficiency reasons, the element size should be at least as a cache line (otherwise we
 * can have false sharing when accessing adjacent elements)
 *
 * Note 3: we expect very-low contention on the front of the queue, and some contention at the end
 * of the queue. And of course, there will be more contention when the queue is empty or close to
 * empty.
 *
 * Note 4: we expect contention over the atomic that stores the begin/end position in the fast queue
 *
 * The intent of this queue is to hold tasks in the task system. There, we typically add any
 * enqueued tasks to the end of the queue. The tasks that are spawned while working on some task are
 * pushed to the front of the queue. The popping of the tasks is typically done on the front of the
 * queue, but when stealing tasks, popping is done from the back of the queue -- trying to maximize
 * locality for nearby tasks.
 */
template <typename T>
class concurrent_dequeue {
public:
    //! The value type stored in the concurrent dequeue
    using value_type = T;

    /**
     * @brief      Constructs a new instance of the queue, with the given preallocated size.
     *
     * @param      expected_size  How many elements to preallocate in our fast queue.
     *
     * If we ever add more elements in our queue than the given limit, our queue starts to become
     * slower.
     *
     * The number of reserved elements should be bigger than the expected concurrency.
     */
    explicit concurrent_dequeue(size_t expected_size);

    //! Pushes one element in the back of the queue.
    //! This is considered the default pushing operation.
    void push_back(T&& elem);

    //! Push one element to the front of the queue.
    void push_front(T&& elem);

    //! Try to pop one element from the front of the queue. Returns false if the queue is empty.
    //! This is considered the default popping operation.
    bool try_pop_front(T& elem);

    //! Try to pop one element from the back of the queue. Returns false if the queue is empty.
    bool try_pop_back(T& elem);

    //! Clears the queue
    void unsafe_clear();

private:
    //! The fast dequeue implementation; uses a fixed number of elements.
    detail::bounded_dequeue<T> fast_deque_;

    //! Deque of elements that have slow access; we use this if we go beyond our threshold
    std::deque<T> slow_access_elems_;
    //! Protects the access to slow_access_elems_
    std::mutex bottleneck_;
    //! The number of elements stored in slow_access_elems_; used it before trying to take the lock
    std::atomic<int> num_elements_slow_{0};
};

template <typename T>
inline concurrent_dequeue<T>::concurrent_dequeue(size_t expected_size)
    : fast_deque_(expected_size) {}

template <typename T>
inline void concurrent_dequeue<T>::push_back(T&& elem) {
    // If we have enough space in the 'fast' queue, construct the element there
    uint16_t pos{0};
    if (fast_deque_.reserve_back(pos)) {
        fast_deque_.construct_in_fast(pos, std::forward<T>(elem));
    } else {
        // Otherwise take the slow approach
        std::lock_guard<std::mutex> lock{bottleneck_};
        num_elements_slow_++;
        slow_access_elems_.push_back(std::forward<T>(elem));
    }
}

template <typename T>
inline void concurrent_dequeue<T>::push_front(T&& elem) {
    // If we have enough space in the 'fast' queue, construct the element there
    uint16_t pos{0};
    if (fast_deque_.reserve_front(pos)) {
        fast_deque_.construct_in_fast(pos, std::forward<T>(elem));
    } else {
        // Otherwise take the slow approach
        std::lock_guard<std::mutex> lock{bottleneck_};
        num_elements_slow_++;
        slow_access_elems_.push_front(std::forward<T>(elem));
    }
}

template <typename T>
inline bool concurrent_dequeue<T>::try_pop_front(T& elem) {
    // If we can extract one element from the fast queue, move an element from there
    uint16_t pos{0};
    if (fast_deque_.consume_front(pos)) {
        fast_deque_.extract_from_fast(pos, elem);
        return true;
    } else if (num_elements_slow_.load() > 0) {
        // Otherwise take the slow approach
        std::lock_guard<std::mutex> lock{bottleneck_};
        if (slow_access_elems_.empty())
            return false;
        num_elements_slow_--;
        elem = std::move(slow_access_elems_.front());
        slow_access_elems_.pop_front();
        return true;
    } else
        return false;
}

template <typename T>
inline bool concurrent_dequeue<T>::try_pop_back(T& elem) {
    // If we can extract one element from the fast queue, move an element from there
    uint16_t pos{0};
    if (fast_deque_.consume_back(pos)) {
        fast_deque_.extract_from_fast(pos, elem);
        return true;
    } else if (num_elements_slow_.load() > 0) {
        // Otherwise take the slow approach
        std::lock_guard<std::mutex> lock{bottleneck_};
        if (slow_access_elems_.empty())
            return false;
        num_elements_slow_--;
        elem = std::move(slow_access_elems_.back());
        slow_access_elems_.pop_back();
        return true;
    } else
        return false;
}

template <typename T>
inline void concurrent_dequeue<T>::unsafe_clear() {
    fast_deque_.fast_range_ = 0;
    for (auto& el : fast_deque_.circular_buffer_)
        el.state_ = 0;
    slow_access_elems_.clear();
    num_elements_slow_ = 0;
}

} // namespace v1
} // namespace concore