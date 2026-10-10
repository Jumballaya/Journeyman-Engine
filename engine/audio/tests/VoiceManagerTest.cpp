#include <gtest/gtest.h>

#include <vector>

#include "VoiceManager.hpp"

namespace {
std::shared_ptr<SoundBuffer> constant(float value, size_t frames) {
  return SoundBuffer::fromSamples(std::vector<float>(frames * 2, value), 2, 48000);
}

VoiceCommand play(SoundInstanceId id, std::shared_ptr<SoundBuffer> buf, bool loop = false,
                  AudioBus bus = AudioBus::Sfx) {
  VoiceCommand c;
  c.type = VoiceCommand::Type::Play;
  c.instance = id;
  c.buffer = std::move(buf);
  c.looping = loop;
  c.bus = bus;
  return c;
}

VoiceCommand simple(VoiceCommand::Type type, SoundInstanceId id = 0, uint32_t frames = 0) {
  VoiceCommand c;
  c.type = type;
  c.instance = id;
  c.frames = frames;
  return c;
}
}  // namespace

TEST(VoiceManager, OneShotPlaysThenFrees) {
  VoiceManager vm;
  vm.apply(play(1, constant(0.25f, 4)));
  std::vector<float> out(16);
  vm.mix(out.data(), 8, 2);
  EXPECT_FLOAT_EQ(out[0], 0.25f);
  EXPECT_FLOAT_EQ(out[7], 0.25f);
  EXPECT_FLOAT_EQ(out[8], 0.0f);  // past the end
  EXPECT_EQ(vm.activeVoiceCount(), 0u);
}

// A stop queued right after play (same frame) must find the voice.
TEST(VoiceManager, StopByInstanceAfterPlay) {
  VoiceManager vm;
  vm.apply(play(7, constant(0.5f, 100), true));
  vm.apply(simple(VoiceCommand::Type::Stop, 7));
  std::vector<float> out(8);
  vm.mix(out.data(), 4, 2);
  EXPECT_FLOAT_EQ(out[0], 0.0f);
}

// Fades advance per sample: a 4-frame fade is silent after 4 frames.
TEST(VoiceManager, FadeIsSampleAccurate) {
  VoiceManager vm;
  vm.apply(play(1, constant(0.5f, 1000), true));
  vm.apply(simple(VoiceCommand::Type::FadeOut, 1, 4));
  std::vector<float> out(16);
  vm.mix(out.data(), 8, 2);
  EXPECT_GT(out[0], 0.0f);
  EXPECT_LT(out[4], out[0]);
  EXPECT_FLOAT_EQ(out[10], 0.0f);
  EXPECT_EQ(vm.activeVoiceCount(), 0u);
}

TEST(VoiceManager, BusVolumeScalesOnlyItsBus) {
  VoiceManager vm;
  VoiceCommand bus;
  bus.type = VoiceCommand::Type::SetBusGain;
  bus.bus = AudioBus::Music;
  bus.value = 0.0f;
  vm.apply(bus);
  vm.apply(play(1, constant(0.25f, 10), false, AudioBus::Music));
  vm.apply(play(2, constant(0.25f, 10), false, AudioBus::Sfx));
  std::vector<float> out(4);
  vm.mix(out.data(), 2, 2);
  EXPECT_FLOAT_EQ(out[0], 0.25f);
}

TEST(VoiceManager, LimiterKeepsOutputBelowOne) {
  VoiceManager vm;
  for (SoundInstanceId i = 1; i <= 20; ++i) vm.apply(play(i, constant(0.9f, 10)));
  std::vector<float> out(4);
  vm.mix(out.data(), 2, 2);
  EXPECT_LE(out[0], 1.0f);
  EXPECT_GT(out[0], 0.8f);
}

// Undecodable bytes are a null buffer, not an exception the editor can't catch.
TEST(SoundBuffer, UndecodableBytesAreNull) {
  EXPECT_EQ(SoundBuffer::decode({'n', 'o', 'p', 'e'}), nullptr);
  EXPECT_EQ(SoundBuffer::fromFile("/nonexistent/sound.wav"), nullptr);
}

// What the main thread is told plays: through a fade until silent; not once
// stopped, stolen or never started; a Play not yet applied counts.
TEST(VoiceManager, PlayingTracksEveryWayASoundEnds) {
  VoiceManager vm;
  std::vector<float> out(64);
  EXPECT_TRUE(vm.playing().contains(1));  // queued, not applied
  EXPECT_FALSE(vm.playing().contains(0));

  vm.apply(play(1, constant(0.5f, 1000), true));
  vm.apply(simple(VoiceCommand::Type::FadeOut, 1, 16));
  vm.mix(out.data(), 8, 2);
  EXPECT_TRUE(vm.playing().contains(1));  // mid-fade
  vm.mix(out.data(), 16, 2);
  EXPECT_FALSE(vm.playing().contains(1));  // faded out

  vm.apply(play(2, constant(0.5f, 1000), true));
  vm.apply(simple(VoiceCommand::Type::Stop, 2));
  EXPECT_FALSE(vm.playing().contains(2));

  // Fill every voice: 3 (sfx) then music; the next play steals 3.
  vm.apply(play(3, constant(0.5f, 1000), true));
  SoundInstanceId id = 4;
  for (; id < 3 + VoiceManager::kMaxVoices; ++id) vm.apply(play(id, constant(0.5f, 1000), true, AudioBus::Music));
  vm.apply(play(id, constant(0.5f, 1000), true));
  EXPECT_FALSE(vm.playing().contains(3));  // stolen
  EXPECT_TRUE(vm.playing().contains(id));

  // All music now but `id`: steal it, then a play finds only music and doesn't start.
  vm.apply(play(id + 1, constant(0.5f, 1000), true, AudioBus::Music));
  EXPECT_FALSE(vm.playing().contains(id));
  vm.apply(play(id + 2, constant(0.5f, 1000), true));
  EXPECT_FALSE(vm.playing().contains(id + 2));  // rejected
  EXPECT_TRUE(vm.playing().contains(id + 3));   // not applied yet
}
