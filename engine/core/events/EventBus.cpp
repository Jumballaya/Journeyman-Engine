#include "EventBus.hpp"

#include <algorithm>

void EventBus::unsubscribe(EventHandle handle) {
  std::lock_guard lk(_subMutex);
  for (auto it = _byType.begin(); it != _byType.end(); ++it) {
    auto& subs = it->second;
    auto sub = std::find_if(subs.begin(), subs.end(), [&](const Sub& s) { return s.handle == handle; });
    if (sub == subs.end()) continue;
    subs.erase(sub);
    if (subs.empty()) _byType.erase(it);
    return;
  }
}

void EventBus::dispatch(size_t maxEvents) {
  Queued e;
  for (size_t n = 0; n < maxEvents && _queue.try_dequeue(e); ++n) {
    std::vector<Sub> subs;
    {
      std::lock_guard lk(_subMutex);
      if (auto it = _byType.find(e.type); it != _byType.end()) subs = it->second;  // a handler may unsubscribe
    }
    for (auto& s : subs) s.fn(e.data);
  }
}
