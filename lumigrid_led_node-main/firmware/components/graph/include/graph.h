#pragma once
#include <stdbool.h>
#include "effect_pixels.h"

#ifdef __cplusplus
extern "C" {
#endif

void graph_init(void);
bool graph_enabled(void);
void graph_reload(void);
bool graph_render_channel(int ch, render_ctx_t* ctx);

#ifdef __cplusplus
}
#endif
