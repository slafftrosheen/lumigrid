#include "effects_registry.h"
#include "effects.h"
#include "effect_pixels.h"
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#define MAX_EXTRA_FX 16

typedef struct {
  char name[32];
  effect_simple_fn simple_fn;
  effect_desc_t desc;
} fx_entry_t;

static fx_entry_t s_fx_table[MAX_EXTRA_FX];
static int s_fx_count = 0;

// Wrapper state for simple effects - stores index into fx_table
typedef struct {
  effect_state_t base;
  int fx_index;  // Index into s_fx_table
  int npx;
  int channel;
} simple_effect_state_t;

static void simple_effect_destroy(effect_state_t* state) {
  if (state) free(state);
}

// Wrapper init functions - one per slot
#define MAKE_INIT_FN(idx) \
static void simple_effect_init_##idx(effect_state_t** state_out, int npx, const effect_params_t* p) { \
  (void)p; \
  simple_effect_state_t* state = (simple_effect_state_t*)calloc(1, sizeof(simple_effect_state_t)); \
  if (!state) { *state_out = NULL; return; } \
  state->base.destroy = simple_effect_destroy; \
  state->fx_index = idx; \
  state->npx = npx; \
  state->channel = 0; \
  *state_out = (effect_state_t*)state; \
}

MAKE_INIT_FN(0)
MAKE_INIT_FN(1)
MAKE_INIT_FN(2)
MAKE_INIT_FN(3)
MAKE_INIT_FN(4)
MAKE_INIT_FN(5)
MAKE_INIT_FN(6)
MAKE_INIT_FN(7)
MAKE_INIT_FN(8)
MAKE_INIT_FN(9)
MAKE_INIT_FN(10)
MAKE_INIT_FN(11)
MAKE_INIT_FN(12)
MAKE_INIT_FN(13)
MAKE_INIT_FN(14)
MAKE_INIT_FN(15)

static effect_init_fn s_init_fns[MAX_EXTRA_FX] = {
  simple_effect_init_0, simple_effect_init_1, simple_effect_init_2, simple_effect_init_3,
  simple_effect_init_4, simple_effect_init_5, simple_effect_init_6, simple_effect_init_7,
  simple_effect_init_8, simple_effect_init_9, simple_effect_init_10, simple_effect_init_11,
  simple_effect_init_12, simple_effect_init_13, simple_effect_init_14, simple_effect_init_15,
};

static void simple_effect_render(effect_state_t* state, uint32_t now_ms, const effect_params_t* p, px_rgba_t* fb, int npx) {
  if (!state) return;
  simple_effect_state_t* s = (simple_effect_state_t*)state;
  
  if (s->fx_index < 0 || s->fx_index >= s_fx_count) return;
  
  // Build render context
  render_ctx_t ctx = {
    .framebuf = fb,
    .count = npx,
    .channel = s->channel,
    .now_ms = now_ms,
    .brightness = p ? p->brightness : 1.0f
  };
  
  // Call the simple effect function
  effect_simple_fn fn = s_fx_table[s->fx_index].simple_fn;
  if (fn) {
    fn(&ctx, p);
  }
}

void fx_registry_init(void) {
  s_fx_count = 0;
  memset(s_fx_table, 0, sizeof(s_fx_table));
}

void fx_register(const char* name, effect_simple_fn fn) {
  if (!name || !fn || s_fx_count >= MAX_EXTRA_FX) return;
  
  fx_entry_t* entry = &s_fx_table[s_fx_count];
  strncpy(entry->name, name, sizeof(entry->name) - 1);
  entry->name[sizeof(entry->name) - 1] = '\0';
  entry->simple_fn = fn;
  
  // Setup effect descriptor with wrappers
  entry->desc.name = entry->name;
  entry->desc.init = s_init_fns[s_fx_count];
  entry->desc.render = simple_effect_render;
  
  s_fx_count++;
}

// Get registered effects (for merging with main list)
const effect_desc_t** fx_registry_get_list(int* count_out) {
  static const effect_desc_t* list[MAX_EXTRA_FX];
  for (int i = 0; i < s_fx_count; i++) {
    list[i] = &s_fx_table[i].desc;
  }
  if (count_out) *count_out = s_fx_count;
  return list;
}

const effect_desc_t* fx_registry_get_by_name(const char* name) {
  if (!name) return NULL;
  for (int i = 0; i < s_fx_count; i++) {
    if (strcasecmp(s_fx_table[i].name, name) == 0) {
      return &s_fx_table[i].desc;
    }
  }
  return NULL;
}

void fx_registry_state_set_channel(effect_state_t* state, int channel) {
  if (!state) {
    return;
  }
  if (state->destroy != simple_effect_destroy) {
    return; // not a simple effect wrapper
  }
  simple_effect_state_t* s = (simple_effect_state_t*)state;
  s->channel = channel;
}
