#include "test_pixel_mapper.hpp"
#include <gtest/gtest.h>
#include "config/lg_config.hpp"
#include "output/lg_pixel_mapper.hpp"
#include "frame/lg_frame.hpp"

TEST(PixelMapper, DisabledLaneOutputsBlack) {
  lg::NodeConfig cfg = {};
  cfg.lanes[0].enabled = false;
  cfg.lanes[0].pixel_count = 10;
  cfg.lanes[0].logical_start = 0;

  lg::PixelMapper mapper(cfg);
  lg::LogicalFrame src{};
  src.pixels[0] = {255, 255, 255};
  src.pixel_count = 1;
  lg::PhysicalFrame dst{};
  mapper.map(src, dst);

  for (uint16_t i = 0; i < 10; ++i) {
    EXPECT_EQ(dst.lanes[0][i].r, 0);
    EXPECT_EQ(dst.lanes[0][i].g, 0);
    EXPECT_EQ(dst.lanes[0][i].b, 0);
  }
}

TEST(PixelMapper, BrightnessScalesCorrectly) {
  lg::NodeConfig cfg = {};
  cfg.lanes[0].enabled = true;
  cfg.lanes[0].pixel_count = 1;
  cfg.lanes[0].logical_start = 0;
  cfg.lanes[0].brightness = 128;

  lg::PixelMapper mapper(cfg);
  lg::LogicalFrame src{};
  src.pixels[0] = {255, 0, 0};
  src.pixel_count = 1;
  lg::PhysicalFrame dst{};
  mapper.map(src, dst);

  EXPECT_EQ(dst.lanes[0][0].r, 128);
}

TEST(PixelMapper, ReversedLaneMapsCorrectly) {
  lg::NodeConfig cfg = {};
  cfg.lanes[0].enabled = true;
  cfg.lanes[0].pixel_count = 3;
  cfg.lanes[0].logical_start = 0;
  cfg.lanes[0].reversed = true;

  lg::PixelMapper mapper(cfg);
  lg::LogicalFrame src{};
  for (size_t i = 0; i < 3; ++i) {
    src.pixels[i] = {static_cast<uint8_t>(i + 1), 0, 0};
  }
  src.pixel_count = 3;
  lg::PhysicalFrame dst{};
  mapper.map(src, dst);

  EXPECT_EQ(dst.lanes[0][0].r, 3);
  EXPECT_EQ(dst.lanes[0][1].r, 2);
  EXPECT_EQ(dst.lanes[0][2].r, 1);
}

TEST(PixelMapper, SourceOutOfRangeIsBlack) {
  lg::NodeConfig cfg = {};
  cfg.lanes[0].enabled = true;
  cfg.lanes[0].pixel_count = 10;
  cfg.lanes[0].logical_start = 0;

  lg::PixelMapper mapper(cfg);
  lg::LogicalFrame src{};
  src.pixels[0] = {255, 255, 255};
  src.pixel_count = 0;
  lg::PhysicalFrame dst{};
  mapper.map(src, dst);

  EXPECT_EQ(dst.lanes[0][0].r, 0);
}

TEST(PixelMapper, BrightnessZeroEmitsBlack) {
  lg::NodeConfig cfg = {};
  cfg.lanes[0].enabled = true;
  cfg.lanes[0].pixel_count = 1;
  cfg.lanes[0].logical_start = 0;
  cfg.lanes[0].brightness = 0;

  lg::PixelMapper mapper(cfg);
  lg::LogicalFrame src{};
  src.pixels[0] = {255, 255, 255};
  src.pixel_count = 1;
  lg::PhysicalFrame dst{};
  mapper.map(src, dst);

  EXPECT_EQ(dst.lanes[0][0].r, 0);
  EXPECT_EQ(dst.lanes[0][0].g, 0);
  EXPECT_EQ(dst.lanes[0][0].b, 0);
}
