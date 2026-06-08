// =============================================================================
// systems/EventLog.cpp
//
// Purpose:
//   Definitions for the EventLog class declared in systems/EventLog.h.
//   This file contains all linked-list node management: allocation on append,
//   eviction on overflow, and deallocation in the destructor.
//
//   This is the graded LINKED LIST showcase (R6.3): every node is a raw
//   Node* allocated with `new` and freed with `delete`. std::list, std::deque,
//   and any other standard linked container are deliberately NOT used here.
//
// Layer: systems (no game-layer dependencies; only <string> / <vector>).
// =============================================================================
#include "systems/EventLog.h"

#include <algorithm> // std::max - used to clamp the recent() count request.

namespace dga {

// ---- Construction and destruction ------------------------------------------

// Build an empty list. head_ and tail_ are null (nothing allocated yet); size_
// starts at 0. We store the capacity so append can consult it for eviction.
EventLog::EventLog(int capacity)
    : head_(nullptr), tail_(nullptr), size_(0), capacity_(capacity) {}

// Walk from head to tail, deleting each node in turn. We keep a local pointer
// (`current`) to the node we are about to delete and advance it to `next` BEFORE
// the delete so the freed memory is never dereferenced again (R6.3).
EventLog::~EventLog() {
    Node* current = head_;
    while (current != nullptr) {
        Node* next = current->next; // Save the successor before freeing.
        delete current;             // Release this node's memory.
        current = next;             // Advance to the saved successor.
    }
    // head_ and tail_ are now dangling, but the EventLog is being destroyed so
    // they will never be used again. Setting them to nullptr here is good practice
    // and guards against accidental use-after-free in subclass destructors.
    head_ = nullptr;
    tail_ = nullptr;
}

// ---- Core operations -------------------------------------------------------

// Append a message to the tail of the linked list.
//
// Step 1 — evict while over capacity. We check BEFORE linking the new node so
//           the invariant size_ <= capacity_ holds after the call. This is
//           important: we want to keep at most capacity_ messages, so we must
//           evict the oldest before inserting the newest.
// Step 2 — allocate a new node and link it.
// Step 3 — update tail_ (and head_ if the list was empty).
void EventLog::append(const std::string& message) {
    // Evict the head (oldest) message if we are already at or above capacity.
    // Using >= here means "no room for the new message yet", so we clear space
    // first. This keeps size_ strictly less than capacity_ going into the
    // allocation, then the new node brings it exactly to capacity_.
    while (size_ >= capacity_) {
        evictHead();
    }

    // Allocate the new tail node. The Node constructor sets next to nullptr.
    Node* newNode = new Node(message);

    if (tail_ == nullptr) {
        // The list was empty: the new node is both head and tail.
        head_ = newNode;
        tail_ = newNode;
    } else {
        // Link the current tail forward to the new node, then advance tail_.
        tail_->next = newNode;
        tail_       = newNode;
    }

    ++size_;
}

// Return the most recent `count` messages in chronological order.
//
// Because the list is singly-linked from oldest to newest, and "recent" means
// "newest", we need to skip the first (size_ - count) nodes and then collect
// the rest. We do this in a single forward pass: compute the skip count, walk
// past those nodes, then collect everything that follows (R29.3).
std::vector<std::string> EventLog::recent(int count) const {
    // Clamp: never return more than what is stored.
    const int available = (count < size_) ? count : size_;
    if (available <= 0) {
        return {};
    }

    // How many nodes to skip from the head (they are older than wanted).
    const int skipCount = size_ - available;

    // Walk past the nodes we do not want.
    Node* current = head_;
    for (int skipped = 0; skipped < skipCount && current != nullptr; ++skipped) {
        current = current->next;
    }

    // Collect the remaining nodes into the output vector.
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(available));
    while (current != nullptr) {
        result.push_back(current->message);
        current = current->next;
    }

    return result;
}

// ---- Queries ---------------------------------------------------------------

int EventLog::size() const {
    return size_;
}

int EventLog::capacity() const {
    return capacity_;
}

// Drop every stored message in one shot. Walks the linked list head-to-tail
// and frees each node, then zeroes head_, tail_ and size_ so the EventLog is
// in the same state a freshly-constructed instance would have. The capacity
// stays untouched.
void EventLog::clear() {
    Node* current = head_;
    while (current != nullptr) {
        Node* next = current->next;
        delete current;
        current = next;
    }
    head_ = nullptr;
    tail_ = nullptr;
    size_ = 0;
}

// ---- Private helpers -------------------------------------------------------

// Remove the head node, free its memory, and update head_/size_.
// If the list becomes empty after the eviction, tail_ is also reset to nullptr.
void EventLog::evictHead() {
    if (head_ == nullptr) {
        // Nothing to evict; this guard is a safety net for callers.
        return;
    }

    Node* toDelete = head_;     // Remember the node we are about to free.
    head_          = head_->next; // Advance head to the next (newer) node.

    if (head_ == nullptr) {
        // The list is now empty: tail_ would point at freed memory otherwise.
        tail_ = nullptr;
    }

    delete toDelete; // Release the node's memory (R6.3).
    --size_;
}

} // namespace dga
