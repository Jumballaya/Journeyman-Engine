#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>

#include "EventBus.hpp"
#include "EventType.hpp"

namespace {

struct PingEvent {
  int value;
};

struct MultiField {
  int i;
  float f;
  uint64_t x;
};

constexpr EventType kPing = createEventType("ping");
constexpr EventType kAlpha = createEventType("alpha");
constexpr EventType kBeta = createEventType("beta");
constexpr EventType kMulti = createEventType("multi");

}  // namespace

// A subscribed handler runs exactly once per emit+dispatch, receiving the
// event payload.
TEST(EventBus, SubscribeEmitDispatchDeliversEvent) {
  EventBus bus;
  int received = -1;
  bus.subscribe<PingEvent>(kPing, [&](const PingEvent& e) { received = e.value; });

  bus.emit(kPing, PingEvent{42});
  bus.dispatch();

  EXPECT_EQ(received, 42);
}

// Every subscriber for a given event type receives a dispatched event.
TEST(EventBus, MultipleSubscribersAllReceiveEvent) {
  EventBus bus;
  int a = 0;
  int b = 0;
  bus.subscribe<PingEvent>(kPing, [&](const PingEvent& e) { a = e.value; });
  bus.subscribe<PingEvent>(kPing, [&](const PingEvent& e) { b = e.value; });

  bus.emit(kPing, PingEvent{7});
  bus.dispatch();

  EXPECT_EQ(a, 7);
  EXPECT_EQ(b, 7);
}

// A subscriber for type A is not invoked when an event of type B is dispatched.
TEST(EventBus, SubscribersReceiveOnlyTheirType) {
  EventBus bus;
  bool aCalled = false;
  bool bCalled = false;
  bus.subscribe<PingEvent>(kAlpha, [&](const PingEvent&) { aCalled = true; });
  bus.subscribe<PingEvent>(kBeta, [&](const PingEvent&) { bCalled = true; });

  bus.emit(kAlpha, PingEvent{1});
  bus.dispatch();

  EXPECT_TRUE(aCalled);
  EXPECT_FALSE(bCalled);
}

// After unsubscribing a handle, the associated handler is no longer called
// on subsequent emits.
TEST(EventBus, UnsubscribeStopsDelivery) {
  EventBus bus;
  int calls = 0;
  auto handle = bus.subscribe<PingEvent>(kPing, [&](const PingEvent&) { ++calls; });
  bus.unsubscribe(handle);

  bus.emit(kPing, PingEvent{1});
  bus.dispatch();

  EXPECT_EQ(calls, 0);
}

// Event payload bytes are preserved exactly across emit -> dispatch via the
// InlineEvent memcpy path.
TEST(EventBus, PayloadRoundTripsExactly) {
  EventBus bus;
  MultiField received{};
  bus.subscribe<MultiField>(kMulti, [&](const MultiField& e) { received = e; });

  bus.emit(kMulti, MultiField{.i = 123, .f = 4.5f, .x = 0xdeadbeefcafeULL});
  bus.dispatch();

  EXPECT_EQ(received.i, 123);
  EXPECT_FLOAT_EQ(received.f, 4.5f);
  EXPECT_EQ(received.x, 0xdeadbeefcafeULL);
}

// dispatch(maxEvents) processes at most that many queued events per call;
// the remainder stays queued for later dispatches.
TEST(EventBus, DispatchMaxEventsCapsCount) {
  EventBus bus;
  int calls = 0;
  bus.subscribe<PingEvent>(kPing, [&](const PingEvent&) { ++calls; });

  for (int i = 0; i < 10; ++i) bus.emit(kPing, PingEvent{i});

  bus.dispatch(3);
  EXPECT_EQ(calls, 3);

  bus.dispatch();
  EXPECT_EQ(calls, 10);
}

// Unsubscribing during a dispatch does not affect the currently-executing
// event (the handler list is copied before iteration), but takes effect on
// the next dispatch.
TEST(EventBus, UnsubscribeInsideHandlerAppliesNextDispatch) {
  EventBus bus;
  int aCalls = 0;
  int bCalls = 0;
  EventBus::EventHandle bHandle = 0;

  bus.subscribe<PingEvent>(kPing, [&](const PingEvent&) {
    ++aCalls;
    bus.unsubscribe(bHandle);
  });
  bHandle = bus.subscribe<PingEvent>(kPing, [&](const PingEvent&) {
    ++bCalls;
  });

  bus.emit(kPing, PingEvent{1});
  bus.dispatch();
  EXPECT_EQ(aCalls, 1);
  EXPECT_EQ(bCalls, 1) << "B should still fire for the event that triggered its unsubscribe";

  bus.emit(kPing, PingEvent{2});
  bus.dispatch();
  EXPECT_EQ(aCalls, 2);
  EXPECT_EQ(bCalls, 1) << "B should not fire after being unsubscribed";
}

// Emitting beyond the queue's capacity drops the excess events and increments
// the dropped() counter by the number of drops.
TEST(EventBus, QueueOverflowIncrementsDroppedCounter) {
  constexpr size_t kCap = 8;
  constexpr size_t kEmits = 20;
  EventBus bus(kCap);
  EXPECT_EQ(bus.dropped(), 0u);

  for (size_t i = 0; i < kEmits; ++i) bus.emit(kPing, PingEvent{static_cast<int>(i)});

  EXPECT_EQ(bus.dropped(), kEmits - kCap);
}

// Unsubscribing a handle that was never issued (or has already been removed)
// is a silent no-op, not a crash.
TEST(EventBus, UnsubscribeWithStaleHandleIsNoOp) {
  EventBus bus;
  EXPECT_NO_THROW(bus.unsubscribe(99999));

  auto handle = bus.subscribe<PingEvent>(kPing, [](const PingEvent&) {});
  bus.unsubscribe(handle);
  EXPECT_NO_THROW(bus.unsubscribe(handle));
}
