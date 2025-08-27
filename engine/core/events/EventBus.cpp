#include "EventBus.hpp"

#include <utility>

void EventBus::unsubscribe(EventHandle handle) {
  std::lock_guard lk(_subMutex);
  auto itH = _byHandle.find(handle);
  if (itH == _byHandle.end()) return;
  const EventType t = itH->second;
  if (auto it = _byType.find(t); it != _byType.end()) {
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const Sub& s) { return s.handle == handle; }),
            v.end());
    if (v.empty()) _byType.erase(it);
  }
  _byHandle.erase(itH);
}

//
//  This is ran on the main thread to drain the events and run them
//
void EventBus::dispatch(size_t maxEvents) {
  InlineEvent e;
  size_t n = 0;
  while (n < maxEvents && _queue.try_dequeue(e)) {
    std::vector<Sub> subs;
    {
      std::lock_guard lk(_subMutex);
      if (auto it = _byType.find(e.type); it != _byType.end())
        subs = it->second;  // copy so unsub in handler is safe
    }
    for (auto& s : subs) s.fn(e.data, e.size);
    ++n;
  }
}