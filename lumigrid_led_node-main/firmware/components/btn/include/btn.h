#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void btn_init(int gpio_pullup);   // e.g., GPIO0 with internal pull-up
int  btn_is_safe_boot(void);      // query at boot after init

#ifdef __cplusplus
}
#endif
