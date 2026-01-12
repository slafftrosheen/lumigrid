#pragma once
#include "effects.h"
#include "effect_pixels.h"

#ifdef __cplusplus
extern "C" {
#endif

// New-style effect function (simpler, uses render_ctx_t)
typedef void (*effect_simple_fn)(render_ctx_t* ctx, const effect_params_t* params);

// Register a new-style effect (will be wrapped to work with existing system)
void fx_register(const char* name, effect_simple_fn fn);

// Initialize the registry (call before fx_register calls)
void fx_registry_init(void);

// Get registered effects for merging with main list
const effect_desc_t** fx_registry_get_list(int* count_out);
const effect_desc_t* fx_registry_get_by_name(const char* name);
void fx_registry_state_set_channel(effect_state_t* state, int channel);

#ifdef __cplusplus
}
#endif
