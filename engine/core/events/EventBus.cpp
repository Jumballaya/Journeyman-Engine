#include "EventBus.hpp"

#include <utility>

void EventBus::unsubscribe(EventHandle tok) {
  if (!_pendingUnsub.try_enqueue(std::move(tok))) {
    JM_LOG_WARN("[Event Bus] Pending unsub queue is full");
  }
}

void EventBus::dispatch(size_t maxEvents) {
  Event e;
  size_t count = 0;

  while (count < maxEvents && _queue.try_dequeue(e)) {
    std::visit([&](auto&& ev) {
      using T = std::decay_t<decltype(ev)>;
      if constexpr (!std::is_same_v<T, events::DynamicEvent>) {
        std::lock_guard lk(_subMutex);
        auto it = _typed.find(std::type_index(typeid(T)));
        if (it == _typed.end()) return;
        auto subs = it->second;
        for (auto& s : subs) s.fn(e);
      }
    },
               e);

    ++count;
  }

  EventHandle tok;
  std::vector<EventHandle> batch;
  while (_pendingUnsub.try_dequeue(tok)) {
    batch.push_back(tok);
  }
  if (!batch.empty()) {
    std::lock_guard lk(_subMutex);
    for (auto t : batch) eraseToken(t);
  }
}

void EventBus::eraseToken(EventHandle tok) {
  for (auto it = _typed.begin(); it != _typed.end(); ++it) {
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(), [&](auto& s) { return s.tok == tok; }), vec.end());
  }
}