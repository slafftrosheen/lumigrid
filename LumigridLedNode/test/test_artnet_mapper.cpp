#include "test_artnet_mapper.hpp"
#include <gtest/gtest.h>
#include "protocols/lg_artnet_mapper.hpp"

TEST(ArtnetMapper, FirstUniverseMapsFirstPixels) {
  lg::ArtnetMapper mapper(0);
  const uint8_t universe[] = {255, 0, 0, 0, 255, 0, 0, 0, 255};
  mapper.apply_universe(0, universe, sizeof(universe));
  const auto& frame = mapper.staging_frame();

  EXPECT_EQ(frame.pixels[0].r, 255);
  EXPECT_EQ(frame.pixels[0].g, 0);
  EXPECT_EQ(frame.pixels[0].b, 0);
  EXPECT_EQ(frame.pixels[1].r, 0);
  EXPECT_EQ(frame.pixels[1].g, 255);
  EXPECT_EQ(frame.pixels[1].b, 0);
  EXPECT_EQ(frame.pixel_count, 3U);
}

TEST(ArtnetMapper, UniverseOffsetIsApplied) {
  lg::ArtnetMapper mapper(0);
  const uint8_t universe[] = {1, 2, 3};
  mapper.apply_universe(1, universe, sizeof(universe));
  const auto& frame = mapper.staging_frame();

  EXPECT_EQ(frame.pixels[170].r, 1);
  EXPECT_EQ(frame.pixels[170].g, 2);
  EXPECT_EQ(frame.pixels[170].b, 3);
  EXPECT_EQ(frame.pixel_count, 171U);
}

TEST(ArtnetMapper, ShortPacketMapsNoPixel) {
  lg::ArtnetMapper mapper(0);
  const uint8_t universe[] = {1, 2};
  mapper.apply_universe(0, universe, sizeof(universe));
  EXPECT_EQ(mapper.staging_frame().pixel_count, 0U);
}

TEST(ArtnetMapper, ReversedLaneMapsCorrectly) {
  // Handled by PixelMapper; ensure ArtnetMapper writes sequentially.
  lg::ArtnetMapper mapper(0);
  const uint8_t universe[] = {1, 2, 3};
  mapper.apply_universe(0, universe, sizeof(universe));
  EXPECT_EQ(mapper.staging_frame().pixels[0].r, 1);
}

TEST(ArtnetMapper, StagingFrameIsCopiedOnTake) {
  lg::ArtnetMapper mapper(0);
  const uint8_t universe[] = {1, 2, 3};
  mapper.apply_universe(0, universe, sizeof(universe));
  lg::LogicalFrame frame = mapper.take_frame(42, 1000);
  EXPECT_EQ(frame.sequence, 42U);
  EXPECT_EQ(frame.timestamp_ms, 1000U);
  EXPECT_EQ(frame.pixels[0].r, 1U);
}
