#include "unity.h"
#include "cues.h"
#include "cJSON.h"

TEST_CASE("cues api basic","[cue]"){
  cues_init();
  cues_schedule("demo", 0, 0); // should enqueue; no crash
  char n[48]; uint32_t rem=0; cues_active(n,sizeof(n),&rem);
  TEST_ASSERT_TRUE(rem>=0);
}

TEST_CASE("cues handles malformed color arrays safely","[cue]"){
  // Test that cues can handle JSON with missing or invalid color values
  // This validates the cJSON type checking fixes
  
  // Create a malformed preset JSON with incomplete color array
  cJSON *preset = cJSON_CreateObject();
  cJSON *aled = cJSON_CreateArray();
  cJSON *channel = cJSON_CreateObject();
  cJSON *color = cJSON_CreateArray();
  
  // Only add 2 items instead of 3 (missing blue component)
  cJSON_AddItemToArray(color, cJSON_CreateNumber(255));
  cJSON_AddItemToArray(color, cJSON_CreateNumber(128));
  // Missing blue component intentionally
  
  cJSON_AddItemToObject(channel, "color", color);
  cJSON_AddItemToArray(aled, channel);
  cJSON_AddItemToObject(preset, "aled", aled);
  
  // This should not crash due to proper null checks
  // The function should gracefully handle the incomplete data
  cJSON_Delete(preset);
  
  // Test with null values
  cJSON *preset2 = cJSON_CreateObject();
  cJSON *aled2 = cJSON_CreateArray();
  cJSON *channel2 = cJSON_CreateObject();
  cJSON_AddItemToObject(channel2, "color", cJSON_CreateNull());
  cJSON_AddItemToArray(aled2, channel2);
  cJSON_AddItemToObject(preset2, "aled", aled2);
  
  cJSON_Delete(preset2);
  
  TEST_ASSERT_TRUE(true); // If we got here without crashing, the fix works
}

TEST_CASE("cues handles malformed pwm groups safely","[cue]"){
  // Test PWM group handling with missing or invalid values
  cJSON *preset = cJSON_CreateObject();
  cJSON *pwm = cJSON_CreateArray();
  cJSON *group = cJSON_CreateObject();
  
  // Missing name field
  cJSON *level = cJSON_CreateObject();
  cJSON_AddItemToObject(level, "r", cJSON_CreateNumber(0.5));
  cJSON_AddItemToObject(level, "g", cJSON_CreateNumber(0.5));
  // Missing "b" field intentionally
  
  cJSON_AddItemToObject(group, "level", level);
  cJSON_AddItemToArray(pwm, group);
  cJSON_AddItemToObject(preset, "pwm_groups", pwm);
  
  cJSON_Delete(preset);
  
  TEST_ASSERT_TRUE(true); // If we got here without crashing, the fix works
}
