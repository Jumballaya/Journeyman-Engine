#include <gtest/gtest.h>

#include <variant>

#include "posteffects/PostEffect.hpp"
#include "posteffects/PostEffectChain.hpp"
#include "posteffects/PostEffectHandle.hpp"

TEST(PostEffectChain, AddReturnsValidHandle) {
  PostEffectChain chain;
  PostEffectHandle h = chain.add(PostEffect{});
  EXPECT_TRUE(h.isValid());
  EXPECT_TRUE(chain.contains(h));
  EXPECT_EQ(chain.size(), 1u);
}

TEST(PostEffectChain, AddIgnoresIncomingHandleField) {
  PostEffectChain chain;
  PostEffect effect{};
  effect.handle = PostEffectHandle{999};
  PostEffectHandle h = chain.add(effect);
  EXPECT_NE(h.id, 999u);
  EXPECT_TRUE(h.isValid());
}

TEST(PostEffectChain, DistinctHandlesForSequentialAdds) {
  PostEffectChain chain;
  PostEffectHandle a = chain.add(PostEffect{});
  PostEffectHandle b = chain.add(PostEffect{});
  EXPECT_NE(a, b);
  EXPECT_TRUE(chain.contains(a));
  EXPECT_TRUE(chain.contains(b));
}

TEST(PostEffectChain, RemoveDropsEffect) {
  PostEffectChain chain;
  PostEffectHandle h = chain.add(PostEffect{});
  chain.remove(h);
  EXPECT_FALSE(chain.contains(h));
  EXPECT_EQ(chain.size(), 0u);
}

TEST(PostEffectChain, RemoveDoesNotInvalidateOtherHandles) {
  PostEffectChain chain;
  PostEffectHandle a = chain.add(PostEffect{});
  PostEffectHandle b = chain.add(PostEffect{});
  PostEffectHandle c = chain.add(PostEffect{});
  chain.remove(b);
  EXPECT_TRUE(chain.contains(a));
  EXPECT_FALSE(chain.contains(b));
  EXPECT_TRUE(chain.contains(c));
  EXPECT_EQ(chain.size(), 2u);
}

TEST(PostEffectChain, DisabledEffectsSkippedInIteration) {
  PostEffectChain chain;
  PostEffectHandle a = chain.add(PostEffect{});
  PostEffectHandle b = chain.add(PostEffect{});
  PostEffectHandle c = chain.add(PostEffect{});
  chain.setEnabled(b, false);

  auto enabled = chain.enabledEffects();
  ASSERT_EQ(enabled.size(), 2u);
  EXPECT_EQ(enabled[0]->handle, a);
  EXPECT_EQ(enabled[1]->handle, c);

  chain.setEnabled(b, true);
  auto reenabled = chain.enabledEffects();
  ASSERT_EQ(reenabled.size(), 3u);
  EXPECT_EQ(reenabled[0]->handle, a);
  EXPECT_EQ(reenabled[1]->handle, b);
  EXPECT_EQ(reenabled[2]->handle, c);
}

TEST(PostEffectChain, MoveToReordersInPlace) {
  PostEffectChain chain;
  PostEffectHandle a = chain.add(PostEffect{});
  PostEffectHandle b = chain.add(PostEffect{});
  PostEffectHandle c = chain.add(PostEffect{});

  chain.moveTo(a, 2);
  auto enabled = chain.enabledEffects();
  ASSERT_EQ(enabled.size(), 3u);
  EXPECT_EQ(enabled[0]->handle, b);
  EXPECT_EQ(enabled[1]->handle, c);
  EXPECT_EQ(enabled[2]->handle, a);
}

TEST(PostEffectChain, MoveToClampsOutOfRange) {
  PostEffectChain chain;
  PostEffectHandle a = chain.add(PostEffect{});
  PostEffectHandle b = chain.add(PostEffect{});
  PostEffectHandle c = chain.add(PostEffect{});

  chain.moveTo(a, 100);
  auto enabled = chain.enabledEffects();
  ASSERT_EQ(enabled.size(), 3u);
  EXPECT_EQ(enabled[0]->handle, b);
  EXPECT_EQ(enabled[1]->handle, c);
  EXPECT_EQ(enabled[2]->handle, a);
}

TEST(PostEffectChain, MoveToFrontPreservesOthers) {
  PostEffectChain chain;
  PostEffectHandle a = chain.add(PostEffect{});
  PostEffectHandle b = chain.add(PostEffect{});
  PostEffectHandle c = chain.add(PostEffect{});

  chain.moveTo(c, 0);
  auto enabled = chain.enabledEffects();
  ASSERT_EQ(enabled.size(), 3u);
  EXPECT_EQ(enabled[0]->handle, c);
  EXPECT_EQ(enabled[1]->handle, a);
  EXPECT_EQ(enabled[2]->handle, b);
}

TEST(PostEffectChain, HandleNotReusedAfterRemove) {
  PostEffectChain chain;
  PostEffectHandle a = chain.add(PostEffect{});
  chain.remove(a);
  PostEffectHandle b = chain.add(PostEffect{});
  EXPECT_GT(b.id, a.id);
}

TEST(PostEffectChain, SetUniformOverwrites) {
  PostEffectChain chain;
  PostEffectHandle h = chain.add(PostEffect{});
  chain.setUniform(h, "x", 1.0f);
  chain.setUniform(h, "x", 2.0f);
  const PostEffect* effect = chain.get(h);
  ASSERT_NE(effect, nullptr);
  auto it = effect->uniforms.find("x");
  ASSERT_NE(it, effect->uniforms.end());
  EXPECT_FLOAT_EQ(std::get<float>(it->second), 2.0f);
}

TEST(PostEffectChain, NoOpOnStaleHandle) {
  PostEffectChain chain;
  PostEffectHandle h = chain.add(PostEffect{});
  chain.remove(h);
  const size_t sizeBefore = chain.size();

  EXPECT_NO_THROW(chain.remove(h));
  EXPECT_NO_THROW(chain.setEnabled(h, false));
  EXPECT_NO_THROW(chain.setUniform(h, "x", 1.0f));
  EXPECT_NO_THROW(chain.setAuxTexture(h, TextureHandle{}));
  EXPECT_NO_THROW(chain.moveTo(h, 0));

  EXPECT_EQ(chain.size(), sizeBefore);
  EXPECT_EQ(chain.get(h), nullptr);
}
