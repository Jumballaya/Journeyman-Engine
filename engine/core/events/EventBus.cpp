#include "EventBus.hpp"

#include <utility>

void EventBus::unsubscribe(Token tok) {
  if (_pendingUnsub.try_enqueue(std::move(tok))) {
    _stats.unsubDeferred.fetch_add(1, std::memory_order_relaxed);
  } else {
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
        dispatchTyped<T>(e);
      } else {
        dispatchDynamic(ev);
      }
    },
               e);

    _stats.dispatched.fetch_add(1, std::memory_order_relaxed);
    ++count;
  }

  Token tok;
  std::vector<Token> batch;
  while (_pendingUnsub.try_dequeue(tok)) batch.push_back(tok);
  if (!batch.empty()) {
    std::lock_guard lk(_subMutex);
    for (auto t : batch) eraseToken(t);
  }
}

void EventBus::eraseToken(Token tok) {
  // typed
  for (auto it = _typed.begin(); it != _typed.end(); ++it) {
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](auto& s) { return s.tok == tok; }),
              vec.end());
  }
  // dynamic
  for (auto it = _dyn.begin(); it != _dyn.end(); ++it) {
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](auto& s) { return s.tok == tok; }),
              vec.end());
  }
}