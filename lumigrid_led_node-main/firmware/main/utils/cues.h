#pragma once
#include <stdint.h>
#include <stddef.h>
void     cues_init(void);
void     cues_schedule(const char *preset, uint64_t at_ms, uint32_t fade_ms);
void     cues_active(char *out_name, size_t out_len, uint32_t *fade_remain_ms);
