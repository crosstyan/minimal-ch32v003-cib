#include "system_tick.h"

#include <ch32fun.h>

namespace {
constexpr uint32_t ticks_per_ms = DELAY_MS_TIME;

#if defined(FUNCONF_SYSTICK_USE_HCLK) && FUNCONF_SYSTICK_USE_HCLK
constexpr uint32_t systick_control =
	SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE | SYSTICK_CTLR_STCLK;
#else
constexpr uint32_t systick_control = SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE;
#endif
}

extern "C" volatile uint32_t g_system_millis;
volatile uint32_t g_system_millis;

extern "C" void system_tick_init() {
	SysTick->CTLR   = 0;
	SysTick->SR     = 0;
	SysTick->CNT    = 0;
	SysTick->CMP    = ticks_per_ms - 1U;
	g_system_millis = 0;

	NVIC_EnableIRQ(SysTick_IRQn);
	SysTick->CTLR = systick_control;
}

extern "C" void SysTick_Handler() {
	SysTick->CMP += ticks_per_ms;
	SysTick->SR     = 0;
	g_system_millis = g_system_millis + 1U;
}
