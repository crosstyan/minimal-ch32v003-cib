#pragma once

#include <stdint.h>

extern "C" volatile uint32_t g_system_millis;
extern "C" void system_tick_init();
extern "C" void SysTick_Handler() __attribute__((interrupt));

inline uint32_t system_millis() { return g_system_millis; }
