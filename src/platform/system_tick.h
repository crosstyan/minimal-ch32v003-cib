#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void system_tick_init(void);
uint32_t system_millis(void);
void SysTick_Handler(void) __attribute__((interrupt));

#ifdef __cplusplus
}
#endif
