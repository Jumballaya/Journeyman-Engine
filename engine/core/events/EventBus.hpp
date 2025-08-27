#pragma once
#include <algorithm>
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
#include "../logger/logging.hpp"
#include "EventType.hpp"

constexpr size_t JM_EVENT_INLINE = 64;

// Use a small inline payload, typically a POD struct
// representing the Event data
// The purpose of this is to avoid heap-allocating
// events.
struct InlineEvent {
  EventType type{};
  uint16_t size{};
  uint16_t _pad{};
  alignas(16) std::byte data[JM_EVENT_INLINE];
};

class EventBus {
 public:
  using EventHandle = uint64_t;

  explicit EventBus(size_t capacity = 8192) : _queue(capacity) {}

  template <class T, class F>
  EventHandle subscribe(EventType type, F&& fn) {
    static_assert(std::is_trivially_copyable_v<T>, "Event T must be trivially copyable");
    static_assert(sizeof(T) <= JM_EVENT_INLINE, "Event T exceeds 64B inline capacity");

    std::lock_guard lk(_subMutex);
    EventHandle handle = ++_next;
    _byType[type].push_back(Sub{
        handle,
        [cb = std::function<void(const T&)>(std::forward<F>(fn))](const void* p, uint16_t) {
          cb(*static_cast<const T*>(p));
        }});
    _byHandle[handle] = type;
    return handle;
  }

  void unsubscribe(EventHandle tok);

  // @TODO: Throttle the emit based on an event-by-event throttle amount in ms
  template <class T>
  void emit(EventType type, const T& ev) {
    static_assert(std::is_trivially_copyable_v<T>, "Event T must be trivially copyable");
    static_assert(sizeof(T) <= JM_EVENT_INLINE, "Event T exceeds 64B inline capacity");
    InlineEvent e;
    e.type = type;
    e.size = static_cast<uint16_t>(sizeof(T));
    std::memcpy(e.data, &ev, sizeof(T));
    if (!_queue.try_enqueue(std::move(e))) _dropped.fetch_add(1, std::memory_order_relaxed);
  }

  void dispatch(size_t maxEvents = SIZE_MAX);

  void shutdown() {
    _queue.shutdown();
  }

  uint64_t dropped() const noexcept { return _dropped.load(std::memory_order_relaxed); }

 private:
  struct Sub {
    EventHandle handle;
    std::function<void(const void*, uint16_t)> fn;
  };

  LockFreeQueue<InlineEvent> _queue;

  std::mutex _subMutex;
  std::unordered_map<EventType, std::vector<Sub>> _byType;
  std::unordered_map<EventHandle, EventType> _byHandle;

  std::atomic<uint64_t> _dropped{0};
  std::atomic<EventHandle> _next{0};
};
