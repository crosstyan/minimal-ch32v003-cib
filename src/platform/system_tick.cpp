#include "platform/system_tick.h"

#include <ch32fun.h>

namespace {
constexpr uint32_t ticks_per_ms = DELAY_MS_TIME;
volatile uint32_t system_millis_count;

#if defined(FUNCONF_SYSTICK_USE_HCLK) && FUNCONF_SYSTICK_USE_HCLK
constexpr uint32_t systick_control =
	SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE | SYSTICK_CTLR_STCLK;
#else
constexpr uint32_t systick_control = SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE;
#endif
}

extern "C" void system_tick_init() {
	SysTick->CTLR       = 0;
	SysTick->SR         = 0;
	SysTick->CNT        = 0;
	SysTick->CMP        = ticks_per_ms - 1U;
	system_millis_count = 0;

	NVIC_EnableIRQ(SysTick_IRQn);
	SysTick->CTLR = systick_control;
}

extern "C" uint32_t system_millis() {
	return system_millis_count;
}

extern "C" void SysTick_Handler() {
	SysTick->CMP += ticks_per_ms;
	SysTick->SR         = 0;
	system_millis_count = system_millis_count + 1U;
}
