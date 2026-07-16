#include <gtest/gtest.h>
#include "config/lg_config_store.hpp"

TEST(ConfigValidator, InvalidPwmOverlapIsRejected) {
  lg::NodeConfig cfg = {};
  cfg.pwm_groups[0].enabled = true;
  cfg.pwm_groups[0].first_channel = 0;
  cfg.pwm_groups[0].channel_count = 4;

  cfg.pwm_groups[1].enabled = true;
  cfg.pwm_groups[1].first_channel = 2; // Overlaps 0-3
  cfg.pwm_groups[1].channel_count = 2;

  lg::ConfigStore store;
  EXPECT_EQ(store.validate(cfg), lg::ConfigError::PwmOverlap);
}

TEST(ConfigValidator, PwmChannel16IsRejected) {
  lg::NodeConfig cfg = {};
  cfg.pwm_groups[0].enabled = true;
  cfg.pwm_groups[0].first_channel = 15;
  cfg.pwm_groups[0].channel_count = 2; // Exceeds bounds (15+2 = 17)

  lg::ConfigStore store;
  EXPECT_EQ(store.validate(cfg), lg::ConfigError::InvalidPwmGroup);
}
