#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "Transport.hpp"
#include "Wire.hpp"

using net::TransportEvent;

namespace {

// Polls both until `done` or a couple of seconds pass.
template <typename Done>
void pump(net::Transport& a, net::Transport& b, std::vector<TransportEvent>& atA, std::vector<TransportEvent>& atB,
          Done done) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!done() && std::chrono::steady_clock::now() < until) {
    a.poll(atA);
    b.poll(atB);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

bool has(const std::vector<TransportEvent>& events, TransportEvent::Kind kind) {
  for (const auto& e : events) {
    if (e.kind == kind) return true;
  }
  return false;
}

}  // namespace

// Two transports on this machine: connect, both kinds of channel, disconnect.
TEST(EnetTransport, ConnectsSendsAndHangsUp) {
  auto host = net::makeEnetTransport();
  auto guest = net::makeEnetTransport();
  ASSERT_TRUE(host->open(0, 4));
  ASSERT_TRUE(guest->open(0, 4));
  ASSERT_NE(host->port(), 0);

  const net::ConnId toHost = guest->connect("127.0.0.1:" + std::to_string(host->port()));
  ASSERT_NE(toHost, 0u);
  std::vector<TransportEvent> atHost, atGuest;
  pump(*host, *guest, atHost, atGuest, [&] { return has(atHost, TransportEvent::Kind::Connected) && has(atGuest, TransportEvent::Kind::Connected); });
  ASSERT_TRUE(has(atHost, TransportEvent::Kind::Connected));
  const net::ConnId toGuest = atHost[0].conn;
  EXPECT_EQ(host->address(toGuest).rfind("127.0.0.1:", 0), 0u);

  guest->send(toHost, net::kReliable, {1, 2, 3});
  guest->send(toHost, net::kUnreliable, {4});
  atHost.clear();
  pump(*host, *guest, atHost, atGuest, [&] { return atHost.size() >= 2; });
  ASSERT_EQ(atHost.size(), 2u);
  EXPECT_EQ(atHost[0].data, (std::vector<uint8_t>{1, 2, 3}));
  EXPECT_EQ(atHost[1].data, (std::vector<uint8_t>{4}));

  guest->disconnect(toHost);
  atHost.clear();
  pump(*host, *guest, atHost, atGuest, [&] { return has(atHost, TransportEvent::Kind::Disconnected); });
  EXPECT_TRUE(has(atHost, TransportEvent::Kind::Disconnected));
}

// Simulated trouble: everything late, unreliable messages all lost at loss 1.
TEST(EnetTransport, ConditionsDelayAndDrop) {
  auto host = net::makeEnetTransport();
  auto guest = net::makeEnetTransport();
  ASSERT_TRUE(host->open(0, 4));
  ASSERT_TRUE(guest->open(0, 4));
  const net::ConnId toHost = guest->connect("127.0.0.1:" + std::to_string(host->port()));
  std::vector<TransportEvent> atHost, atGuest;
  pump(*host, *guest, atHost, atGuest, [&] {
    return has(atGuest, TransportEvent::Kind::Connected) && has(atHost, TransportEvent::Kind::Connected);
  });

  guest->setConditions({.latency = 0.15f, .loss = 1.0f});
  atHost.clear();
  const auto sent = std::chrono::steady_clock::now();
  guest->send(toHost, net::kUnreliable, {7});
  guest->send(toHost, net::kReliable, {8});
  pump(*host, *guest, atHost, atGuest, [&] { return has(atHost, TransportEvent::Kind::Received); });
  const auto took = std::chrono::steady_clock::now() - sent;
  ASSERT_EQ(atHost.size(), 1u);
  EXPECT_EQ(atHost[0].data, (std::vector<uint8_t>{8}));
  EXPECT_GE(took, std::chrono::milliseconds(140));
}

TEST(EnetTransport, NonsenseAddressesAreRefused) {
  auto t = net::makeEnetTransport();
  ASSERT_TRUE(t->open(0, 2));
  EXPECT_EQ(t->connect("no-port-here"), 0u);
  EXPECT_EQ(t->connect("127.0.0.1:99999"), 0u);
  EXPECT_EQ(t->connect(":7777"), 0u);
}
