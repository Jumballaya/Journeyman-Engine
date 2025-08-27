#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "../async/LockFreeQueue.hpp"
#include "../logger/logging.hpp"
#include "Event.hpp"

class EventBus {
 public:
  using EventHandle = uint64_t;

  explicit EventBus(size_t capacity = 8192)
      : _queue(capacity), _pendingUnsub(capacity) {}

  template <typename T>
  EventHandle subscribe(std::function<void(const T&)> fn) {
    std::lock_guard lk(_subMutex);
    const auto key = std::type_index(typeid(T));
    const EventHandle tok = ++_nextToken;
    _typed[key].push_back({tok, [f = std::move(fn)](const Event& e) {
                             f(std::get<T>(e));
                           }});
    return tok;
  }

  void unsubscribe(EventHandle tok);

  // @TODO: Throttle the emit based on an event-by-event throttle amount in ms
  template <typename T>
  void emit(const T& e) {
    _queue.try_enqueue(Event{e});
  }

  void dispatch(size_t maxEvents = SIZE_MAX);

  void shutdown() {
    _queue.shutdown();
    _pendingUnsub.shutdown();
  }

 private:
  struct TypedSub {
    EventHandle tok;
    std::function<void(const Event&)> fn;
  };

  void eraseToken(EventHandle tok);

  // queues
  LockFreeQueue<Event> _queue;
  LockFreeQueue<EventHandle> _pendingUnsub;

  // subscribers
  std::mutex _subMutex;
  std::unordered_map<std::type_index, std::vector<TypedSub>> _typed;

  EventHandle _nextToken = 0;
};
