#include <gtest/gtest.h>

#include <limits>

#include "Wire.hpp"

using net::Msg;
using net::Reader;
using net::Writer;

TEST(Wire, ValuesComeBackInOrder) {
  Writer w(Msg::Spawn);
  w.u32(0x01020304).i32(-7).f32(1.5f).f64(-2.25).str("prefab.json").u8(9).u16(512);
  Reader r(w.data());
  EXPECT_EQ(static_cast<Msg>(r.u8()), Msg::Spawn);
  EXPECT_EQ(r.u32(), 0x01020304u);
  EXPECT_EQ(r.i32(), -7);
  EXPECT_FLOAT_EQ(r.f32(), 1.5f);
  EXPECT_DOUBLE_EQ(r.f64(), -2.25);
  EXPECT_EQ(r.str(), "prefab.json");
  EXPECT_EQ(r.u8(), 9);
  EXPECT_EQ(r.u16(), 512);
  EXPECT_TRUE(r.ok());
  EXPECT_TRUE(r.done());
}

TEST(Wire, ACutShortMessageReadsAsBroken) {
  Writer w(Msg::Data);
  w.u32(5).str("a key that is long");
  std::vector<uint8_t> bytes = w.data();
  bytes.resize(bytes.size() - 3);
  Reader r(bytes);
  r.u8();
  EXPECT_EQ(r.u32(), 5u);
  EXPECT_EQ(r.str(), "");
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.u32(), 0u);  // and stays broken
}

TEST(Wire, AStringLengthPastTheEndIsRefused) {
  Writer w(Msg::Session);
  w.u32(1000000);  // a length with nothing behind it
  Reader r(w.data());
  r.u8();
  EXPECT_EQ(r.str(), "");
  EXPECT_FALSE(r.ok());
}

TEST(Wire, CountsCanBeFilledInAfterward) {
  Writer w(Msg::Bind);
  const size_t at = w.mark();
  w.u16(0).str("x").str("y");
  w.patchU16(at, 2);
  Reader r(w.data());
  r.u8();
  EXPECT_EQ(r.u16(), 2);
}

TEST(Wire, AStringOverItsCapIsRefused) {
  const std::string big(net::kMaxString + 1, 'x');
  Writer w(Msg::Data);
  w.str(big).str(big);
  Reader r(w.data());
  r.u8();
  EXPECT_EQ(r.str(), "");
  EXPECT_FALSE(r.ok());

  Reader json(w.data());  // the session store and overrides may be bigger
  json.u8();
  EXPECT_EQ(json.str(net::kMaxPacket).size(), big.size());
  EXPECT_TRUE(json.ok());
}

TEST(Wire, FloatsThatArentFiniteReadAsZero) {
  Writer w(Msg::Spawn);
  w.f32(std::numeric_limits<float>::quiet_NaN())
      .f32(-std::numeric_limits<float>::infinity())
      .f64(std::numeric_limits<double>::infinity())
      .f32(2.5f);
  Reader r(w.data());
  r.u8();
  EXPECT_EQ(r.f32(), 0.0f);
  EXPECT_EQ(r.f32(), 0.0f);
  EXPECT_EQ(r.f64(), 0.0);
  EXPECT_EQ(r.f32(), 2.5f);
  EXPECT_TRUE(r.ok());
}
