#include "unity.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "aled_state.h"
#include "self_test.h"

TEST_CASE("self_test spawns non-blocking task", "[self_test]"){
    aled_state_init_defaults(1, 8);

    TickType_t before = xTaskGetTickCount();
    self_test_run();
    TickType_t after = xTaskGetTickCount();

    TEST_ASSERT_LESS_OR_EQUAL_UINT32(pdMS_TO_TICKS(20), after - before);
    TEST_ASSERT_TRUE(self_test_is_running());

    /* give the worker task a moment to spin up */
    vTaskDelay(pdMS_TO_TICKS(10));

    self_test_cancel();

    for (int i = 0; i < 20 && self_test_is_running(); ++i){
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    TEST_ASSERT_FALSE(self_test_is_running());
}
