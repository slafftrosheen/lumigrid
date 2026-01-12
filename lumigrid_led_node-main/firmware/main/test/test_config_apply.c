#include "unity.h"
#include "config_apply.h"
#include "storage_fs.h"

TEST_CASE("cfg_apply_all tolerates missing file","[cfg]"){
  // simulate no config.json
  // just ensure it doesn't crash
  TEST_ASSERT_EQUAL(ESP_OK, cfg_apply_all());
}
