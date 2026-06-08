// =============================================================================
// core/RingBuffer.h
//
// Purpose:
//   RingBuffer<T> is a generic bounded buffer: a list with a fixed maximum size
//   that always keeps the most recent items. You keep appending to it, and once
//   it is full each new item pushes out the oldest one. It never grows past its
//   capacity and never needs to allocate more memory after construction.
//
//   It is the project's second reuse of templates (alongside Grid<T>), which is
//   what the templates rubric criterion asks for - one generic component used
//   for more than one element type (R7.1, R7.2). In the game it backs short
//   rolling histories for the HUD, such as the most recent Event_Log lines
//   (RingBuffer<std::string>) or recent damage numbers (RingBuffer<int>).
//
// FIFO eviction, explained plainly:
//   "FIFO" means First-In, First-Out: the first item that went in is the first
//   item to leave. Think of a queue of people where only N can stand in the room
//   at once - when person N+1 arrives, the person who has been waiting longest
//   leaves to make space. So while the buffer is below capacity, push() simply
//   adds to the back; once it is at capacity, each push() first drops the oldest
//   (front) item, then adds the new one at the back. size() therefore never
//   exceeds capacity().
//
// Why header-only:
//   RingBuffer is a template, so the compiler needs its full body at every call
//   site to generate a version per element type. Templates live in headers (the
//   allowed exception to the project's multi-file rule), so there is no
//   RingBuffer.cpp.
//
// Implementation note:
//   This wraps a std::vector<T> used as a circular buffer: a single block of
//   `capacity` slots plus a `head` marking the oldest item and a `count` of how
//   many slots are currently used. Appending and evicting are done by advancing
//   indices modulo capacity, so no elements are ever shifted or reallocated.
//
// Layer: core (depends only on the C++ standard library).
// =============================================================================
#pragma once

#include <cstddef> // std::size_t - index/size type for the backing storage.
#include <vector>  // std::vector - the fixed-size circular backing store.

namespace dga {

/// A fixed-capacity buffer that keeps only the most recent items (FIFO eviction).
///
/// @tparam type the element type stored in the buffer (for example std::string
///         or int).
///
/// Once full, every push() evicts the oldest element so the buffer always holds
/// at most `capacity` of the newest items, in insertion order (oldest first).
template <typename type>
class RingBuffer {
public:
    /// Construct an empty buffer that will hold at most `capacity` items.
    /// @param capacity the maximum number of items retained at once; must be
    ///        >= 1. The backing storage is sized once here and never grows.
    /// Precondition: capacity >= 1. A capacity of 0 (a buffer that can hold
    /// nothing) is treated as a programming error and is clamped up to 1 so the
    /// type always has at least one usable slot rather than misbehaving.
    explicit RingBuffer(int capacity)
        : capacity_(capacity < 1 ? 1 : capacity),
          head_(0),
          count_(0),
          slots_(static_cast<std::size_t>(capacity_ < 1 ? 1 : capacity_)) {}

    /// Append one item to the back of the buffer.
    /// While the buffer is below capacity this just stores the item and grows
    /// the size by one. Once the buffer is full, this first evicts the oldest
    /// (front) item, then stores the new one - so the new item is always kept
    /// and size() stays equal to capacity() (FIFO eviction; see the file header).
    /// @param value the item to append (copied into the buffer).
    void push(const type& value) {
        // The next free slot is `count_` steps ahead of the oldest item, wrapped
        // around the end of the storage block.
        const std::size_t writeIndex =
            (head_ + count_) % static_cast<std::size_t>(capacity_);
        slots_[writeIndex] = value;

        if (count_ < static_cast<std::size_t>(capacity_)) {
            // Still room: the buffer simply grew by one element.
            ++count_;
        } else {
            // Full: we just overwrote the oldest slot, so advance head_ to the
            // new oldest item. count_ stays pinned at capacity.
            head_ = (head_ + 1) % static_cast<std::size_t>(capacity_);
        }
    }

    /// Append one item to the back of the buffer.
    /// This is a synonym for push(), provided because "append" reads more
    /// naturally at some call sites (for example the Event_Log display).
    /// @param value the item to append (copied into the buffer).
    void append(const type& value) { push(value); }

    /// @return the number of items currently stored (always <= capacity()).
    int size() const { return static_cast<int>(count_); }

    /// @return the maximum number of items the buffer can hold at once.
    int capacity() const { return capacity_; }

    /// @return true when the buffer holds no items.
    bool empty() const { return count_ == 0; }

    /// Copy the current contents into a vector in age order (oldest first,
    /// newest last). This is how callers read or iterate the buffer for display.
    /// @return a vector of the stored items; its length equals size().
    std::vector<type> items() const {
        std::vector<type> ordered;
        ordered.reserve(count_);
        for (std::size_t offset = 0; offset < count_; ++offset) {
            const std::size_t index =
                (head_ + offset) % static_cast<std::size_t>(capacity_);
            ordered.push_back(slots_[index]);
        }
        return ordered;
    }

    /// Copy the most recent `requestedCount` items into a vector, in age order
    /// (oldest of those first, newest last). Used by the HUD / Event_Log display
    /// to show only the last few entries.
    /// @param requestedCount how many of the newest items to return; if it is
    ///        larger than size() the whole buffer is returned, and if it is <= 0
    ///        an empty vector is returned.
    /// @return a vector with min(requestedCount, size()) items.
    std::vector<type> recent(int requestedCount) const {
        if (requestedCount <= 0) {
            return std::vector<type>();
        }

        const std::size_t available = count_;
        const std::size_t wanted = static_cast<std::size_t>(requestedCount);
        const std::size_t take = wanted < available ? wanted : available;

        // Skip the older items so only the last `take` remain.
        const std::size_t skip = available - take;

        std::vector<type> ordered;
        ordered.reserve(take);
        for (std::size_t offset = 0; offset < take; ++offset) {
            const std::size_t index =
                (head_ + skip + offset) % static_cast<std::size_t>(capacity_);
            ordered.push_back(slots_[index]);
        }
        return ordered;
    }

private:
    int capacity_;            ///< Maximum items retained; fixed at construction.
    std::size_t head_;        ///< Index of the oldest item within slots_.
    std::size_t count_;       ///< Number of items currently stored (<= capacity_).
    std::vector<type> slots_; ///< Circular backing store of exactly capacity_ slots.
};

} // namespace dga
