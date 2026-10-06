#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "../async/LockFreeQueue.hpp"
#include "EventType.hpp"

// Events are POD structs copied inline into the queue, so emitting never allocates.
inline constexpr size_t kMaxEventSize = 64;
template <class T>
concept InlineEventPayload = std::is_trivially_copyable_v<T> && sizeof(T) <= kMaxEventSize && alignof(T) <= 16;

// Any thread emits; dispatch (main thread) delivers queued events to their
// subscribers. Events past the queue's capacity are dropped and counted.
class EventBus {
 public:
  using EventHandle = uint64_t;

  explicit EventBus(size_t capacity = 8192) : _queue(capacity) {}

  template <InlineEventPayload T, class F>
  EventHandle subscribe(EventType type, F&& fn) {
    std::lock_guard lk(_subMutex);
    const EventHandle handle = ++_lastHandle;
    _byType[type].push_back(
        Sub{handle, [fn = std::forward<F>(fn)](const void* p) mutable { fn(*static_cast<const T*>(p)); }});
    return handle;
  }

  // A stale or unknown handle is ignored.
  void unsubscribe(EventHandle handle);

  template <InlineEventPayload T>
  void emit(EventType type, const T& ev) {
    Queued e;
    e.type = type;
    std::memcpy(e.data, &ev, sizeof(T));
    if (!_queue.try_enqueue(std::move(e))) _dropped.fetch_add(1, std::memory_order_relaxed);
  }

  void dispatch(size_t maxEvents = SIZE_MAX);

  uint64_t dropped() const noexcept { return _dropped.load(std::memory_order_relaxed); }

 private:
  struct Queued {
    EventType type{};
    alignas(16) std::byte data[kMaxEventSize];
  };
  struct Sub {
    EventHandle handle;
    std::function<void(const void*)> fn;
  };

  LockFreeQueue<Queued> _queue;

  std::mutex _subMutex;
  std::unordered_map<EventType, std::vector<Sub>> _byType;
  EventHandle _lastHandle = 0;

  std::atomic<uint64_t> _dropped{0};
};
