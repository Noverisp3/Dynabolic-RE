#ifndef OBJECT_POOL_HPP
#define OBJECT_POOL_HPP

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace dynabolic {

// Generic chunked registry indexed by a 32-bit Id.
//
// Storage is std::deque<std::weak_ptr<T>>: deque guarantees stable element
// addresses across growth (no relocation), and the chunk-of-pointers layout
// keeps the registry from being a random-address probe per element. Ids
// (uint32_t) are pool-local indices, so a hot-path traversal can store 4-byte
// Ids in adjacency lists instead of 16-byte shared_ptrs (~4x denser
// per cache line).
//
// The pool stores weak_ptrs — it is a *registry*, not an owner. Real
// ownership stays with the engine's nodes_/links_ maps and any shared_ptrs
// returned from the public API. When the last strong reference goes away the
// registry slot's weak_ptr expires and get() returns null. This preserves
// existing shared_ptr ownership semantics while giving the engine an O(1)
// index-based lookup path.
//
// A free-list reuses Ids returned via release(). Reused slots are overwritten
// with a fresh weak_ptr.
template <typename T>
class ObjectPool {
public:
    using Id = uint32_t;
    static constexpr Id INVALID_ID = 0xFFFFFFFFu;

    // Register an object and return a fresh pool Id. If you already have a
    // numeric handle (e.g. GraphNode::getNumericId()), prefer registerWithId
    // so the Id matches.
    Id registerObject(const std::shared_ptr<T>& obj) {
        std::unique_lock<std::shared_mutex> lk(mtx_);
        Id id;
        if (!free_list_.empty()) {
            id = free_list_.back();
            free_list_.pop_back();
            slots_[id] = obj;
        } else {
            id = static_cast<Id>(slots_.size());
            slots_.emplace_back(obj);
        }
        return id;
    }

    // Register an object at a specific Id (grow the slot vector if needed).
    // Used to align the pool index with GraphNode/GraphLink::numeric_id_.
    // Idempotent: re-registering the same Id with the same object is a no-op.
    void registerWithId(Id id, const std::shared_ptr<T>& obj) {
        std::unique_lock<std::shared_mutex> lk(mtx_);
        if (id >= slots_.size()) {
            slots_.resize(static_cast<std::size_t>(id) + 1);
        }
        slots_[id] = obj;
    }

    // O(1) lookup. Returns null if the slot is empty, the Id is out of range,
    // or the registered object has been destroyed.
    std::shared_ptr<T> get(Id id) const {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        if (id >= slots_.size()) return nullptr;
        return slots_[id].lock();
    }

    // Drop the slot and recycle the Id.
    void release(Id id) {
        std::unique_lock<std::shared_mutex> lk(mtx_);
        if (id >= slots_.size()) return;
        slots_[id].reset();
        free_list_.push_back(id);
    }

    // Approximate (live + expired) slot count. O(slots) if you actually
    // care about live-only count.
    std::size_t capacity() const {
        std::shared_lock<std::shared_mutex> lk(mtx_);
        return slots_.size();
    }

private:
    // std::deque keeps element addresses stable on growth (chunked storage).
    mutable std::shared_mutex mtx_;
    std::deque<std::weak_ptr<T>> slots_;
    std::vector<Id> free_list_;
};

}  // namespace dynabolic

#endif  // OBJECT_POOL_HPP
