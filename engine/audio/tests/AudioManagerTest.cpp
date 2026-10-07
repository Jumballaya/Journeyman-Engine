#include <gtest/gtest.h>

#include <vector>

#include "AudioManager.hpp"

namespace {
std::shared_ptr<SoundBuffer> silence() { return SoundBuffer::fromSamples(std::vector<float>(64, 0.0f), 2, 48000); }
}  // namespace

// A voice's copy stands in for the audio thread: the replaced buffer must
// outlive it, then be freed on a later (main-thread) registration.
TEST(AudioManager, ReplacedBufferOutlivesItsVoices) {
  AudioManager audio;
  auto old = silence();
  std::weak_ptr<SoundBuffer> watch = old;
  audio.registerSound({"assets/a.wav", "a.wav", "a"}, old);
  std::shared_ptr<SoundBuffer> playing = old;  // what a voice would hold
  old.reset();

  audio.registerSound({"assets/a.wav", "a.wav", "a"}, silence());
  audio.registerSound({"assets/b.wav"}, silence());
  EXPECT_FALSE(watch.expired()) << "freed while a voice still had it";

  playing.reset();  // the voice drops it: on the audio thread this frees nothing
  EXPECT_FALSE(watch.expired());
  audio.registerSound({"assets/c.wav"}, silence());
  EXPECT_TRUE(watch.expired()) << "retired buffer never freed";
}

TEST(AudioManager, BufferStillNamedElsewhereStaysKnown) {
  AudioManager audio;
  auto old = silence();
  audio.registerSound({"assets/a.wav", "a"}, old);
  audio.registerSound({"a"}, silence());
  audio.registerSound({"assets/z.wav"}, silence());
  EXPECT_TRUE(audio.knows("assets/a.wav"));
  EXPECT_GE(old.use_count(), 2) << "still registered as assets/a.wav";
}
